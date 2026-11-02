/*
 * Unit tests for the lexer and quote removal.
 *
 * Each lexer case gives an input line and the expected token stream
 * written as "TYPE:text TYPE:text ..." (EOF omitted). Easy to read,
 * easy to add new cases to.
 */
#include "expand.h"
#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static int passes = 0;

static void render(const token_list_t *t, char *buf, size_t n)
{
    buf[0] = '\0';
    for (size_t i = 0; i < t->len && t->items[i].type != TOK_EOF; i++) {
        if (i)
            strncat(buf, " ", n - strlen(buf) - 1);
        strncat(buf, token_type_name(t->items[i].type), n - strlen(buf) - 1);
        strncat(buf, ":", n - strlen(buf) - 1);
        strncat(buf, t->items[i].text, n - strlen(buf) - 1);
    }
}

static void expect_tokens(const char *input, const char *expected)
{
    token_list_t t;
    lex_status_t st = lex(input, &t);
    if (st != LEX_OK) {
        printf("FAIL lex(%s): error %s\n", input, lex_status_message(st));
        failures++;
        return;
    }
    char got[1024];
    render(&t, got, sizeof got);
    if (strcmp(got, expected) != 0) {
        printf("FAIL lex(%s)\n  want: %s\n  got:  %s\n", input, expected, got);
        failures++;
    } else {
        passes++;
    }
    token_list_free(&t);
}

static void expect_lex_error(const char *input, lex_status_t want)
{
    token_list_t t;
    lex_status_t st = lex(input, &t);
    if (st != want) {
        printf("FAIL lex(%s): want error %d, got %d\n", input, want, st);
        failures++;
        if (st == LEX_OK)
            token_list_free(&t);
    } else {
        passes++;
    }
}

static void expect_unquoted(const char *word, const char *want)
{
    char *got = remove_quotes(word);
    if (strcmp(got, want) != 0) {
        printf("FAIL remove_quotes(%s)\n  want: [%s]\n  got:  [%s]\n", word,
               want, got);
        failures++;
    } else {
        passes++;
    }
    free(got);
}

int main(void)
{
    /* --- words and blanks --- */
    expect_tokens("", "");
    expect_tokens("   \t  ", "");
    expect_tokens("ls", "WORD:ls");
    expect_tokens("  ls   -l\t/tmp  ", "WORD:ls WORD:-l WORD:/tmp");

    /* --- operators, longest match --- */
    expect_tokens("a|b", "WORD:a PIPE:| WORD:b");
    expect_tokens("a||b", "WORD:a OR_IF:|| WORD:b");
    expect_tokens("a&&b&", "WORD:a AND_IF:&& WORD:b AMP:&");
    expect_tokens("a;b", "WORD:a SEMI:; WORD:b");
    expect_tokens("a>f>>g<h", "WORD:a GREAT:> WORD:f DGREAT:>> WORD:g LESS:< WORD:h");
    expect_tokens("a|||b", "WORD:a OR_IF:|| PIPE:| WORD:b");
    expect_tokens("(a)", "LPAREN:( WORD:a RPAREN:)");

    /* --- IO_NUMBER: digits glued to < or > --- */
    expect_tokens("cmd 2>err", "WORD:cmd IO_NUMBER:2 GREAT:> WORD:err");
    expect_tokens("cmd 2>&1", "WORD:cmd IO_NUMBER:2 GREATAND:>& WORD:1");
    expect_tokens("cmd 2 >err", "WORD:cmd WORD:2 GREAT:> WORD:err");
    expect_tokens("cmd a2>err", "WORD:cmd WORD:a2 GREAT:> WORD:err");
    expect_tokens("echo 12", "WORD:echo WORD:12");

    /* --- quoting keeps operators and blanks inside one word --- */
    expect_tokens("echo 'a | b'", "WORD:echo WORD:'a | b'");
    expect_tokens("echo \"a ; b\"", "WORD:echo WORD:\"a ; b\"");
    expect_tokens("echo a\\ b", "WORD:echo WORD:a\\ b");
    expect_tokens("echo a\\|b", "WORD:echo WORD:a\\|b");
    expect_tokens("echo \"x\\\"y\"", "WORD:echo WORD:\"x\\\"y\"");
    expect_tokens("echo pre'mid'\"post\"", "WORD:echo WORD:pre'mid'\"post\"");
    expect_tokens("echo ''", "WORD:echo WORD:''");
    expect_tokens("echo a\\\nb", "WORD:echo WORD:ab");   /* continuation */

    /* --- ${...} is one unit inside a word --- */
    expect_tokens("echo ${x:-a b}", "WORD:echo WORD:${x:-a b}");
    expect_tokens("echo ${x:-a|b;c}d", "WORD:echo WORD:${x:-a|b;c}d");
    expect_tokens("echo \"${x:-\"a b\"}\"", "WORD:echo WORD:\"${x:-\"a b\"}\"");
    expect_tokens("echo ${a:-${b:-c d}}", "WORD:echo WORD:${a:-${b:-c d}}");
    expect_tokens("echo $x|y", "WORD:echo WORD:$x PIPE:| WORD:y");

    /* --- comments --- */
    expect_tokens("ls # comment | here", "WORD:ls");
    expect_tokens("echo a#b", "WORD:echo WORD:a#b");
    expect_tokens("echo '#not'", "WORD:echo WORD:'#not'");

    /* --- errors --- */
    expect_lex_error("echo 'oops", LEX_ERR_UNTERMINATED_SQUOTE);
    expect_lex_error("echo \"oops", LEX_ERR_UNTERMINATED_DQUOTE);
    expect_lex_error("echo \"oops\\\"", LEX_ERR_UNTERMINATED_DQUOTE);
    expect_lex_error("echo ${x", LEX_ERR_UNTERMINATED_BRACE);
    expect_lex_error("echo \"${x\"", LEX_ERR_UNTERMINATED_BRACE);
    expect_lex_error("echo ${x:-'}", LEX_ERR_UNTERMINATED_BRACE);

    /* --- quote removal --- */
    expect_unquoted("abc", "abc");
    expect_unquoted("'a b'", "a b");
    expect_unquoted("\"a b\"", "a b");
    expect_unquoted("a\\ b", "a b");
    expect_unquoted("'a\\b'", "a\\b");           /* no escapes in '' */
    expect_unquoted("\"a\\\"b\"", "a\"b");       /* \" inside "" */
    expect_unquoted("\"a\\nb\"", "a\\nb");       /* \n is NOT special in "" */
    expect_unquoted("\"a\\\\b\"", "a\\b");       /* \\ -> \ in "" */
    expect_unquoted("pre'mid'\"post\"", "premidpost");
    expect_unquoted("''", "");
    expect_unquoted("\"'\"", "'");
    expect_unquoted("'\"'", "\"");

    printf("%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
