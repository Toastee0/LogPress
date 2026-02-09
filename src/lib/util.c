/*
 * util.c — Foundation utilities for LogPilot
 */
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* ================================================================
 * lp_string
 * ================================================================ */

lp_string lp_string_new(size_t initial_cap) {
    lp_string s;
    s.cap = initial_cap ? initial_cap : 64;
    s.data = (char *)malloc(s.cap);
    s.len = 0;
    if (s.data) s.data[0] = '\0';
    return s;
}

void lp_string_free(lp_string *s) {
    free(s->data);
    s->data = NULL;
    s->len = s->cap = 0;
}

void lp_string_append(lp_string *s, const char *str, size_t len) {
    if (s->len + len + 1 > s->cap) {
        while (s->len + len + 1 > s->cap)
            s->cap = s->cap ? s->cap * 2 : 64;
        s->data = (char *)realloc(s->data, s->cap);
    }
    memcpy(s->data + s->len, str, len);
    s->len += len;
    s->data[s->len] = '\0';
}

void lp_string_append_cstr(lp_string *s, const char *str) {
    lp_string_append(s, str, strlen(str));
}

char *lp_string_cstr(lp_string *s) {
    if (s->data) s->data[s->len] = '\0';
    return s->data;
}

void lp_string_clear(lp_string *s) {
    s->len = 0;
    if (s->data) s->data[0] = '\0';
}

/* ================================================================
 * File I/O
 * ================================================================ */

int lp_readline(FILE *fp, char **buf, size_t *buf_cap) {
    if (!*buf) {
        *buf_cap = 256;
        *buf = (char *)malloc(*buf_cap);
    }

    size_t pos = 0;
    int ch;
    while ((ch = fgetc(fp)) != EOF) {
        if (pos + 2 > *buf_cap) {
            *buf_cap *= 2;
            *buf = (char *)realloc(*buf, *buf_cap);
        }
        if (ch == '\n') {
            (*buf)[pos] = '\0';
            return (int)pos;
        }
        if (ch == '\r') {
            int next = fgetc(fp);
            if (next != '\n' && next != EOF) ungetc(next, fp);
            (*buf)[pos] = '\0';
            return (int)pos;
        }
        (*buf)[pos++] = (char)ch;
    }
    if (pos > 0) {
        (*buf)[pos] = '\0';
        return (int)pos;
    }
    return -1;
}

char *lp_read_file(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz < 0) { fclose(fp); return NULL; }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }

    size_t rd = fread(buf, 1, (size_t)sz, fp);
    buf[rd] = '\0';
    if (out_len) *out_len = rd;
    fclose(fp);
    return buf;
}

/* ================================================================
 * String utilities
 * ================================================================ */

char *lp_strtrim(const char *str) {
    if (!str) return NULL;
    while (*str && isspace((unsigned char)*str)) str++;
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) len--;
    char *out = (char *)malloc(len + 1);
    memcpy(out, str, len);
    out[len] = '\0';
    return out;
}

char *lp_strdup_range(const char *s, size_t start, size_t end) {
    if (end <= start) return strdup("");
    size_t len = end - start;
    char *out = (char *)malloc(len + 1);
    memcpy(out, s + start, len);
    out[len] = '\0';
    return out;
}

bool lp_str_starts_with(const char *str, const char *prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

bool lp_str_contains(const char *haystack, const char *needle) {
    return strstr(haystack, needle) != NULL;
}

bool lp_str_contains_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen > hlen) return false;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

char **lp_split_csv(const char *csv, size_t *count) {
    *count = 0;
    if (!csv || !*csv) return NULL;

    /* Count commas to estimate */
    size_t cap = 8;
    char **result = (char **)malloc(cap * sizeof(char *));
    const char *p = csv;

    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ',') p++;
        /* Trim trailing space */
        const char *end = p;
        while (end > start && isspace((unsigned char)end[-1])) end--;
        size_t len = (size_t)(end - start);
        if (len > 0) {
            if (*count >= cap) {
                cap *= 2;
                result = (char **)realloc(result, cap * sizeof(char *));
            }
            result[*count] = (char *)malloc(len + 1);
            memcpy(result[*count], start, len);
            result[*count][len] = '\0';
            (*count)++;
        }
    }
    return result;
}

void lp_free_strings(char **strs, size_t count) {
    if (!strs) return;
    for (size_t i = 0; i < count; i++) free(strs[i]);
    free(strs);
}

/* ================================================================
 * Platform
 * ================================================================ */

char *lp_path_join(const char *dir, const char *file) {
    size_t dlen = strlen(dir);
    size_t flen = strlen(file);
    bool need_sep = (dlen > 0 && dir[dlen - 1] != '/' && dir[dlen - 1] != '\\');
    char *out = (char *)malloc(dlen + flen + 2);
    memcpy(out, dir, dlen);
    if (need_sep) out[dlen++] = '/';
    memcpy(out + dlen, file, flen);
    out[dlen + flen] = '\0';
    return out;
}

bool lp_file_exists(const char *path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

char *lp_get_exe_dir(void) {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return NULL;
    /* Strip filename to get directory */
    char *last_sep = strrchr(buf, '\\');
    if (!last_sep) last_sep = strrchr(buf, '/');
    if (last_sep) *last_sep = '\0';
    return strdup(buf);
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len < 0) return NULL;
    buf[len] = '\0';
    char *last_sep = strrchr(buf, '/');
    if (last_sep) *last_sep = '\0';
    return strdup(buf);
#endif
}

#ifdef _WIN32

int lp_dir_iter(const char *dir, const char *suffix, lp_dir_cb cb, void *userdata) {
    char *pattern = lp_path_join(dir, "*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) return -1;

    int count = 0;
    size_t suf_len = suffix ? strlen(suffix) : 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (suffix) {
            size_t nlen = strlen(fd.cFileName);
            if (nlen < suf_len) continue;
            if (strcmp(fd.cFileName + nlen - suf_len, suffix) != 0) continue;
        }
        char *full = lp_path_join(dir, fd.cFileName);
        cb(full, userdata);
        free(full);
        count++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return count;
}

int lp_dir_iter_recursive(const char *dir, const char *suffix, lp_dir_cb cb, void *userdata) {
    char *pattern = lp_path_join(dir, "*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) return -1;

    int count = 0;
    size_t suf_len = suffix ? strlen(suffix) : 0;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char *full = lp_path_join(dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            count += lp_dir_iter_recursive(full, suffix, cb, userdata);
        } else {
            if (suffix) {
                size_t nlen = strlen(fd.cFileName);
                if (nlen >= suf_len && strcmp(fd.cFileName + nlen - suf_len, suffix) == 0) {
                    cb(full, userdata);
                    count++;
                }
            } else {
                cb(full, userdata);
                count++;
            }
        }
        free(full);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return count;
}

#else /* POSIX */

int lp_dir_iter(const char *dir, const char *suffix, lp_dir_cb cb, void *userdata) {
    DIR *d = opendir(dir);
    if (!d) return -1;

    int count = 0;
    size_t suf_len = suffix ? strlen(suffix) : 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_type != DT_REG && ent->d_type != DT_UNKNOWN) continue;
        if (suffix) {
            size_t nlen = strlen(ent->d_name);
            if (nlen < suf_len) continue;
            if (strcmp(ent->d_name + nlen - suf_len, suffix) != 0) continue;
        }
        char *full = lp_path_join(dir, ent->d_name);
        cb(full, userdata);
        free(full);
        count++;
    }
    closedir(d);
    return count;
}

int lp_dir_iter_recursive(const char *dir, const char *suffix, lp_dir_cb cb, void *userdata) {
    DIR *d = opendir(dir);
    if (!d) return -1;

    int count = 0;
    size_t suf_len = suffix ? strlen(suffix) : 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char *full = lp_path_join(dir, ent->d_name);
        if (ent->d_type == DT_DIR) {
            count += lp_dir_iter_recursive(full, suffix, cb, userdata);
        } else {
            if (suffix) {
                size_t nlen = strlen(ent->d_name);
                if (nlen >= suf_len && strcmp(ent->d_name + nlen - suf_len, suffix) == 0) {
                    cb(full, userdata);
                    count++;
                }
            } else {
                cb(full, userdata);
                count++;
            }
        }
        free(full);
    }
    closedir(d);
    return count;
}

#endif
