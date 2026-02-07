/*
 * token.h — Token estimation (~4 chars/token)
 */
#ifndef LP_TOKEN_H
#define LP_TOKEN_H

#include <stddef.h>

/* Estimate tokens for a string. ~4 chars per token, adjusted for whitespace. */
size_t lp_estimate_tokens(const char *text, size_t len);

/* Batch: estimate total tokens for an array of lines. */
size_t lp_estimate_tokens_lines(const char **lines, size_t count);

#endif /* LP_TOKEN_H */
