/*
 * mode.h — TOML mode loader + auto-detect
 */
#ifndef LP_MODE_H
#define LP_MODE_H

#include <stddef.h>
#include <stdbool.h>

/* Build system mode configuration */
typedef struct lp_mode {
    char  *name;
    char  *description;
    char **signatures;       /* Detection strings */
    size_t sig_count;
    char **strip_patterns;   /* Regex patterns for dedup normalization */
    size_t strip_count;
    char **phase_markers;
    size_t phase_count;
    char **block_triggers;
    size_t trigger_count;
    char **keywords;
    size_t keyword_count;
    char **error_patterns;
    size_t error_count;
    char **warning_patterns;
    size_t warning_count;
    char  *progress_pattern;  /* Regex for build progress lines (e.g. [N/M]) */
    char **boilerplate_patterns;
    size_t boilerplate_count;

    /* Elision: lines matching these are silently dropped (never shown) */
    char **drop_contains;
    size_t drop_count;
    /* Elision: lines matching these appear once in summary, never elsewhere */
    char **keep_once_contains;
    size_t keep_once_count;

    /* Summary extraction patterns (regex with capture groups) */
    char  *board_pattern;
    char  *zephyr_version_pattern;
    char  *toolchain_pattern;
    char  *overlay_pattern;
    char  *memory_pattern;
    char  *output_pattern;
} lp_mode;

/* Load a single mode from a TOML file. Returns NULL on error. */
lp_mode *lp_mode_load(const char *path);

/* Free a mode */
void lp_mode_free(lp_mode *m);

/* Load all modes from a directory. Returns malloc'd array. Sets *count. */
lp_mode **lp_mode_load_dir(const char *dir, size_t *count);

/* Free an array of modes */
void lp_modes_free(lp_mode **modes, size_t count);

/* Auto-detect: sniff first N lines against all loaded modes.
   Returns the best-matching mode name (from modes array), or "generic".
   The returned pointer is owned by the modes array. */
const char *lp_mode_detect(const char **first_lines, size_t line_count,
                           lp_mode **modes, size_t mode_count);

/* Find a mode by name in an array. Returns NULL if not found. */
lp_mode *lp_mode_find(lp_mode **modes, size_t mode_count, const char *name);

/* Search for modes directory. Tries: ./modes, $LOGPILOT_MODES, exe dir.
   Returns malloc'd path or NULL. */
char *lp_mode_find_dir(void);

#endif /* LP_MODE_H */
