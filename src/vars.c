#include "vars.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* A plain growable array with linear search. A shell has ~50-100
 * variables; a hash table would be faster on paper but slower to read,
 * and lookups are nowhere near our bottleneck (fork+exec is). */
typedef struct {
    char *name;
    char *value;     /* NULL = declared (e.g. `export x`) but no value */
    bool exported;
} var_t;

static var_t *g_vars;
static size_t g_nvars, g_capvars;

static var_t *find(const char *name)
{
    for (size_t i = 0; i < g_nvars; i++)
        if (strcmp(g_vars[i].name, name) == 0)
            return &g_vars[i];
    return NULL;
}

static var_t *create(const char *name)
{
    if (g_nvars == g_capvars) {
        size_t ncap = g_capvars ? g_capvars * 2 : 64;
        var_t *nv = realloc(g_vars, ncap * sizeof *nv);
        if (!nv)
            return NULL;
        g_vars = nv;
        g_capvars = ncap;
    }
    char *n = strdup(name);
    if (!n)
        return NULL;
    var_t *v = &g_vars[g_nvars++];
    v->name = n;
    v->value = NULL;
    v->exported = false;
    return v;
}

bool vars_is_name(const char *s, size_t len)
{
    if (len == 0 || !(isalpha((unsigned char)s[0]) || s[0] == '_'))
        return false;
    for (size_t i = 1; i < len; i++)
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_'))
            return false;
    return true;
}

const char *vars_get(const char *name)
{
    var_t *v = find(name);
    return v ? v->value : NULL;
}

int vars_set(const char *name, const char *value)
{
    var_t *v = find(name);
    if (!v && !(v = create(name)))
        return -1;
    char *copy = strdup(value);
    if (!copy)
        return -1;
    free(v->value);
    v->value = copy;
    return 0;
}

int vars_export(const char *name)
{
    var_t *v = find(name);
    if (!v && !(v = create(name)))
        return -1;
    v->exported = true;
    return 0;
}

void vars_unset(const char *name)
{
    var_t *v = find(name);
    if (!v)
        return;
    free(v->name);
    free(v->value);
    *v = g_vars[--g_nvars];          /* order doesn't matter: swap-remove */
}

void vars_init(char **envp)
{
    for (char **e = envp; e && *e; e++) {
        const char *eq = strchr(*e, '=');
        if (!eq || !vars_is_name(*e, (size_t)(eq - *e)))
            continue;                /* skip junk like "=C:" or "1x=2" */
        char *name = strndup(*e, (size_t)(eq - *e));
        if (!name)
            continue;
        /* Like bash, never inherit IFS: a hostile parent could use it to
         * change how every word of every script gets split. */
        if (strcmp(name, "IFS") != 0) {
            vars_set(name, eq + 1);
            vars_export(name);
        }
        free(name);
    }

    vars_set("IFS", " \t\n");

    /* bash marks OLDPWD exported from the start (with no value until the
     * first cd), so children see it once it is set. */
    vars_export("OLDPWD");

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd)) {
        vars_set("PWD", cwd);
        vars_export("PWD");
    }
}

void vars_free_all(void)
{
    for (size_t i = 0; i < g_nvars; i++) {
        free(g_vars[i].name);
        free(g_vars[i].value);
    }
    free(g_vars);
    g_vars = NULL;
    g_nvars = g_capvars = 0;
}

/* ---------- export -p ---------- */

static int cmp_var_ptr(const void *a, const void *b)
{
    const var_t *x = *(const var_t *const *)a;
    const var_t *y = *(const var_t *const *)b;
    return strcmp(x->name, y->name);
}

void vars_print_exported(void)
{
    const var_t **list = malloc((g_nvars ? g_nvars : 1) * sizeof *list);
    if (!list)
        return;
    size_t n = 0;
    for (size_t i = 0; i < g_nvars; i++)
        if (g_vars[i].exported)
            list[n++] = &g_vars[i];
    qsort(list, n, sizeof *list, cmp_var_ptr);

    for (size_t i = 0; i < n; i++) {
        printf("declare -x %s", list[i]->name);
        if (list[i]->value) {
            /* Re-quote so the output can be pasted back into a shell. */
            putchar('=');
            putchar('"');
            for (const char *c = list[i]->value; *c; c++) {
                if (strchr("\"\\$`", *c))
                    putchar('\\');
                putchar(*c);
            }
            putchar('"');
        }
        putchar('\n');
    }
    free(list);
}

/* ---------- envp for children ---------- */

static char *join_kv(const char *k, const char *v)
{
    size_t lk = strlen(k), lv = strlen(v);
    char *s = malloc(lk + lv + 2);
    if (!s)
        return NULL;
    memcpy(s, k, lk);
    s[lk] = '=';
    memcpy(s + lk + 1, v, lv + 1);
    return s;
}

char **vars_build_envp(const assign_t *ov, size_t nov)
{
    char **env = calloc(g_nvars + nov + 1, sizeof *env);
    if (!env)
        return NULL;
    size_t n = 0;

    for (size_t i = 0; i < g_nvars; i++) {
        const var_t *v = &g_vars[i];
        if (!v->exported || !v->value)
            continue;
        bool overridden = false;
        for (size_t j = 0; j < nov; j++)
            if (strcmp(ov[j].name, v->name) == 0)
                overridden = true;
        if (overridden)
            continue;
        if (!(env[n++] = join_kv(v->name, v->value)))
            goto oom;
    }
    for (size_t j = 0; j < nov; j++) {
        /* If the same name appears twice (`A=1 A=2 cmd`), the last wins. */
        bool later = false;
        for (size_t k = j + 1; k < nov; k++)
            if (strcmp(ov[j].name, ov[k].name) == 0)
                later = true;
        if (later)
            continue;
        if (!(env[n++] = join_kv(ov[j].name, ov[j].value)))
            goto oom;
    }
    env[n] = NULL;
    return env;

oom:
    vars_free_envp(env);
    return NULL;
}

void vars_free_envp(char **envp)
{
    if (!envp)
        return;
    for (char **e = envp; *e; e++)
        free(*e);
    free(envp);
}
