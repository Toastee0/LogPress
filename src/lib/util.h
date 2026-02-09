/*
 * util.h — Foundation utilities for LogPilot
 * Strings, dynamic arrays, file I/O, platform shims
 */
#ifndef LP_UTIL_H
#define LP_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

/* ---- Length-prefixed string ---- */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} lp_string;

lp_string lp_string_new(size_t initial_cap);
void      lp_string_free(lp_string *s);
void      lp_string_append(lp_string *s, const char *str, size_t len);
void      lp_string_append_cstr(lp_string *s, const char *str);
char     *lp_string_cstr(lp_string *s);  /* NUL-terminated view */
void      lp_string_clear(lp_string *s);

/* ---- Generic dynamic array ---- */
#define LP_VEC(T) struct { T *items; size_t len; size_t cap; }

#define lp_vec_init(v) do { (v).items = NULL; (v).len = 0; (v).cap = 0; } while(0)

#define lp_vec_push(v, item) do {                                         \
    if ((v).len >= (v).cap) {                                             \
        (v).cap = (v).cap ? (v).cap * 2 : 8;                             \
        (v).items = realloc((v).items, (v).cap * sizeof(*(v).items));     \
    }                                                                     \
    (v).items[(v).len++] = (item);                                        \
} while(0)

#define lp_vec_at(v, i) ((v).items[i])
#define lp_vec_free(v) do { free((v).items); (v).items = NULL; (v).len = (v).cap = 0; } while(0)

/* ---- File I/O ---- */
/* Read one line from fp into buf. Returns line length, or -1 on EOF.
   Caller owns buf (realloc'd as needed). */
int  lp_readline(FILE *fp, char **buf, size_t *buf_cap);

/* Read entire file into a malloc'd buffer. Sets *out_len. Returns NULL on error. */
char *lp_read_file(const char *path, size_t *out_len);

/* ---- String utilities ---- */
char *lp_strtrim(const char *str);           /* Returns malloc'd trimmed copy */
char *lp_strdup_range(const char *s, size_t start, size_t end);
bool  lp_str_starts_with(const char *str, const char *prefix);
bool  lp_str_contains(const char *haystack, const char *needle);
bool  lp_str_contains_ci(const char *haystack, const char *needle);

/* Split a CSV string. Returns malloc'd array of malloc'd strings. Sets *count. */
char **lp_split_csv(const char *csv, size_t *count);
void   lp_free_strings(char **strs, size_t count);

/* ---- Platform ---- */
char *lp_path_join(const char *dir, const char *file);
bool  lp_file_exists(const char *path);

/* Get directory containing the running executable. Returns malloc'd string. */
char *lp_get_exe_dir(void);

/* Iterate files in a directory matching a suffix (e.g. ".toml").
   Calls cb(path, userdata) for each match. */
typedef void (*lp_dir_cb)(const char *path, void *userdata);
int lp_dir_iter(const char *dir, const char *suffix, lp_dir_cb cb, void *userdata);

/* Recursively iterate files matching suffix */
int lp_dir_iter_recursive(const char *dir, const char *suffix, lp_dir_cb cb, void *userdata);

#endif /* LP_UTIL_H */
