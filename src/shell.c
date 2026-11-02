#include "shell.h"

/* The one definition of the shell's global state. Lives in its own file
 * (not main.c) so unit tests can link every module except main.c. */
shell_t g_shell;
