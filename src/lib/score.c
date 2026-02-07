/*
 * score.c — Interest scoring for segments
 */
#include "score.h"
#include "mode.h"

#include <stdlib.h>
#include <string.h>

float lp_score_segment(lp_segment *seg, const struct lp_mode *mode,
                       const char **extra_keywords, size_t extra_kw_count,
                       lp_dedup_table *dedup) {
    float score = 0.0f;

    /* Type-based base score */
    switch (seg->type) {
        case LP_SEG_ERROR:   score += 10.0f; break;
        case LP_SEG_WARNING: score += 5.0f;  break;
        case LP_SEG_DATA:    score += 4.0f;  break;
        case LP_SEG_PHASE:   score += 2.0f;  break;
        default: break;
    }

    /* Keyword matches from mode */
    if (mode) {
        for (size_t i = 0; i < seg->line_count; i++) {
            for (size_t k = 0; k < mode->keyword_count; k++) {
                if (lp_str_contains(seg->lines[i], mode->keywords[k]))
                    score += 3.0f;
            }
            /* Mode-specific trigger match */
            for (size_t k = 0; k < mode->trigger_count; k++) {
                if (lp_str_contains_ci(seg->lines[i], mode->block_triggers[k]))
                    score += 1.0f;
            }
        }
    }

    /* Extra CLI keywords */
    for (size_t i = 0; i < seg->line_count; i++) {
        for (size_t k = 0; k < extra_kw_count; k++) {
            if (lp_str_contains(seg->lines[i], extra_keywords[k]))
                score += 3.0f;
        }
    }

    /* Frequency outlier bonus */
    if (dedup && dedup->count > 0) {
        /* Get approximate percentile thresholds */
        size_t sorted_count;
        lp_dedup_entry **sorted = lp_dedup_sorted(dedup, &sorted_count);
        if (sorted_count > 0) {
            size_t top5_threshold_idx = sorted_count / 20;  /* 5% */
            size_t top5_count = sorted[top5_threshold_idx < sorted_count ? top5_threshold_idx : 0]->count;
            size_t bot5_idx = sorted_count - sorted_count / 20 - 1;
            if (bot5_idx >= sorted_count) bot5_idx = sorted_count - 1;
            size_t bot5_count = sorted[bot5_idx]->count;

            for (size_t i = 0; i < seg->line_count; i++) {
                /* Look up each line in the dedup table */
                size_t len = strlen(seg->lines[i]);
                uint64_t h = lp_fnv1a(seg->lines[i], len);
                size_t idx = (size_t)(h & (dedup->capacity - 1));
                while (dedup->buckets[idx].occupied) {
                    if (dedup->buckets[idx].hash == h) {
                        size_t c = dedup->buckets[idx].count;
                        if (c >= top5_count && top5_count > 1) score += 2.0f;
                        if (c <= bot5_count && c == 1) score += 2.0f;
                        break;
                    }
                    idx = (idx + 1) & (dedup->capacity - 1);
                }
            }
        }
        free(sorted);
    }

    return score;
}

void lp_score_all(lp_segment *segs, size_t seg_count,
                  const struct lp_mode *mode,
                  const char **extra_keywords, size_t extra_kw_count,
                  lp_dedup_table *dedup) {
    for (size_t i = 0; i < seg_count; i++) {
        segs[i].score = lp_score_segment(&segs[i], mode, extra_keywords, extra_kw_count, dedup);
    }
}
