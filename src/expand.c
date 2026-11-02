#include "expand.h"

#include <stdlib.h>
#include <string.h>

char *remove_quotes(const char *word)
{
    /* Output is never longer than input, so one allocation suffices. */
    char *out = malloc(strlen(word) + 1);
    if (!out)
        return NULL;

    char *o = out;
    const char *p = word;

    while (*p) {
        if (*p == '\'') {
            p++;
            while (*p && *p != '\'')
                *o++ = *p++;
            if (*p)
                p++;                    /* skip closing ' */
        } else if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                /* Inside "", backslash is special only before these. */
                if (*p == '\\' && p[1] && strchr("$`\"\\", p[1]))
                    p++;
                *o++ = *p++;
            }
            if (*p)
                p++;                    /* skip closing " */
        } else if (*p == '\\' && p[1]) {
            p++;                        /* drop the backslash ... */
            *o++ = *p++;                /* ... keep what it escaped */
        } else {
            *o++ = *p++;
        }
    }
    *o = '\0';
    return out;
}
