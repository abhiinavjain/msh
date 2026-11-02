/*
 * main.c — the read–eval loop.
 *
 *   read line -> lex -> (parse: step 3) -> expand -> execute -> repeat
 *
 * There is still no parser: a line is one "simple command", i.e.
 *
 *     [NAME=value ...] [word ...]
 *
 * Operators are rejected with a clear message until step 3.
 */
#include "builtins.h"
#include "exec.h"
#include "expand.h"
#include "lexer.h"
#include "shell.h"
#include "vars.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

/* POSIX: a word is an assignment if it looks like NAME=... where NAME is
 * a valid, *unquoted* name. ("x"=1 is a command called x=1, not an
 * assignment — vars_is_name rejects the quote characters.) */
static bool is_assignment(const char *w)
{
    const char *eq = strchr(w, '=');
    return eq && vars_is_name(w, (size_t)(eq - w));
}

static void free_assigns(assign_t *as, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        free(as[i].name);
        free(as[i].value);
    }
    free(as);
}

/* Undo temporary assignments, newest first, so that `A=1 A=2 cmd`
 * restores A to its value from before the command. */
static void restore_vars(const assign_t *as, char **saved, size_t n)
{
    for (size_t i = n; i-- > 0;) {
        if (saved[i])
            vars_set(as[i].name, saved[i]);
        else
            vars_unset(as[i].name);
        free(saved[i]);
    }
}

static void run_simple_command(const token_list_t *toks)
{
    size_t ntok = toks->len - 1;           /* drop the EOF token */

    /* 1. Leading NAME=value words are assignments. */
    size_t nassign = 0;
    while (nassign < ntok && is_assignment(toks->items[nassign].text))
        nassign++;

    /* 2. Expand the command words FIRST (POSIX order), so in `x=1 echo $x`
     *    the $x still sees the old x. Fields can be more or fewer than
     *    the words (`$empty` vanishes, `$list` may split). */
    strvec_t argv = {0};
    for (size_t i = nassign; i < ntok; i++) {
        if (expand_word(toks->items[i].text, &argv) < 0) {
            strvec_free(&argv);
            g_shell.last_status = 1;
            return;
        }
    }

    /* 3. Expand and apply assignments left to right, so each one sees
     *    the previous (`x=1 y=$x` gives y=1). If a command follows, remember
     *    the old values: the assignments must not outlive it. */
    bool has_cmd = argv.n > 0;
    assign_t *as = calloc(nassign ? nassign : 1, sizeof *as);
    char **saved = calloc(nassign ? nassign : 1, sizeof *saved);
    size_t done = 0;
    int status = 0;
    if (!as || !saved) {
        fprintf(stderr, "%s: out of memory\n", MSH_NAME);
        status = 1;
        goto out;
    }
    for (; done < nassign; done++) {
        const char *w = toks->items[done].text;
        const char *eq = strchr(w, '=');
        as[done].name = strndup(w, (size_t)(eq - w));
        as[done].value = expand_assignment_value(eq + 1);
        if (!as[done].name || !as[done].value) {
            free(as[done].name);
            free(as[done].value);
            as[done].name = as[done].value = NULL;
            status = 1;
            goto out;
        }
        if (has_cmd) {
            const char *old = vars_get(as[done].name);
            saved[done] = old ? strdup(old) : NULL;
        }
        vars_set(as[done].name, as[done].value);
    }

    /* 4. Run. Where the assignments end up depends on what ran:
     *      no command  -> they stay: they were the whole point
     *      builtin     -> visible while it runs, then restored
     *      external    -> copied into the child's environment, then restored */
    if (!has_cmd) {
        status = 0;
    } else if (is_builtin(argv.v[0])) {
        status = run_builtin((int)argv.n, argv.v);
    } else {
        char **envp = vars_build_envp(as, nassign);
        if (!envp) {
            fprintf(stderr, "%s: out of memory\n", MSH_NAME);
            status = 1;
        } else {
            status = exec_external(argv.v, envp);
            vars_free_envp(envp);
        }
    }

out:
    if (has_cmd && saved)
        restore_vars(as, saved, done);
    free(saved);
    if (as)
        free_assigns(as, nassign);
    strvec_free(&argv);
    g_shell.last_status = status;
}

static void run_line(const char *line)
{
    token_list_t toks;
    lex_status_t st = lex(line, &toks);
    if (st != LEX_OK) {
        fprintf(stderr, "%s: syntax error: %s\n", MSH_NAME,
                lex_status_message(st));
        g_shell.last_status = 2;          /* bash uses 2 for syntax errors */
        return;
    }

    for (size_t i = 0; toks.items[i].type != TOK_EOF; i++) {
        if (toks.items[i].type != TOK_WORD) {
            fprintf(stderr, "%s: `%s': not supported yet\n", MSH_NAME,
                    toks.items[i].text);
            g_shell.last_status = 2;
            token_list_free(&toks);
            return;
        }
    }

    if (toks.len > 1)                     /* blank line: $? unchanged */
        run_simple_command(&toks);
    token_list_free(&toks);
}

int main(void)
{
    g_shell.interactive = isatty(STDIN_FILENO);
    vars_init(environ);

    char *line = NULL;   /* getline() grows this as needed: no 1024 limit */
    size_t cap = 0;

    while (!g_shell.should_exit) {
        if (g_shell.interactive) {
            fputs(MSH_PROMPT, stdout);
            fflush(stdout);
        }

        ssize_t n = getline(&line, &cap, stdin);
        if (n < 0) {                       /* EOF (Ctrl-D) or read error */
            if (g_shell.interactive)
                fputs("exit\n", stderr);
            g_shell.exit_code = g_shell.last_status;
            break;
        }
        if (n > 0 && line[n - 1] == '\n')
            line[n - 1] = '\0';

        run_line(line);
    }

    free(line);
    vars_free_all();
    return g_shell.exit_code;
}
