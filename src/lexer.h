/*
 * lexer.h — turns one input line into a flat array of tokens.
 *
 * Follows the token rules in POSIX "Shell Command Language" §2.3:
 *   - Quotes and backslashes are *recognised* here (so `"a b"` stays one
 *     word and `">"` is not an operator), but they are NOT removed.
 *     Quote removal happens later, in expand.c, after $-expansion —
 *     because expansion needs to know which parts were quoted.
 *   - Operators are matched greedily: `>>` beats `>`, `&&` beats `&`.
 *   - A word made only of digits that is immediately followed by `<` or
 *     `>` becomes an IO_NUMBER (the `2` in `2>file`).
 */
#ifndef MSH_LEXER_H
#define MSH_LEXER_H

#include <stddef.h>

typedef enum {
    TOK_WORD,       /* ls, "a b", 'x', foo\ bar   (quotes still inside) */
    TOK_IO_NUMBER,  /* the 2 in 2>err                                    */
    TOK_PIPE,       /* |  */
    TOK_OR_IF,      /* || */
    TOK_AMP,        /* &  */
    TOK_AND_IF,     /* && */
    TOK_SEMI,       /* ;  */
    TOK_LESS,       /* <  */
    TOK_GREAT,      /* >  */
    TOK_DGREAT,     /* >> */
    TOK_LESSAND,    /* <& */
    TOK_GREATAND,   /* >& */
    TOK_LPAREN,     /* (  (reserved for subshells later) */
    TOK_RPAREN,     /* )  */
    TOK_EOF         /* always the last token */
} token_type_t;

typedef struct {
    token_type_t type;
    char *text;     /* malloc'd; exact source spelling, quotes included */
} token_t;

typedef struct {
    token_t *items;
    size_t len;
    size_t cap;
} token_list_t;

typedef enum {
    LEX_OK = 0,
    LEX_ERR_UNTERMINATED_SQUOTE,
    LEX_ERR_UNTERMINATED_DQUOTE,
    LEX_ERR_UNTERMINATED_BRACE,
    LEX_ERR_NOMEM
} lex_status_t;

/* Tokenise `line` into `out` (which must be zero-initialised or freed).
 * On success the list ends with TOK_EOF. On error `out` is left empty. */
lex_status_t lex(const char *line, token_list_t *out);

void token_list_free(token_list_t *list);

/* `p` points at the '$' of "${". Return a pointer just past the matching
 * '}', honouring quotes and nested ${...} inside; NULL if there is none.
 * Shared by the lexer (to keep ${x:-a b} one word) and the expander. */
const char *lex_skip_braced(const char *p);

const char *token_type_name(token_type_t t);
const char *lex_status_message(lex_status_t s);

#endif
