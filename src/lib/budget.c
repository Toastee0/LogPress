/*
 * budget.c — Token budget packing (greedy knapsack)
 */
#include "budget.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t idx;
    float  score;
} scored_idx;

static int cmp_score_desc(const void *a, const void *b) {
    float sa = ((const scored_idx *)a)->score;
    float sb = ((const scored_idx *)b)->score;
    if (sa > sb) return -1;
    if (sa < sb) return 1;
    return 0;
}

static int cmp_size_t_asc(const void *a, const void *b) {
    size_t va = *(const size_t *)a;
    size_t vb = *(const size_t *)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

lp_budget_result lp_budget_pack(lp_segment *segs, size_t seg_count,
                                size_t budget_tokens, size_t reserve_tokens) {
    lp_budget_result result;
    result.budget_tokens = budget_tokens;
    result.indices = (size_t *)malloc(seg_count * sizeof(size_t));
    result.count = 0;
    result.total_tokens = 0;

    size_t available = budget_tokens > reserve_tokens ? budget_tokens - reserve_tokens : 0;

    /* Phase 1: mandatory error segments */
    for (size_t i = 0; i < seg_count; i++) {
        if (segs[i].type == LP_SEG_ERROR) {
            result.indices[result.count++] = i;
            result.total_tokens += segs[i].token_count;
        }
    }

    /* Phase 2: fill remaining with highest-scoring non-error segments */
    scored_idx *candidates = (scored_idx *)malloc(seg_count * sizeof(scored_idx));
    size_t ncand = 0;
    for (size_t i = 0; i < seg_count; i++) {
        if (segs[i].type == LP_SEG_ERROR) continue;  /* already included */
        candidates[ncand].idx = i;
        candidates[ncand].score = segs[i].score;
        ncand++;
    }
    qsort(candidates, ncand, sizeof(scored_idx), cmp_score_desc);

    for (size_t c = 0; c < ncand; c++) {
        size_t idx = candidates[c].idx;
        if (result.total_tokens + segs[idx].token_count <= available) {
            result.indices[result.count++] = idx;
            result.total_tokens += segs[idx].token_count;
        }
    }
    free(candidates);

    /* Sort included indices by line position for ordered output */
    qsort(result.indices, result.count, sizeof(size_t), cmp_size_t_asc);

    result.total_tokens += reserve_tokens; /* account for reserved */
    return result;
}

void lp_budget_result_free(lp_budget_result *r) {
    free(r->indices);
    r->indices = NULL;
    r->count = 0;
}
