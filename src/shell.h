/*
 * shell.h — state that belongs to the shell process as a whole.
 *
 * Kept in one struct (not scattered globals) so it is obvious what the
 * shell "remembers" between commands. Job table, variables, aliases and
 * history will be added here in later steps.
 */
#ifndef MSH_SHELL_H
#define MSH_SHELL_H

#include <stdbool.h>

#define MSH_NAME "msh"
#define MSH_PROMPT "abhinavTerminal> "

typedef struct {
    int last_status;   /* exit status of the most recent command: $? */
    bool interactive;  /* stdin is a terminal -> show prompt, etc.   */
    bool should_exit;  /* set by `exit`; main loop stops and cleans up */
    int exit_code;     /* status to exit with when should_exit is set  */
} shell_t;

extern shell_t g_shell;

#endif
