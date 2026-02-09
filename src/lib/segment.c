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

bool lp_is_build_progress(const char *line) {
    /* Match lines like: [1/203] Building C object ...
       Also matches [5/10] Generating ..., [198/203] Linking ..., etc.
       Tolerates leading whitespace. */
    const char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '[') return false;
    p++;
    /* Expect digits */
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) p++;
    if (*p != '/') return false;
    p++;
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) p++;
    if (*p != ']') return false;
    return true;
}

bool lp_is_compiler_command(const char *line) {
    /* Compiler/linker invocations are long lines with many flags.
       Heuristic: line is >300 chars and contains a compiler executable. */
    size_t len = strlen(line);
    if (len < 300) return false;
    /* Look for common compiler/linker executables */
    if (strstr(line, "gcc") || strstr(line, "g++") ||
        strstr(line, "clang") || strstr(line, "cl.exe") ||
        strstr(line, "/cc ") || strstr(line, "/ld ") ||
        strstr(line, "arm-zephyr-eabi") || strstr(line, "arm-none-eabi") ||
        strstr(line, "xtensa-") || strstr(line, "riscv")) {
        /* Confirm: has multiple flag-like tokens */
        if (strstr(line, " -D") || strstr(line, " -I") ||
            strstr(line, " -f") || strstr(line, " -W") ||
            strstr(line, " /D") || strstr(line, " /I")) {
            return true;
        }
    }
    return false;
}

bool lp_is_boilerplate(const char *line, const struct lp_mode *mode) {
    if (!mode || !mode->boilerplate_patterns) return false;
    for (size_t i = 0; i < mode->boilerplate_count; i++) {
        if (lp_str_contains(line, mode->boilerplate_patterns[i]))
            return true;
    }
    return false;
}

bool lp_is_source_context(const char *line) {
    /* GCC/clang source context lines:
       "   42 |   some_code_here"   (line number + pipe + source)
       "      |   ^~~~"             (caret line, no line number)
       "      |   ~~~~~"            (underline continuation)
    */
    const char *p = line;
    while (*p == ' ') p++;  /* skip leading spaces */

    /* Case 1: digits followed by ' | ' — source line with line number */
    if (isdigit((unsigned char)*p)) {
        while (isdigit((unsigned char)*p)) p++;
        if (*p == ' ' && *(p + 1) == '|' && *(p + 2) == ' ') return true;
    }

    /* Case 2: ' | ' directly — caret/underline line (no line number) */
    if (*p == '|' && (*(p + 1) == ' ' || *(p + 1) == '\0')) return true;

    /* Case 3: bare caret/tilde line (some compilers): "      ^~~~~" */
    if (*p == '^' || *p == '~') {
        const char *q = p;
        while (*q == '^' || *q == '~' || *q == ' ') q++;
        if (*q == '\0' || *q == '\n') return true;
    }

    return false;
}

lp_fate lp_line_fate(const char *line, const struct lp_mode *mode) {
    if (!line) return LP_FATE_DROP;

    /* Blank lines: drop */
    if (lp_is_blank(line)) return LP_FATE_DROP;

    /* Error/warning lines always survive */
    if (lp_str_contains_ci(line, "error:") || lp_str_contains_ci(line, "fatal:") ||
        lp_str_contains_ci(line, "FAILED") || lp_str_contains_ci(line, "undefined reference"))
        return LP_FATE_KEEP;
    if (lp_str_contains_ci(line, "warning:"))
        return LP_FATE_KEEP;

    if (mode) {
        /* Mode-specific error/warning patterns → KEEP */
        for (size_t i = 0; i < mode->error_count; i++) {
            if (lp_str_contains_ci(line, mode->error_patterns[i]))
                return LP_FATE_KEEP;
        }
        for (size_t i = 0; i < mode->warning_count; i++) {
            if (lp_str_contains_ci(line, mode->warning_patterns[i]))
                return LP_FATE_KEEP;
        }

        /* Explicit drop patterns → DROP */
        for (size_t i = 0; i < mode->drop_count; i++) {
            if (lp_str_contains(line, mode->drop_contains[i]))
                return LP_FATE_DROP;
        }

        /* Boilerplate → DROP */
        if (lp_is_boilerplate(line, mode))
            return LP_FATE_DROP;

        /* Keep-once patterns → KEEP_ONCE */
        for (size_t i = 0; i < mode->keep_once_count; i++) {
            if (lp_str_contains(line, mode->keep_once_contains[i]))
                return LP_FATE_KEEP_ONCE;
        }
    }

    /* Build progress and compiler commands → DROP */
    if (lp_is_build_progress(line)) return LP_FATE_DROP;
    if (lp_is_compiler_command(line)) return LP_FATE_DROP;

    return LP_FATE_KEEP;
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

/* Build and push a segment onto the vector */
static void push_segment(void *segs_ptr, const char **lines,
                         size_t seg_start, size_t seg_end, lp_seg_type seg_type) {
    /* segs_ptr is LP_VEC(lp_segment)* — we use a macro-compatible approach */
    typedef struct { lp_segment *items; size_t len; size_t cap; } seg_vec;
    seg_vec *sv = (seg_vec *)segs_ptr;

    size_t seg_lines = seg_end - seg_start;
    if (seg_lines == 0) return;

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
        case LP_SEG_ERROR:          snprintf(label_buf, sizeof(label_buf), "error"); break;
        case LP_SEG_WARNING:        snprintf(label_buf, sizeof(label_buf), "warning"); break;
        case LP_SEG_DATA:           snprintf(label_buf, sizeof(label_buf), "data"); break;
        case LP_SEG_PHASE:          snprintf(label_buf, sizeof(label_buf), "phase"); break;
        case LP_SEG_INFO:           snprintf(label_buf, sizeof(label_buf), "info"); break;
        case LP_SEG_BUILD_PROGRESS: snprintf(label_buf, sizeof(label_buf), "build"); break;
        case LP_SEG_BOILERPLATE:    snprintf(label_buf, sizeof(label_buf), "boilerplate"); break;
        default:                    snprintf(label_buf, sizeof(label_buf), "block"); break;
    }
    seg.label = strdup(label_buf);

    /* Push onto vector */
    if (sv->len >= sv->cap) {
        sv->cap = sv->cap ? sv->cap * 2 : 8;
        sv->items = (lp_segment *)realloc(sv->items, sv->cap * sizeof(lp_segment));
    }
    sv->items[sv->len++] = seg;
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
        bool saw_error_content = false;

        /* Check if this is a phase marker */
        if (is_phase_marker(lines[i], mode)) {
            seg_type = LP_SEG_PHASE;
        }

        /* Check if first line is build progress */
        bool first_is_progress = lp_is_build_progress(lines[i]);

        /* Classify first line */
        lp_seg_type line_type = classify_line(lines[i], mode);
        if (line_type == LP_SEG_ERROR) {
            seg_type = LP_SEG_ERROR;
            saw_error_content = true;
        } else if (line_type > seg_type) {
            seg_type = line_type;
        }

        /* If first line is progress and not an error, mark as build progress */
        if (first_is_progress && seg_type == LP_SEG_NORMAL) {
            seg_type = LP_SEG_BUILD_PROGRESS;
        }

        i++;

        /* Extend segment: continue until blank line, major indent change, or phase marker */
        while (i < count) {
            if (lp_is_blank(lines[i])) break;
            if (is_phase_marker(lines[i], mode) && i > seg_start) break;

            int indent = lp_indent_level(lines[i]);
            /* A big indent decrease (back to base or less) after indented block = new segment */
            if (indent < base_indent - 2 && i > seg_start + 1) break;

            /* Classify this line */
            line_type = classify_line(lines[i], mode);
            bool this_is_progress = lp_is_build_progress(lines[i]);

            /* KEY FIX: If we're in an error segment and hit a normal build
               progress line (not itself an error), break the segment here.
               This prevents [N/M] Building lines after the error from being
               absorbed into the error block. */
            if (saw_error_content && this_is_progress && line_type == LP_SEG_NORMAL) {
                break;
            }

            /* If we're in a build progress segment and hit a non-progress line
               that's not just a cmake status line, break */
            if (seg_type == LP_SEG_BUILD_PROGRESS && !this_is_progress &&
                line_type == LP_SEG_ERROR) {
                break;
            }

            if (line_type == LP_SEG_ERROR) {
                seg_type = LP_SEG_ERROR;
                saw_error_content = true;
            } else if (line_type == LP_SEG_WARNING && seg_type == LP_SEG_NORMAL)
                seg_type = LP_SEG_WARNING;

            /* If we're in a normal/progress segment and all lines so far are
               progress lines, keep it as build progress */
            if (seg_type == LP_SEG_BUILD_PROGRESS && !this_is_progress &&
                line_type == LP_SEG_NORMAL) {
                /* Non-progress normal line mixed in (e.g. cmake status) — keep extending */
            }

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

        /* Post-classify: if most lines are boilerplate, mark as such */
        if (seg_type == LP_SEG_NORMAL || seg_type == LP_SEG_DATA) {
            size_t bp_count = 0;
            size_t progress_count = 0;
            for (size_t j = seg_start; j < seg_end; j++) {
                if (lp_is_boilerplate(lines[j], mode)) bp_count++;
                if (lp_is_build_progress(lines[j])) progress_count++;
            }
            if (bp_count * 2 >= seg_lines && seg_type != LP_SEG_ERROR) {
                seg_type = LP_SEG_BOILERPLATE;
            } else if (progress_count * 2 >= seg_lines && seg_type == LP_SEG_NORMAL) {
                seg_type = LP_SEG_BUILD_PROGRESS;
            }
        }

        /* Check if this segment is tabular data */
        if (seg_type == LP_SEG_NORMAL && lp_is_tabular(lines + seg_start, seg_lines)) {
            seg_type = LP_SEG_DATA;
        }

        push_segment(&segs, lines, seg_start, seg_end, seg_type);
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