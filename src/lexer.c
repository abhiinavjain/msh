/*
 * lexer.c — see lexer.h for the rules we follow.
 *
 * Shape of the code: one loop over the input. At each position we are
 * in exactly one of three situations:
 *   1. a blank        -> skip it
 *   2. an operator    -> emit the longest operator that matches
 *   3. anything else  -> read one WORD, honouring quotes, until an
 *                        *unquoted* blank or operator character
 */
#include "lexer.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* ---------- small growable string buffer ---------- */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} sbuf_t;

static bool sbuf_putc(sbuf_t *s, char c)
{
    if (s->len + 1 >= s->cap) {               /* +1 keeps room for '\0' */
        size_t ncap = s->cap ? s->cap * 2 : 16;
        char *nb = realloc(s->buf, ncap);
        if (!nb)
            return false;
        s->buf = nb;
        s->cap = ncap;
    }
    s->buf[s->len++] = c;
    s->buf[s->len] = '\0';
    return true;
}

/* ---------- token list ---------- */

static bool list_push(token_list_t *l, token_type_t type, char *text)
{
    if (l->len == l->cap) {
        size_t ncap = l->cap ? l->cap * 2 : 8;
        token_t *ni = realloc(l->items, ncap * sizeof *ni);
        if (!ni)
            return false;
        l->items = ni;
        l->cap = ncap;
    }
    l->items[l->len].type = type;
    l->items[l->len].text = text;
    l->len++;
    return true;
}

void token_list_free(token_list_t *l)
{
    for (size_t i = 0; i < l->len; i++)
        free(l->items[i].text);
    free(l->items);
    l->items = NULL;
    l->len = l->cap = 0;
}

/* ---------- character classes ---------- */

static bool is_blank(char c) { return c == ' ' || c == '\t' || c == '\n'; }

/* Characters that can START an operator. An unquoted one ends a word. */
static bool is_op_start(char c)
{
    return c == '|' || c == '&' || c == ';' || c == '<' || c == '>' ||
           c == '(' || c == ')';
}

/* Operator table. Longer spellings MUST come before their prefixes,
 * because we take the first match (this is the "longest match" rule). */
static const struct {
    const char *spelling;
    token_type_t type;
} OPS[] = {
    {"&&", TOK_AND_IF}, {"||", TOK_OR_IF}, {">>", TOK_DGREAT},
    {"<&", TOK_LESSAND}, {">&", TOK_GREATAND},
    {"|", TOK_PIPE}, {"&", TOK_AMP}, {";", TOK_SEMI},
    {"<", TOK_LESS}, {">", TOK_GREAT}, {"(", TOK_LPAREN}, {")", TOK_RPAREN},
};

/* ---------- the lexer ---------- */

/* Read one WORD starting at *pp. Advances *pp past it. */
static lex_status_t read_word(const char **pp, sbuf_t *w)
{
    const char *p = *pp;

    while (*p && !is_blank(*p) && !is_op_start(*p)) {
        if (*p == '\'') {
            /* Single quotes: everything is literal until the next '.
             * There is no escape inside single quotes, not even \'. */
            const char *close = strchr(p + 1, '\'');
            if (!close)
                return LEX_ERR_UNTERMINATED_SQUOTE;
            for (; p <= close; p++)
                if (!sbuf_putc(w, *p))
                    return LEX_ERR_NOMEM;
        } else if (*p == '"') {
            /* Double quotes: scan to the matching ", skipping over
             * backslash-escaped characters so that \" doesn't close. */
            if (!sbuf_putc(w, *p++))
                return LEX_ERR_NOMEM;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) {
                    if (!sbuf_putc(w, *p++))
                        return LEX_ERR_NOMEM;
                }
                if (!sbuf_putc(w, *p++))
                    return LEX_ERR_NOMEM;
            }
            if (*p != '"')
                return LEX_ERR_UNTERMINATED_DQUOTE;
            if (!sbuf_putc(w, *p++))
                return LEX_ERR_NOMEM;
        } else if (*p == '\\') {
            if (p[1] == '\n') {
                p += 2;          /* backslash-newline: line continuation,
                                    removed entirely (POSIX §2.2.1) */
            } else {
                /* Keep "\x" together so x can't end the word or start
                 * an operator. A lone trailing '\' is kept as-is. */
                if (!sbuf_putc(w, *p++))
                    return LEX_ERR_NOMEM;
                if (*p && !sbuf_putc(w, *p++))
                    return LEX_ERR_NOMEM;
            }
        } else {
            if (!sbuf_putc(w, *p++))
                return LEX_ERR_NOMEM;
        }
    }
    *pp = p;
    return LEX_OK;
}

static bool all_digits(const char *s)
{
    if (!*s)
        return false;
    for (; *s; s++)
        if (!isdigit((unsigned char)*s))
            return false;
    return true;
}

lex_status_t lex(const char *line, token_list_t *out)
{
    const char *p = line;
    lex_status_t st = LEX_OK;

    out->items = NULL;
    out->len = out->cap = 0;

    while (*p) {
        if (is_blank(*p)) {
            p++;
            continue;
        }

        /* A '#' that starts a word begins a comment to end of line.
         * (A '#' in the middle of a word, like a#b, is ordinary.) */
        if (*p == '#') {
            break;
        }

        if (is_op_start(*p)) {
            size_t i;
            for (i = 0; i < sizeof OPS / sizeof OPS[0]; i++) {
                size_t n = strlen(OPS[i].spelling);
                if (strncmp(p, OPS[i].spelling, n) == 0)
                    break;
            }
            /* is_op_start guarantees some single-char op matches. */
            char *text = strdup(OPS[i].spelling);
            if (!text || !list_push(out, OPS[i].type, text)) {
                free(text);
                st = LEX_ERR_NOMEM;
                goto fail;
            }
            p += strlen(OPS[i].spelling);
            continue;
        }

        sbuf_t w = {0};
        st = read_word(&p, &w);
        if (st != LEX_OK) {
            free(w.buf);
            goto fail;
        }
        /* "2>" -> IO_NUMBER "2" then GREAT. "2 >" stays WORD "2". */
        token_type_t type =
            (all_digits(w.buf) && (*p == '<' || *p == '>')) ? TOK_IO_NUMBER
                                                             : TOK_WORD;
        if (!list_push(out, type, w.buf)) {
            free(w.buf);
            st = LEX_ERR_NOMEM;
            goto fail;
        }
    }

    {
        char *eof = strdup("");
        if (!eof || !list_push(out, TOK_EOF, eof)) {
            free(eof);
            st = LEX_ERR_NOMEM;
            goto fail;
        }
    }
    return LEX_OK;

fail:
    token_list_free(out);
    return st;
}

const char *token_type_name(token_type_t t)
{
    switch (t) {
    case TOK_WORD:      return "WORD";
    case TOK_IO_NUMBER: return "IO_NUMBER";
    case TOK_PIPE:      return "PIPE";
    case TOK_OR_IF:     return "OR_IF";
    case TOK_AMP:       return "AMP";
    case TOK_AND_IF:    return "AND_IF";
    case TOK_SEMI:      return "SEMI";
    case TOK_LESS:      return "LESS";
    case TOK_GREAT:     return "GREAT";
    case TOK_DGREAT:    return "DGREAT";
    case TOK_LESSAND:   return "LESSAND";
    case TOK_GREATAND:  return "GREATAND";
    case TOK_LPAREN:    return "LPAREN";
    case TOK_RPAREN:    return "RPAREN";
    case TOK_EOF:       return "EOF";
    }
    return "?";
}

const char *lex_status_message(lex_status_t s)
{
    switch (s) {
    case LEX_OK:                      return "ok";
    case LEX_ERR_UNTERMINATED_SQUOTE: return "unexpected EOF while looking for matching `''";
    case LEX_ERR_UNTERMINATED_DQUOTE: return "unexpected EOF while looking for matching `\"'";
    case LEX_ERR_NOMEM:               return "out of memory";
    }
    return "?";
}
