/*
 * expand.c — see expand.h for the big picture.
 *
 * The central trick: while expanding we write each output byte TOGETHER
 * with a flag:
 *
 *   F_LIT    came from literal text or from inside quotes -> never split
 *   F_SPLIT  came from an UNQUOTED expansion ($x, ${x:-a b}) -> field
 *            splitting may cut here if the byte is in IFS
 *   F_MARK   not a byte at all: "a quote was here". It makes "" and
 *            "$empty" produce an empty argument instead of nothing.
 *
 * Quote characters themselves are never written, so quote removal is
 * free. After the pass, split_fields() walks the flagged buffer once.
 */
#include "expand.h"
#include "lexer.h"
#include "shell.h"
#include "vars.h"

#include <ctype.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

enum { F_LIT = 0, F_SPLIT = 1, F_MARK = 2 };

/* ---------- flagged buffer ---------- */

typedef struct {
    char *s;
    unsigned char *f;
    size_t len, cap;
} xbuf_t;

static int xb_put(xbuf_t *b, char c, unsigned char flag)
{
    if (b->len == b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 32;
        char *ns = realloc(b->s, ncap);
        if (!ns)
            return -1;
        b->s = ns;
        unsigned char *nf = realloc(b->f, ncap);
        if (!nf)
            return -1;
        b->f = nf;
        b->cap = ncap;
    }
    b->s[b->len] = c;
    b->f[b->len] = flag;
    b->len++;
    return 0;
}

static int xb_puts(xbuf_t *b, const char *s, unsigned char flag)
{
    for (; *s; s++)
        if (xb_put(b, *s, flag) < 0)
            return -1;
    return 0;
}

static void xb_free(xbuf_t *b)
{
    free(b->s);
    free(b->f);
}

/* The buffer's real bytes (markers dropped) as a C string. */
static char *xb_string(const xbuf_t *b)
{
    char *out = malloc(b->len + 1);
    if (!out)
        return NULL;
    size_t n = 0;
    for (size_t i = 0; i < b->len; i++)
        if (b->f[i] != F_MARK)
            out[n++] = b->s[i];
    out[n] = '\0';
    return out;
}

/* ---------- string vector ---------- */

int strvec_push(strvec_t *sv, char *s)
{
    if (sv->n + 2 > sv->cap) {             /* room for s and the NULL */
        size_t ncap = sv->cap ? sv->cap * 2 : 8;
        char **nv = realloc(sv->v, ncap * sizeof *nv);
        if (!nv)
            return -1;
        sv->v = nv;
        sv->cap = ncap;
    }
    sv->v[sv->n++] = s;
    sv->v[sv->n] = NULL;
    return 0;
}

void strvec_free(strvec_t *sv)
{
    for (size_t i = 0; i < sv->n; i++)
        free(sv->v[i]);
    free(sv->v);
    sv->v = NULL;
    sv->n = sv->cap = 0;
}

/* ---------- errors ---------- */

static int oom(void)
{
    fprintf(stderr, "%s: out of memory\n", MSH_NAME);
    return -1;
}

static int bad_subst(const char *from, const char *to, const char *why)
{
    fprintf(stderr, "%s: %.*s: %s\n", MSH_NAME, (int)(to - from), from, why);
    return -1;
}

/* ---------- parameters ---------- */

/* Value of a parameter as a malloc'd string, or NULL if it is unset. */
static char *get_param(const char *name)
{
    char num[32];

    if (strcmp(name, "?") == 0) {
        snprintf(num, sizeof num, "%d", g_shell.last_status);
        return strdup(num);
    }
    if (strcmp(name, "$") == 0) {
        snprintf(num, sizeof num, "%ld", (long)getpid());
        return strdup(num);
    }
    if (strcmp(name, "#") == 0)
        return strdup("0");               /* no positional params yet */
    if (strcmp(name, "0") == 0)
        return strdup(MSH_NAME);
    if (strcmp(name, "-") == 0)
        return strdup("");                /* no `set` options yet */
    if (isdigit((unsigned char)name[0]) || strchr("!@*", name[0]))
        return NULL;                      /* $1.., $!, $@, $*: later steps */

    const char *v = vars_get(name);
    return v ? strdup(v) : NULL;
}

/* Append an expansion result. Unquoted results are splittable; quoted
 * ones get a marker so that "$empty" still yields one (empty) field. */
static int append_value(xbuf_t *out, const char *val, bool quoted)
{
    if (quoted && xb_put(out, 0, F_MARK) < 0)
        return -1;
    return val ? xb_puts(out, val, quoted ? F_LIT : F_SPLIT) : 0;
}

/* ---------- tilde ---------- */

/* *pp points at '~'. Returns 1 if expanded (and advances *pp), 0 if the
 * text should stay literal, -1 on OOM. In an assignment value, a ':'
 * also ends the tilde prefix (PATH=~/bin:~/.local/bin). */
static int try_tilde(xbuf_t *out, const char **pp, const char *end,
                     bool assign)
{
    const char *p = *pp;
    const char *q = p + 1;
    while (q < end && *q != '/' && !(assign && *q == ':')) {
        if (strchr("'\"\\$`", *q))
            return 0;                     /* quoted/expanded prefix: literal */
        q++;
    }

    size_t ulen = (size_t)(q - p - 1);
    const char *dir = NULL;

    if (ulen == 0) {                      /* ~      -> $HOME */
        dir = vars_get("HOME");
        if (!dir) {
            struct passwd *pw = getpwuid(getuid());
            dir = pw ? pw->pw_dir : NULL;
        }
    } else if (ulen == 1 && p[1] == '+') { /* ~+   -> $PWD */
        dir = vars_get("PWD");
    } else if (ulen == 1 && p[1] == '-') { /* ~-   -> $OLDPWD */
        dir = vars_get("OLDPWD");
    } else {                              /* ~user -> user's home */
        char *user = strndup(p + 1, ulen);
        if (!user)
            return -1;
        struct passwd *pw = getpwnam(user);
        free(user);
        dir = pw ? pw->pw_dir : NULL;
    }

    if (!dir)
        return 0;                         /* unknown user etc.: keep "~x" */
    /* Tilde results are never field-split, even if HOME has spaces. */
    if (xb_put(out, 0, F_MARK) < 0 || xb_puts(out, dir, F_LIT) < 0)
        return -1;
    *pp = q;
    return 1;
}

/* ---------- the main pass ---------- */

typedef struct {
    bool outer_dq;   /* the whole segment sits inside "..." */
    bool lit_split;  /* unquoted literal text is splittable: true only
                        for the word in an unquoted ${x:-word} */
    bool tilde;      /* tilde expansion allowed at the start */
    bool assign;     /* assignment value: tilde also after ':' */
} ctx_t;

static int expand_segment(xbuf_t *out, const char *p, const char *end,
                          ctx_t ctx);

/* Expand `word` [from,to) into its own buffer (used by ${x=w}, ${x?w}). */
static char *expand_to_string(const char *from, const char *to, bool quoted)
{
    xbuf_t tmp = {0};
    ctx_t c = {.outer_dq = quoted};
    char *s = NULL;
    if (expand_segment(&tmp, from, to, c) == 0)
        s = xb_string(&tmp);
    xb_free(&tmp);
    return s;
}

/* *pp points at "${". Handles:
 *   ${x}  ${#x}  ${x-w} ${x:-w}  ${x=w} ${x:=w}  ${x+w} ${x:+w}  ${x?w} ${x:?w}
 * The ':' forms treat "set but empty" like "unset". */
static int expand_braced(xbuf_t *out, const char **pp, const char *end,
                         bool quoted)
{
    const char *start = *pp;
    const char *close = lex_skip_braced(start);
    if (!close || close > end)
        return bad_subst(start, end, "bad substitution");

    const char *in = start + 2;           /* first byte inside the braces */
    const char *inend = close - 1;        /* the '}' */

    bool length = false;
    if (*in == '#' && in + 1 < inend) {   /* ${#x}; but ${#} is "$#" */
        length = true;
        in++;
    }

    const char *n = in;
    if (isalpha((unsigned char)*n) || *n == '_') {
        while (n < inend && (isalnum((unsigned char)*n) || *n == '_'))
            n++;
    } else if (isdigit((unsigned char)*n)) {
        while (n < inend && isdigit((unsigned char)*n))
            n++;
    } else if (n < inend && strchr("?$#!@*-", *n)) {
        n++;
    }
    if (n == in)
        return bad_subst(start, close, "bad substitution");

    /* After the name: nothing, or [:]op word. */
    char op = 0;
    bool colon = false;
    const char *word = inend;
    if (n < inend) {
        if (length)
            return bad_subst(start, close, "bad substitution");
        const char *o = n;
        if (*o == ':') {
            colon = true;
            o++;
        }
        if (o < inend && strchr("-=+?", *o)) {
            op = *o;
            word = o + 1;
        } else if (strchr("%#/^,:", *n)) {
            /* Valid in bash (${x%.c}, ${x/a/b}, ${x:1:2}...) — later. */
            return bad_subst(start, close, "this form is not supported yet");
        } else {
            return bad_subst(start, close, "bad substitution");
        }
    }

    char *name = strndup(in, (size_t)(n - in));
    if (!name)
        return oom();
    char *val = get_param(name);
    bool set = val != NULL;
    bool use_word = colon ? (!set || val[0] == '\0') : !set;
    int rc = 0;
    ctx_t wctx = {.outer_dq = quoted, .lit_split = !quoted};

    switch (op) {
    case 0:
        if (length) {
            char num[32];
            snprintf(num, sizeof num, "%zu", val ? strlen(val) : (size_t)0);
            rc = append_value(out, num, quoted);
        } else {
            rc = append_value(out, val, quoted);
        }
        break;

    case '-':                             /* default value */
        if (use_word) {
            if (quoted && xb_put(out, 0, F_MARK) < 0)
                rc = -1;
            else
                rc = expand_segment(out, word, inend, wctx);
        } else {
            rc = append_value(out, val, quoted);
        }
        break;

    case '+':                             /* alternate value */
        if (quoted && xb_put(out, 0, F_MARK) < 0)
            rc = -1;
        else if (!use_word)
            rc = expand_segment(out, word, inend, wctx);
        break;

    case '=':                             /* assign default */
        if (use_word) {
            if (!vars_is_name(name, strlen(name))) {
                fprintf(stderr, "%s: $%s: cannot assign in this way\n",
                        MSH_NAME, name);
                rc = -1;
                break;
            }
            char *nv = expand_to_string(word, inend, true);
            if (!nv || vars_set(name, nv) < 0) {
                free(nv);
                rc = -1;
                break;
            }
            rc = append_value(out, nv, quoted);
            free(nv);
        } else {
            rc = append_value(out, val, quoted);
        }
        break;

    case '?':                             /* error if unset */
        if (use_word) {
            char *msg = word < inend ? expand_to_string(word, inend, true)
                                     : NULL;
            fprintf(stderr, "%s: %s: %s\n", MSH_NAME, name,
                    msg ? msg
                        : colon ? "parameter null or not set"
                                : "parameter not set");
            free(msg);
            /* POSIX: a non-interactive shell exits here. */
            if (!g_shell.interactive) {
                g_shell.should_exit = true;
                g_shell.exit_code = 1;
            }
            rc = -1;
        } else {
            rc = append_value(out, val, quoted);
        }
        break;
    }

    free(name);
    free(val);
    if (rc == 0)
        *pp = close;
    return rc;
}

/* *pp points at '$'. */
static int expand_dollar(xbuf_t *out, const char **pp, const char *end,
                         bool quoted)
{
    const char *s = *pp + 1;

    if (s < end && *s == '{')
        return expand_braced(out, pp, end, quoted);

    if (s < end && *s == '(') {
        fprintf(stderr, "%s: $(...) is not supported yet\n", MSH_NAME);
        return -1;
    }

    const char *e = s;
    if (s < end && (isalpha((unsigned char)*s) || *s == '_')) {
        while (e < end && (isalnum((unsigned char)*e) || *e == '_'))
            e++;
    } else if (s < end && (isdigit((unsigned char)*s) || strchr("?$#!@*-", *s))) {
        e = s + 1;                        /* $10 is ${1} followed by 0 */
    } else {
        /* "$" followed by nothing special is just a dollar sign. */
        if (xb_put(out, '$', F_LIT) < 0)
            return oom();
        *pp = s;
        return 0;
    }

    char *name = strndup(s, (size_t)(e - s));
    if (!name)
        return oom();
    char *val = get_param(name);
    int rc = append_value(out, val, quoted);
    free(name);
    free(val);
    *pp = e;
    return rc < 0 ? oom() : 0;
}

static int expand_segment(xbuf_t *out, const char *p, const char *end,
                          ctx_t ctx)
{
    bool in_dq = false;
    bool tilde_ok = ctx.tilde;

    while (p < end) {
        char c = *p;
        bool dq = in_dq || ctx.outer_dq;

        if (tilde_ok && c == '~' && !dq) {
            int r = try_tilde(out, &p, end, ctx.assign);
            if (r < 0)
                return oom();
            tilde_ok = false;
            if (r > 0)
                continue;
        }
        tilde_ok = false;

        if (c == '\'' && !dq) {
            const char *close = memchr(p + 1, '\'', (size_t)(end - p - 1));
            if (!close)
                close = end;              /* can't happen: lexer checked */
            if (xb_put(out, 0, F_MARK) < 0)
                return oom();
            for (p++; p < close; p++)
                if (xb_put(out, *p, F_LIT) < 0)
                    return oom();
            p = close < end ? close + 1 : end;
        } else if (c == '"') {
            in_dq = !in_dq;
            if (xb_put(out, 0, F_MARK) < 0)
                return oom();
            p++;
        } else if (c == '\\') {
            if (p + 1 >= end) {
                if (xb_put(out, '\\', F_LIT) < 0)
                    return oom();
                p++;
            } else if (!dq || strchr("$`\"\\", p[1])) {
                if (xb_put(out, p[1], F_LIT) < 0)   /* escaped: literal */
                    return oom();
                p += 2;
            } else {
                if (xb_put(out, '\\', F_LIT) < 0)   /* "\n" keeps the \ */
                    return oom();
                p++;
            }
        } else if (c == '$') {
            if (expand_dollar(out, &p, end, dq) < 0)
                return -1;
        } else if (c == '`') {
            fprintf(stderr, "%s: `...` is not supported yet\n", MSH_NAME);
            return -1;
        } else {
            unsigned char f = (ctx.lit_split && !dq) ? F_SPLIT : F_LIT;
            if (xb_put(out, c, f) < 0)
                return oom();
            if (c == ':' && ctx.assign && !dq)
                tilde_ok = true;
            p++;
        }
    }
    return 0;
}

/* ---------- field splitting (POSIX §2.6.5) ---------- */

static bool is_ifs_ws(char c) { return c == ' ' || c == '\t' || c == '\n'; }

static int split_fields(const xbuf_t *b, strvec_t *out)
{
    const char *ifs = vars_get("IFS");
    if (!ifs)
        ifs = " \t\n";                    /* unset IFS means the default */

    /* A byte is a separator only if it is splittable AND in IFS. */
#define IS_SEP(i) (b->f[i] == F_SPLIT && b->s[i] && strchr(ifs, b->s[i]))
#define IS_SEP_WS(i) (IS_SEP(i) && is_ifs_ws(b->s[i]))

    char *field = malloc(b->len + 1);
    if (!field)
        return oom();
    size_t flen = 0;
    bool started = false;   /* current field exists (even if empty) */
    size_t i = 0;

    while (i < b->len) {
        if (b->f[i] == F_MARK) {
            started = true;
            i++;
        } else if (IS_SEP(i)) {
            /* One separator = optional IFS-whitespace, at most one
             * non-whitespace IFS char, optional IFS-whitespace.
             * Whitespace alone separates but never creates an empty
             * field; a non-whitespace char (like ':') always delimits. */
            bool hard = false;
            while (i < b->len && IS_SEP_WS(i))
                i++;
            if (i < b->len && IS_SEP(i) && !is_ifs_ws(b->s[i])) {
                hard = true;
                i++;
                while (i < b->len && IS_SEP_WS(i))
                    i++;
            }
            if (started || hard) {
                field[flen] = '\0';
                char *f = strdup(field);
                if (!f || strvec_push(out, f) < 0) {
                    free(f);
                    free(field);
                    return oom();
                }
            }
            flen = 0;
            started = false;
        } else {
            field[flen++] = b->s[i++];
            started = true;
        }
    }
    if (started) {
        field[flen] = '\0';
        char *f = strdup(field);
        if (!f || strvec_push(out, f) < 0) {
            free(f);
            free(field);
            return oom();
        }
    }
    free(field);
    return 0;
#undef IS_SEP
#undef IS_SEP_WS
}

/* ---------- public API ---------- */

int expand_word(const char *word, strvec_t *out)
{
    xbuf_t b = {0};
    ctx_t ctx = {.tilde = true};
    int rc = expand_segment(&b, word, word + strlen(word), ctx);
    if (rc == 0)
        rc = split_fields(&b, out);
    xb_free(&b);
    return rc;
}

char *expand_assignment_value(const char *raw)
{
    xbuf_t b = {0};
    ctx_t ctx = {.tilde = true, .assign = true};
    char *s = NULL;
    if (expand_segment(&b, raw, raw + strlen(raw), ctx) == 0) {
        s = xb_string(&b);
        if (!s)
            oom();
    }
    xb_free(&b);
    return s;
}

char *remove_quotes(const char *word)
{
    /* Output is never longer than input, so one allocation suffices. */
    char *out = malloc(strlen(word) + 1);
    if (!out)
        return NULL;

    char *o = out;
    const char *p = word;

    while (*p) {
        if (*p == '\'') {
            p++;
            while (*p && *p != '\'')
                *o++ = *p++;
            if (*p)
                p++;
        } else if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1] && strchr("$`\"\\", p[1]))
                    p++;
                *o++ = *p++;
            }
            if (*p)
                p++;
        } else if (*p == '\\' && p[1]) {
            p++;
            *o++ = *p++;
        } else {
            *o++ = *p++;
        }
    }
    *o = '\0';
    return out;
}
