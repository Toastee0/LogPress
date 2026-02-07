/*
 * token.c — Token estimation
 */
#include "token.h"
#include <ctype.h>

size_t lp_estimate_tokens(const char *text, size_t len) {
    if (!text || len == 0) return 0;
    /* Count non-whitespace characters for a better estimate */
    size_t non_ws = 0;
    for (size_t i = 0; i < len; i++) {
        if (!isspace((unsigned char)text[i])) non_ws++;
    }
    /* Base: ~4 chars/token. Whitespace-heavy lines get discounted. */
    size_t base = (len + 3) / 4;
    size_t content = (non_ws + 3) / 4;
    /* Blend: 70% content-based, 30% raw length */
    return (content * 7 + base * 3 + 5) / 10;
}

size_t lp_estimate_tokens_lines(const char **lines, size_t count) {
    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
        if (lines[i]) {
            size_t len = 0;
            const char *p = lines[i];
            while (*p) { len++; p++; }
            total += lp_estimate_tokens(lines[i], len);
        }
        total += 1; /* newline token */
    }
    return total;
}
