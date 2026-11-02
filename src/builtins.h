#ifndef MSH_BUILTINS_H
#define MSH_BUILTINS_H

#include <stdbool.h>

/* Builtins run inside the shell process itself, never in a child,
 * because they change the shell's own state (cwd, exit, variables). */

bool is_builtin(const char *name);

/* Run builtin argv[0]. argc counts argv entries (excluding the NULL).
 * Returns its exit status. `exit` does not return. */
int run_builtin(int argc, char *const argv[]);

#endif
