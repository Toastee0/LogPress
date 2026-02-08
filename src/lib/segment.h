/*
 * segment.h — Block detection for log files
 */
#ifndef LP_SEGMENT_H
#define LP_SEGMENT_H

#include <stddef.h>
#include <stdbool.h>
#include "util.h"

/* Segment types */
typedef enum {
    LP_SEG_ERROR,
    LP_SEG_WARNING,
    LP_SEG_INFO,
    LP_SEG_DATA,      /* Tabular data (memory maps, test summaries) */
    LP_SEG_PHASE,     /* Phase boundary marker */
    LP_SEG_BUILD_PROGRESS, /* Build step lines: [N/M] Building/Linking/Generating */
    LP_SEG_BOILERPLATE,   /* CMake/west config lines — zero diagnostic value */
    LP_SEG_NORMAL
} lp_seg_type;

/* A detected segment (contiguous block of lines) */
typedef struct {
    size_t       start_line;
    size_t       end_line;
    lp_seg_type  type;
    char        *label;         /* Human-readable label (e.g. "devicetree-error") */
    char       **lines;         /* Pointers into the line array (not owned) */
    size_t       line_count;
    size_t       token_count;
    float        score;         /* Set later by scoring */
} lp_segment;

/* Forward-declare mode struct to avoid circular include */
struct lp_mode;

/* Detect segments from an array of lines.
   lines[]: array of string pointers (not owned).
   count: number of lines.
   mode: parsed mode config (may be NULL for generic detection).
   Returns malloc'd array of segments. Sets *out_count. */
lp_segment *lp_segment_detect(const char **lines, size_t count,
                              const struct lp_mode *mode, size_t *out_count);

/* Free a single segment's owned data */
void lp_segment_free(lp_segment *seg);

/* Free an array of segments */
void lp_segments_free(lp_segment *segs, size_t count);

/* Get indentation level (number of leading spaces/tabs) */
int lp_indent_level(const char *line);

/* Check if a line is blank (empty or whitespace only) */
bool lp_is_blank(const char *line);

/* Detect if lines form tabular data (consistent column alignment) */
bool lp_is_tabular(const char **lines, size_t count);

/* Check if a line is a ninja/cmake build progress line like [N/M] Building... */
bool lp_is_build_progress(const char *line);

/* Check if a line matches a boilerplate pattern */
bool lp_is_boilerplate(const char *line, const struct lp_mode *mode);

/* Check if a line is a compiler/linker command invocation (long noise) */
bool lp_is_compiler_command(const char *line);

#endif /* LP_SEGMENT_H */
