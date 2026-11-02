#include "builtins.h"
#include "shell.h"
#include "vars.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------- cd [dir | -] ---------- */

static int builtin_cd(int argc, char *const argv[])
{
    const char *target;

    if (argc > 2) {
        /* 2 = usage error. bash <= 5.2 returned 1 here; bash 5.3 returns 2. */
        fprintf(stderr, "%s: cd: too many arguments\n", MSH_NAME);
        return 2;
    }
    if (argc == 1) {                       /* plain `cd` goes home */
        target = vars_get("HOME");
        if (!target) {
            fprintf(stderr, "%s: cd: HOME not set\n", MSH_NAME);
            return 1;
        }
    } else if (strcmp(argv[1], "-") == 0) { /* `cd -` goes back */
        target = vars_get("OLDPWD");
        if (!target) {
            fprintf(stderr, "%s: cd: OLDPWD not set\n", MSH_NAME);
            return 1;
        }
    } else {
        target = argv[1];
    }

    /* Copy target: it may point into the variable table, and we are
     * about to overwrite OLDPWD (which `cd -` read it from). */
    char tgt[PATH_MAX];
    snprintf(tgt, sizeof tgt, "%s", target);
    target = tgt;

    char old[PATH_MAX];
    bool have_old = getcwd(old, sizeof old) != NULL;

    if (chdir(target) != 0) {
        fprintf(stderr, "%s: cd: %s: %s\n", MSH_NAME, target, strerror(errno));
        return 1;
    }

    /* Keep PWD/OLDPWD up to date, like every POSIX shell, so child
     * programs and `cd -` see the right values. */
    char now[PATH_MAX];
    if (have_old)
        vars_set("OLDPWD", old);
    if (getcwd(now, sizeof now)) {
        vars_set("PWD", now);
        if (argc == 2 && strcmp(argv[1], "-") == 0)
            printf("%s\n", now);           /* `cd -` prints where it went */
    }
    return 0;
}

/* ---------- exit [n]  (and quit, our alias) ---------- */

static bool parse_status(const char *s, long *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || end == s || *end != '\0')
        return false;
    *out = v;
    return true;
}

/* We don't call exit() here. We set a flag and let main() leave its loop,
 * so there is exactly one place where the shell shuts down and frees
 * memory (and, later, saves history and kills background jobs). */
static void request_exit(int code)
{
    g_shell.should_exit = true;
    g_shell.exit_code = code & 0xFF;       /* exit codes are 8 bits */
}

static int builtin_exit(int argc, char *const argv[])
{
    if (g_shell.interactive)
        fprintf(stderr, "exit\n");         /* bash prints this too */

    if (argc == 1) {
        request_exit(g_shell.last_status); /* POSIX: exit with $? */
        return g_shell.last_status;
    }

    long code;
    if (!parse_status(argv[1], &code)) {
        fprintf(stderr, "%s: exit: %s: numeric argument required\n",
                MSH_NAME, argv[1]);
        request_exit(2);
        return 2;
    }
    if (argc > 2) {
        /* bash refuses to exit here, so a typo can't kill your session */
        fprintf(stderr, "%s: exit: too many arguments\n", MSH_NAME);
        return 1;
    }
    request_exit((int)code);
    return (int)(code & 0xFF);
}

/* ---------- export [-p] [name[=value]...] ---------- */

static int builtin_export(int argc, char *const argv[])
{
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "-p") == 0)) {
        vars_print_exported();
        return 0;
    }

    int status = 0;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *eq = strchr(arg, '=');
        size_t nlen = eq ? (size_t)(eq - arg) : strlen(arg);

        if (!vars_is_name(arg, nlen)) {
            fprintf(stderr, "%s: export: `%s': not a valid identifier\n",
                    MSH_NAME, arg);
            status = 1;                   /* keep going with the others */
            continue;
        }
        char *name = strndup(arg, nlen);
        if (!name || (eq && vars_set(name, eq + 1) < 0) ||
            vars_export(name) < 0) {
            fprintf(stderr, "%s: export: out of memory\n", MSH_NAME);
            free(name);
            return 1;
        }
        free(name);
    }
    return status;
}

/* ---------- unset [-v] name... ---------- */

static int builtin_unset(int argc, char *const argv[])
{
    int status = 0;
    int i = 1;
    bool only_vars = false;
    if (i < argc && strcmp(argv[i], "-v") == 0) {
        only_vars = true;
        i++;
    }
    for (; i < argc; i++) {
        if (!vars_is_name(argv[i], strlen(argv[i]))) {
            /* Plain `unset` may also remove a *function* of that name, and
             * function names are looser than variable names, so bash just
             * ignores it. Only `unset -v` insists on a valid variable. */
            if (!only_vars)
                continue;
            fprintf(stderr, "%s: unset: `%s': not a valid identifier\n",
                    MSH_NAME, argv[i]);
            status = 1;
            continue;
        }
        vars_unset(argv[i]);
    }
    return status;
}

/* ---------- dispatch table ---------- */

typedef int (*builtin_fn)(int, char *const[]);

static const struct {
    const char *name;
    builtin_fn fn;
} BUILTINS[] = {
    {"cd", builtin_cd},
    {"exit", builtin_exit},
    {"quit", builtin_exit},
    {"export", builtin_export},
    {"unset", builtin_unset},
};

#define N_BUILTINS (sizeof BUILTINS / sizeof BUILTINS[0])

bool is_builtin(const char *name)
{
    for (size_t i = 0; i < N_BUILTINS; i++)
        if (strcmp(name, BUILTINS[i].name) == 0)
            return true;
    return false;
}

int run_builtin(int argc, char *const argv[])
{
    for (size_t i = 0; i < N_BUILTINS; i++)
        if (strcmp(argv[0], BUILTINS[i].name) == 0)
            return BUILTINS[i].fn(argc, argv);
    return 127;
}
