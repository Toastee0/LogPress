/*
 * budget.h — Token budget packing (greedy knapsack)
 */
#ifndef LP_BUDGET_H
#define LP_BUDGET_H

#include "segment.h"
#include <stddef.h>
#include <stdbool.h>

/* Result of budget packing */
typedef struct {
    size_t  *indices;        /* Indices into original segment array (in output order) */
    size_t   count;          /* Number of packed segments */
    size_t   total_tokens;   /* Total tokens consumed */
    size_t   budget_tokens;  /* Original budget */
} lp_budget_result;

/* Pack segments into a token budget.
   - Error segments are always included (mandatory).
   - Remaining budget is filled by highest-scoring segments.
   - reserve_tokens: tokens to hold back for stats header, freq table, tail.
   Returns a budget result. Caller must free result.indices. */
lp_budget_result lp_budget_pack(lp_segment *segs, size_t seg_count,
                                size_t budget_tokens, size_t reserve_tokens);

void lp_budget_result_free(lp_budget_result *r);

#endif /* LP_BUDGET_H */
