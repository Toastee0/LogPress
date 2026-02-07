/*
 * dedup.c — Line hashing + frequency table
 */
#include "dedup.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <re.h>

/* FNV-1a constants */
#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME  1099511628211ULL

uint64_t lp_fnv1a(const char *data, size_t len) {
    uint64_t h = FNV_OFFSET;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint64_t)(unsigned char)data[i];
        h *= FNV_PRIME;
    }
    return h;
}

static size_t next_pow2(size_t n) {
    size_t v = 1;
    while (v < n) v <<= 1;
    return v;
}

void lp_dedup_init(lp_dedup_table *t, size_t initial_cap) {
    t->capacity = next_pow2(initial_cap < 64 ? 64 : initial_cap);
    t->count = 0;
    t->buckets = (lp_dedup_entry *)calloc(t->capacity, sizeof(lp_dedup_entry));
}

void lp_dedup_free(lp_dedup_table *t) {
    for (size_t i = 0; i < t->capacity; i++) {
        if (t->buckets[i].occupied) {
            free(t->buckets[i].normalized);
            free(t->buckets[i].original);
        }
    }
    free(t->buckets);
    t->buckets = NULL;
    t->capacity = t->count = 0;
}

static void dedup_grow(lp_dedup_table *t) {
    size_t new_cap = t->capacity * 2;
    lp_dedup_entry *new_buckets = (lp_dedup_entry *)calloc(new_cap, sizeof(lp_dedup_entry));

    for (size_t i = 0; i < t->capacity; i++) {
        if (!t->buckets[i].occupied) continue;
        uint64_t h = t->buckets[i].hash;
        size_t idx = (size_t)(h & (new_cap - 1));
        while (new_buckets[idx].occupied) {
            idx = (idx + 1) & (new_cap - 1);
        }
        new_buckets[idx] = t->buckets[i];
    }

    free(t->buckets);
    t->buckets = new_buckets;
    t->capacity = new_cap;
}

char *lp_normalize_line(const char *line, const char **strip_patterns, size_t strip_count) {
    /* Start with a copy */
    char *result = strdup(line);
    if (!result) return NULL;

    /* Apply each strip pattern using tiny-regex-c */
    for (size_t i = 0; i < strip_count; i++) {
        re_t pat = re_compile(strip_patterns[i]);
        if (!pat) continue;

        /* Replace all matches with a single space */
        char *buf = (char *)malloc(strlen(result) + 1);
        size_t out_pos = 0;
        const char *p = result;
        int match_len;

        while (*p) {
            int match_idx = re_matchp(pat, p, &match_len);
            if (match_idx >= 0 && match_len > 0) {
                /* Copy text before match */
                for (int j = 0; j < match_idx; j++)
                    buf[out_pos++] = p[j];
                buf[out_pos++] = ' ';
                p += match_idx + match_len;
            } else {
                /* No more matches, copy rest */
                while (*p) buf[out_pos++] = *p++;
            }
        }
        buf[out_pos] = '\0';
        free(result);
        result = buf;
    }

    /* Collapse whitespace */
    char *dst = result;
    const char *src = result;
    bool in_ws = false;
    while (isspace((unsigned char)*src)) src++; /* skip leading */
    while (*src) {
        if (isspace((unsigned char)*src)) {
            if (!in_ws) { *dst++ = ' '; in_ws = true; }
            src++;
        } else {
            *dst++ = *src++;
            in_ws = false;
        }
    }
    /* Trim trailing */
    if (dst > result && *(dst - 1) == ' ') dst--;
    *dst = '\0';

    return result;
}

lp_dedup_entry *lp_dedup_insert(lp_dedup_table *t, const char *line, size_t line_num,
                                const char **strip_patterns, size_t strip_count) {
    /* Grow if load factor > 0.7 */
    if (t->count * 10 > t->capacity * 7) {
        dedup_grow(t);
    }

    char *norm = lp_normalize_line(line, strip_patterns, strip_count);
    size_t norm_len = strlen(norm);
    uint64_t h = lp_fnv1a(norm, norm_len);
    size_t idx = (size_t)(h & (t->capacity - 1));

    while (t->buckets[idx].occupied) {
        if (t->buckets[idx].hash == h && strcmp(t->buckets[idx].normalized, norm) == 0) {
            /* Existing entry */
            t->buckets[idx].count++;
            free(norm);
            return &t->buckets[idx];
        }
        idx = (idx + 1) & (t->capacity - 1);
    }

    /* New entry */
    t->buckets[idx].occupied = true;
    t->buckets[idx].hash = h;
    t->buckets[idx].normalized = norm;
    t->buckets[idx].original = strdup(line);
    t->buckets[idx].first_line = line_num;
    t->buckets[idx].count = 1;
    t->count++;

    return &t->buckets[idx];
}

static int cmp_freq_desc(const void *a, const void *b) {
    const lp_dedup_entry *ea = *(const lp_dedup_entry **)a;
    const lp_dedup_entry *eb = *(const lp_dedup_entry **)b;
    if (ea->count > eb->count) return -1;
    if (ea->count < eb->count) return 1;
    return 0;
}

lp_dedup_entry **lp_dedup_sorted(lp_dedup_table *t, size_t *out_count) {
    lp_dedup_entry **arr = (lp_dedup_entry **)malloc(t->count * sizeof(lp_dedup_entry *));
    size_t n = 0;
    for (size_t i = 0; i < t->capacity; i++) {
        if (t->buckets[i].occupied) {
            arr[n++] = &t->buckets[i];
        }
    }
    qsort(arr, n, sizeof(lp_dedup_entry *), cmp_freq_desc);
    *out_count = n;
    return arr;
}
