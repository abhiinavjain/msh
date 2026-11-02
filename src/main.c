/*
 * main.c — the read–eval loop.
 *
 *   read line -> lex -> (parse: step 3) -> expand -> execute -> repeat
 *
 * For step 1 there is no parser yet: we accept only plain words, turn
 * them into argv, and reject operators with a clear message.
 */
#include "builtins.h"
#include "exec.h"
#include "expand.h"
#include "lexer.h"
#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Build a NULL-terminated argv from the WORD tokens, with quotes removed.
 * Returns NULL (and prints why) if the line uses something we don't
 * support yet, or on allocation failure. *argc_out gets the count. */
static char **build_argv(const token_list_t *toks, int *argc_out)
{
    /* The last token is EOF, so there are at most len-1 words. */
    char **argv = calloc(toks->len, sizeof *argv);
    if (!argv) {
        fprintf(stderr, "%s: out of memory\n", MSH_NAME);
        return NULL;
    }

    int argc = 0;
    for (size_t i = 0; toks->items[i].type != TOK_EOF; i++) {
        const token_t *t = &toks->items[i];
        if (t->type != TOK_WORD) {
            fprintf(stderr, "%s: `%s': not supported yet\n", MSH_NAME,
                    t->text);
            goto fail;
        }
        argv[argc] = remove_quotes(t->text);
        if (!argv[argc]) {
            fprintf(stderr, "%s: out of memory\n", MSH_NAME);
            goto fail;
        }
        argc++;
    }
    argv[argc] = NULL;
    *argc_out = argc;
    return argv;

fail:
    for (int j = 0; j < argc; j++)
        free(argv[j]);
    free(argv);
    return NULL;
}

static void free_argv(char **argv)
{
    for (char **a = argv; *a; a++)
        free(*a);
    free(argv);
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

    int argc = 0;
    char **argv = build_argv(&toks, &argc);
    token_list_free(&toks);
    if (!argv) {
        g_shell.last_status = 2;
        return;
    }

    if (argc > 0) {
        g_shell.last_status = is_builtin(argv[0]) ? run_builtin(argc, argv)
                                                  : exec_external(argv);
    }
    free_argv(argv);
}

int main(void)
{
    g_shell.interactive = isatty(STDIN_FILENO);

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
    return g_shell.exit_code;
}
