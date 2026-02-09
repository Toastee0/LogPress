/*
 * fix.h — YAML fix database loader + matching
 */
#ifndef LP_FIX_H
#define LP_FIX_H

#include <stddef.h>
#include <stdbool.h>

/* A fix entry */
typedef struct {
    char   *pattern;      /* Short match pattern */
    char   *regex;        /* Optional regex for precise matching */
    char  **tags;         /* Tag array */
    size_t  tag_count;
    char   *fix_text;     /* The fix description */
    char   *context;      /* When/why this was encountered */
    char   *severity;     /* "error", "warning", etc. */
    char   *resolved;     /* Date string */
    char   *commit_ref;   /* Git commit reference */
    char   *file_path;    /* Where this fix was loaded from */
} lp_fix;

/* A match result */
typedef struct {
    lp_fix *fix;
    float   confidence;   /* 0.0 - 1.0 */
} lp_fix_match;

/* Load a single fix from a YAML file */
lp_fix *lp_fix_load(const char *path);

/* Free a fix */
void lp_fix_free(lp_fix *f);

/* Load all fixes from a directory (recursive) */
lp_fix **lp_fix_load_dir(const char *dir, size_t *count);

/* Free an array of fixes */
void lp_fixes_free(lp_fix **fixes, size_t count);

/* Match an error string against all fixes.
   Returns malloc'd array of matches sorted by confidence desc. Sets *match_count.
   Only returns matches above min_confidence. */
lp_fix_match *lp_fix_match_all(const char *error_text, lp_fix **fixes, size_t fix_count,
                                size_t *match_count, float min_confidence);

/* Free match results */
void lp_fix_matches_free(lp_fix_match *matches, size_t count);

/* Validate a fix entry (check required fields) */
bool lp_fix_validate(const lp_fix *f, char *errbuf, size_t errlen);

/* Write a fix entry to a YAML file */
int lp_fix_write(const char *path, const lp_fix *f);

/* Find fixes directory. Tries: ./fixes, $LOGPILOT_FIXES, exe dir. */
char *lp_fix_find_dir(void);

/* Find global (OS-level) fixes directory: ~/.logpilot/fixes/ */
char *lp_fix_find_global_dir(void);

#endif /* LP_FIX_H */
