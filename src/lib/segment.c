/*
 * segment.c — Block detection for log files
 */
#include "segment.h"
#include "mode.h"
#include "token.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int lp_indent_level(const char *line) {
    int level = 0;
    while (*line) {
        if (*line == ' ') level++;
        else if (*line == '\t') level += 4;
        else break;
        line++;
    }
    return level;
}

bool lp_is_blank(const char *line) {
    while (*line) {
        if (!isspace((unsigned char)*line)) return false;
        line++;
    }
    return true;
}

bool lp_is_tabular(const char **lines, size_t count) {
    if (count < 3) return false;
    /* Check if lines have consistent column alignment by looking for
       2+ consecutive spaces (column separator) at similar positions */
    int col_positions[32] = {0};
    int max_cols = 0;

    for (size_t i = 0; i < count && i < 5; i++) {
        const char *p = lines[i];
        int ncols = 0;
        int pos = 0;
        bool in_space = false;
        while (*p && ncols < 32) {
            if (*p == ' ' || *p == '\t') {
                if (!in_space && pos > 0) {
                    in_space = true;
                }
            } else {
                if (in_space) {
                    col_positions[ncols]++;
                    ncols++;
                    in_space = false;
                }
            }
            pos++;
            p++;
        }
        if (ncols > max_cols) max_cols = ncols;
    }
    /* If most lines have similar column counts, it's tabular */
    return max_cols >= 2;
}

static lp_seg_type classify_line(const char *line, const struct lp_mode *mode) {
    /* Check mode-specific error patterns */
    if (mode) {
        for (size_t i = 0; i < mode->error_count; i++) {
            if (lp_str_contains_ci(line, mode->error_patterns[i]))
                return LP_SEG_ERROR;
        }
        for (size_t i = 0; i < mode->warning_count; i++) {
            if (lp_str_contains_ci(line, mode->warning_patterns[i]))
                return LP_SEG_WARNING;
        }
    }
    /* Generic fallbacks */
    if (lp_str_contains_ci(line, "error:") || lp_str_contains_ci(line, "fatal:") ||
        lp_str_contains_ci(line, "FAILED") || lp_str_contains_ci(line, "undefined reference"))
        return LP_SEG_ERROR;
    if (lp_str_contains_ci(line, "warning:"))
        return LP_SEG_WARNING;
    return LP_SEG_NORMAL;
}

static bool is_phase_marker(const char *line, const struct lp_mode *mode) {
    if (!mode) return false;
    for (size_t i = 0; i < mode->phase_count; i++) {
        if (lp_str_contains(line, mode->phase_markers[i]))
            return true;
    }
    return false;
}

static bool is_block_trigger(const char *line, const struct lp_mode *mode) {
    if (!mode) return false;
    for (size_t i = 0; i < mode->trigger_count; i++) {
        if (lp_str_contains_ci(line, mode->block_triggers[i]))
            return true;
    }
    return false;
}

lp_segment *lp_segment_detect(const char **lines, size_t count,
                              const struct lp_mode *mode, size_t *out_count) {
    LP_VEC(lp_segment) segs;
    lp_vec_init(segs);

    if (count == 0) {
        *out_count = 0;
        return NULL;
    }

    size_t i = 0;
    while (i < count) {
        /* Skip blank lines between segments */
        if (lp_is_blank(lines[i])) { i++; continue; }

        /* Start a new segment */
        size_t seg_start = i;
        lp_seg_type seg_type = LP_SEG_NORMAL;
        int base_indent = lp_indent_level(lines[i]);

        /* Check if this is a phase marker */
        if (is_phase_marker(lines[i], mode)) {
            seg_type = LP_SEG_PHASE;
        }

        /* Classify first line */
        lp_seg_type line_type = classify_line(lines[i], mode);
        if (line_type > seg_type || (line_type == LP_SEG_ERROR))
            seg_type = line_type;

        i++;

        /* Extend segment: continue until blank line, major indent change, or phase marker */
        while (i < count) {
            if (lp_is_blank(lines[i])) break;
            if (is_phase_marker(lines[i], mode) && i > seg_start) break;

            int indent = lp_indent_level(lines[i]);
            /* A big indent decrease (back to base or less) after indented block = new segment */
            if (indent < base_indent - 2 && i > seg_start + 1) break;

            /* Classify this line too */
            line_type = classify_line(lines[i], mode);
            if (line_type == LP_SEG_ERROR) seg_type = LP_SEG_ERROR;
            else if (line_type == LP_SEG_WARNING && seg_type == LP_SEG_NORMAL)
                seg_type = LP_SEG_WARNING;

            /* Block trigger check */
            if (is_block_trigger(lines[i], mode) && i > seg_start + 2 &&
                seg_type == LP_SEG_NORMAL) {
                /* Start fresh segment for the triggered block */
                break;
            }

            i++;
        }

        size_t seg_end = i;
        size_t seg_lines = seg_end - seg_start;

        /* Check if this segment is tabular data */
        if (seg_type == LP_SEG_NORMAL && lp_is_tabular(lines + seg_start, seg_lines)) {
            seg_type = LP_SEG_DATA;
        }

        /* Build segment */
        lp_segment seg;
        seg.start_line = seg_start;
        seg.end_line = seg_end - 1;
        seg.type = seg_type;
        seg.line_count = seg_lines;
        seg.score = 0.0f;

        /* Copy line pointers (not the strings themselves) */
        seg.lines = (char **)malloc(seg_lines * sizeof(char *));
        for (size_t j = 0; j < seg_lines; j++)
            seg.lines[j] = (char *)lines[seg_start + j];

        /* Estimate tokens */
        seg.token_count = lp_estimate_tokens_lines((const char **)(lines + seg_start), seg_lines);

        /* Generate label */
        char label_buf[128];
        switch (seg_type) {
            case LP_SEG_ERROR:   snprintf(label_buf, sizeof(label_buf), "error"); break;
            case LP_SEG_WARNING: snprintf(label_buf, sizeof(label_buf), "warning"); break;
            case LP_SEG_DATA:    snprintf(label_buf, sizeof(label_buf), "data"); break;
            case LP_SEG_PHASE:   snprintf(label_buf, sizeof(label_buf), "phase"); break;
            case LP_SEG_INFO:    snprintf(label_buf, sizeof(label_buf), "info"); break;
            default:             snprintf(label_buf, sizeof(label_buf), "block"); break;
        }
        seg.label = strdup(label_buf);

        lp_vec_push(segs, seg);
    }

    *out_count = segs.len;
    return segs.items;
}

void lp_segment_free(lp_segment *seg) {
    free(seg->label);
    free(seg->lines);
}

void lp_segments_free(lp_segment *segs, size_t count) {
    for (size_t i = 0; i < count; i++)
        lp_segment_free(&segs[i]);
    free(segs);
}
