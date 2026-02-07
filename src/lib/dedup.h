/*
 * dedup.h — Line hashing + frequency table
 */
#ifndef LP_DEDUP_H
#define LP_DEDUP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* A single entry in the dedup table */
typedef struct {
    char    *normalized;   /* Normalized (stripped) line text */
    char    *original;     /* First-seen original line text */
    size_t   first_line;   /* Line number of first occurrence */
    size_t   count;        /* Number of occurrences */
    uint64_t hash;         /* FNV-1a hash of normalized text */
    bool     occupied;
} lp_dedup_entry;

/* The dedup hash table */
typedef struct {
    lp_dedup_entry *buckets;
    size_t           capacity;  /* Power of 2 */
    size_t           count;     /* Number of occupied buckets */
} lp_dedup_table;

/* Strip pattern for normalization */
typedef struct {
    const char *pattern;   /* tiny-regex-c pattern string */
} lp_strip_pattern;

void lp_dedup_init(lp_dedup_table *t, size_t initial_cap);
void lp_dedup_free(lp_dedup_table *t);

/* Insert a line. Returns pointer to the entry (new or existing). */
lp_dedup_entry *lp_dedup_insert(lp_dedup_table *t, const char *line, size_t line_num,
                                const char **strip_patterns, size_t strip_count);

/* Normalize a line: apply strip patterns, collapse whitespace */
char *lp_normalize_line(const char *line, const char **strip_patterns, size_t strip_count);

/* Get frequency table sorted by count descending.
   Returns malloc'd array of pointers. Sets *out_count. */
lp_dedup_entry **lp_dedup_sorted(lp_dedup_table *t, size_t *out_count);

/* FNV-1a hash */
uint64_t lp_fnv1a(const char *data, size_t len);

#endif /* LP_DEDUP_H */
