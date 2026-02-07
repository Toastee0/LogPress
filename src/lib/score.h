/*
 * score.h — Interest scoring for segments
 */
#ifndef LP_SCORE_H
#define LP_SCORE_H

#include "segment.h"
#include "dedup.h"

struct lp_mode;

/* Score a single segment based on mode config, keywords, and dedup stats.
   extra_keywords/extra_kw_count: CLI --keywords additions. */
float lp_score_segment(lp_segment *seg, const struct lp_mode *mode,
                       const char **extra_keywords, size_t extra_kw_count,
                       lp_dedup_table *dedup);

/* Score all segments in-place */
void lp_score_all(lp_segment *segs, size_t seg_count,
                  const struct lp_mode *mode,
                  const char **extra_keywords, size_t extra_kw_count,
                  lp_dedup_table *dedup);

#endif /* LP_SCORE_H */
