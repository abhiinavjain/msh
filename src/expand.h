/*
 * expand.h — word expansion.
 *
 * POSIX order (§2.6): tilde -> parameter ($VAR) -> command substitution
 * -> arithmetic -> field splitting -> globbing -> QUOTE REMOVAL (last).
 *
 * Step 1 implements only quote removal. $VAR, $? and ~ arrive in step 2;
 * they will slot in *before* remove_quotes() without changing callers.
 */
#ifndef MSH_EXPAND_H
#define MSH_EXPAND_H

/* Return a malloc'd copy of `word` with quoting removed:
 *   'abc'  -> abc         (everything literal)
 *   "a\"b" -> a"b         (\ only escapes $ ` " \ inside "")
 *   a\ b   -> a b         (outside quotes, \ escapes any char)
 * Returns NULL on allocation failure. */
char *remove_quotes(const char *word);

#endif
