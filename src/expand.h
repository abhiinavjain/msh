/*
 * expand.h — word expansion (POSIX §2.6).
 *
 * POSIX order:
 *   1. tilde expansion        ~  ~user  ~+  ~-
 *   2. parameter expansion    $x ${x} ${x:-w} ${#x} $? $$ ...
 *   3. command substitution   $(...)            (not yet)
 *   4. arithmetic             $((...))          (not yet)
 *   5. field splitting        on IFS, ONLY on unquoted results of 2-4
 *   6. pathname expansion     *.c               (not yet)
 *   7. quote removal
 *
 * Steps 1, 2 and 7 happen in one left-to-right pass that writes each
 * output byte together with a flag saying whether field splitting may
 * cut there. Step 5 then runs over that flagged buffer.
 */
#ifndef MSH_EXPAND_H
#define MSH_EXPAND_H

#include <stddef.h>

typedef struct {
    char **v;       /* NULL-terminated once at least one push happened */
    size_t n;
    size_t cap;
} strvec_t;

int strvec_push(strvec_t *sv, char *s);   /* takes ownership of s */
void strvec_free(strvec_t *sv);

/* Expand one WORD token into zero or more fields, appended to `out`.
 * Returns 0 on success, -1 on an expansion error (message already
 * printed; for ${x?msg} in a non-interactive shell, exit is requested). */
int expand_word(const char *word, strvec_t *out);

/* Expand the value part of NAME=value: tilde (also after each ':'),
 * parameters, quote removal — but NO field splitting.
 * Returns a malloc'd string, or NULL on error. */
char *expand_assignment_value(const char *raw);

/* Quote removal only (kept for unit tests and simple callers). */
char *remove_quotes(const char *word);

#endif
