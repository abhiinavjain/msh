#ifndef MSH_EXEC_H
#define MSH_EXEC_H

/* Run an external program: fork, execvp in the child, wait in the parent.
 * argv must be NULL-terminated with argv[0] != NULL. `envp` is the
 * complete environment the child gets (see vars_build_envp).
 * Returns a shell-style exit status:
 *   0..255     the program's own exit code
 *   126        found but could not be executed (e.g. permission denied)
 *   127        not found
 *   128 + N    killed by signal N (e.g. 130 for SIGINT, 137 for SIGKILL)
 */
int exec_external(char *const argv[], char **envp);

#endif
