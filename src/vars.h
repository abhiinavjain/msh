/*
 * vars.h — the shell's variable table.
 *
 * A shell has TWO kinds of variables, and the difference matters:
 *   - shell variables  `x=5`         visible to the shell only
 *   - exported ones    `export x=5`  also copied into every child's
 *                                    environment (what getenv() sees)
 *
 * We keep both in one table with an `exported` flag, and build a fresh
 * envp array for each child. We never modify our own process `environ`
 * after startup, so the table is the single source of truth.
 */
#ifndef MSH_VARS_H
#define MSH_VARS_H

#include <stdbool.h>
#include <stddef.h>

/* Import the process environment (all exported) and set PWD and IFS. */
void vars_init(char **envp);
void vars_free_all(void);

/* NULL if unset. The pointer is valid until the variable next changes. */
const char *vars_get(const char *name);

/* Set a variable. If it is new it starts unexported; if it already
 * exists it keeps its exported flag. Returns 0, or -1 on OOM. */
int vars_set(const char *name, const char *value);

/* Mark exported (creating it with no value if needed, like `export x`). */
int vars_export(const char *name);

void vars_unset(const char *name);

/* `export -p` / bare `export`: print exported vars, sorted, bash style. */
void vars_print_exported(void);

/* POSIX name: [A-Za-z_][A-Za-z0-9_]*  — checks the first `len` bytes. */
bool vars_is_name(const char *s, size_t len);

/* An "override" is a NAME=value placed in front of a command
 * (`FOO=1 cmd`): it goes into that command's environment only. */
typedef struct {
    char *name;
    char *value;
} assign_t;

/* Build a NULL-terminated "NAME=value" array: every exported variable
 * that has a value, with `overrides` replacing or adding entries.
 * Free with vars_free_envp(). Returns NULL on OOM. */
char **vars_build_envp(const assign_t *overrides, size_t n_overrides);
void vars_free_envp(char **envp);

#endif
