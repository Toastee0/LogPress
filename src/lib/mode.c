/*
 * mode.c — TOML mode loader + auto-detect
 *
 * Uses a minimal hand-rolled TOML parser sufficient for our mode files.
 * We only need: [section], key = "value", key = ["a", "b", "c"]
 */
#include "mode.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* ---- Minimal TOML parser ---- */

static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static void skip_line(const char **p) {
    while (**p && **p != '\n') (*p)++;
    if (**p == '\n') (*p)++;
}

/* Parse a quoted string. Returns malloc'd string. Advances p past closing quote. */
static char *parse_string(const char **p) {
    if (**p != '"') return NULL;
    (*p)++; /* skip opening quote */
    const char *start = *p;
    while (**p && **p != '"' && **p != '\n') {
        if (**p == '\\') (*p)++; /* skip escape */
        if (**p) (*p)++;
    }
    size_t len = (size_t)(*p - start);
    char *s = (char *)malloc(len + 1);
    /* Process escapes */
    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            i++;
            switch (start[i]) {
                case 'n': s[out++] = '\n'; break;
                case 't': s[out++] = '\t'; break;
                case '\\': s[out++] = '\\'; break;
                case '"': s[out++] = '"'; break;
                default: s[out++] = start[i]; break;
            }
        } else {
            s[out++] = start[i];
        }
    }
    s[out] = '\0';
    if (**p == '"') (*p)++;
    return s;
}

/* Parse a string array: ["a", "b", "c"]
   Returns malloc'd array of malloc'd strings. Sets *count. */
static char **parse_string_array(const char **p, size_t *count) {
    *count = 0;
    if (**p != '[') return NULL;
    (*p)++;

    size_t cap = 8;
    char **arr = (char **)malloc(cap * sizeof(char *));

    while (**p) {
        skip_ws(p);
        if (**p == ']') { (*p)++; break; }
        if (**p == '#') { skip_line(p); continue; }
        if (**p == ',') { (*p)++; continue; }
        if (**p == '\n') { (*p)++; continue; }
        if (**p == '"') {
            char *s = parse_string(p);
            if (s) {
                if (*count >= cap) {
                    cap *= 2;
                    arr = (char **)realloc(arr, cap * sizeof(char *));
                }
                arr[(*count)++] = s;
            }
        } else {
            (*p)++;
        }
    }
    return arr;
}

/* Assign a string array to a mode field */
static void assign_array(const char *section, const char *key, char **values, size_t count,
                         lp_mode *m) {
    char **dst = NULL;
    size_t *dst_count = NULL;

    if (strcmp(section, "detection") == 0 && strcmp(key, "signatures") == 0) {
        dst = NULL; m->signatures = values; m->sig_count = count; return;
    }
    if (strcmp(section, "dedup") == 0 && strcmp(key, "strip_patterns") == 0) {
        m->strip_patterns = values; m->strip_count = count; return;
    }
    if (strcmp(section, "segments") == 0) {
        if (strcmp(key, "phase_markers") == 0) {
            m->phase_markers = values; m->phase_count = count; return;
        }
        if (strcmp(key, "block_triggers") == 0) {
            m->block_triggers = values; m->trigger_count = count; return;
        }
        if (strcmp(key, "boilerplate_patterns") == 0) {
            m->boilerplate_patterns = values; m->boilerplate_count = count; return;
        }
    }
    if (strcmp(section, "interest") == 0) {
        if (strcmp(key, "keywords") == 0) {
            m->keywords = values; m->keyword_count = count; return;
        }
        if (strcmp(key, "error_patterns") == 0) {
            m->error_patterns = values; m->error_count = count; return;
        }
        if (strcmp(key, "warning_patterns") == 0) {
            m->warning_patterns = values; m->warning_count = count; return;
        }
    }
    /* Unrecognized — free */
    (void)dst;
    (void)dst_count;
    lp_free_strings(values, count);
}

lp_mode *lp_mode_load(const char *path) {
    size_t file_len;
    char *data = lp_read_file(path, &file_len);
    if (!data) return NULL;

    lp_mode *m = (lp_mode *)calloc(1, sizeof(lp_mode));
    char section[64] = "";

    const char *p = data;
    while (*p) {
        skip_ws(&p);
        if (!*p) break;

        /* Comment */
        if (*p == '#') { skip_line(&p); continue; }

        /* Section header */
        if (*p == '[') {
            p++;
            const char *start = p;
            while (*p && *p != ']' && *p != '\n') p++;
            size_t len = (size_t)(p - start);
            if (len >= sizeof(section)) len = sizeof(section) - 1;
            memcpy(section, start, len);
            section[len] = '\0';
            if (*p == ']') p++;
            skip_line(&p);
            continue;
        }

        /* Key = Value */
        const char *key_start = p;
        while (*p && *p != '=' && *p != '\n') p++;
        if (*p != '=') { skip_line(&p); continue; }
        size_t key_len = (size_t)(p - key_start);
        /* Trim trailing whitespace from key */
        while (key_len > 0 && isspace((unsigned char)key_start[key_len - 1])) key_len--;
        char key[64];
        if (key_len >= sizeof(key)) key_len = sizeof(key) - 1;
        memcpy(key, key_start, key_len);
        key[key_len] = '\0';

        p++; /* skip '=' */
        skip_ws(&p);

        if (*p == '"') {
            /* String value */
            char *val = parse_string(&p);
            if (val) {
                if (strcmp(section, "mode") == 0 && strcmp(key, "name") == 0) {
                    free(m->name); m->name = val;
                } else if (strcmp(section, "mode") == 0 && strcmp(key, "description") == 0) {
                    free(m->description); m->description = val;
                } else if (strcmp(section, "segments") == 0 && strcmp(key, "progress_pattern") == 0) {
                    free(m->progress_pattern); m->progress_pattern = val;
                } else {
                    free(val);
                }
            }
        } else if (*p == '[') {
            /* Array value */
            size_t count;
            char **arr = parse_string_array(&p, &count);
            if (arr) {
                assign_array(section, key, arr, count, m);
            }
        }

        skip_line(&p);
    }

    free(data);
    return m;
}

void lp_mode_free(lp_mode *m) {
    if (!m) return;
    free(m->name);
    free(m->description);
    lp_free_strings(m->signatures, m->sig_count);
    lp_free_strings(m->strip_patterns, m->strip_count);
    lp_free_strings(m->phase_markers, m->phase_count);
    lp_free_strings(m->block_triggers, m->trigger_count);
    lp_free_strings(m->keywords, m->keyword_count);
    lp_free_strings(m->error_patterns, m->error_count);
    lp_free_strings(m->warning_patterns, m->warning_count);
    lp_free_strings(m->boilerplate_patterns, m->boilerplate_count);
    free(m->progress_pattern);
    free(m);
}

/* Callback for directory iteration */
typedef struct {
    lp_mode **modes;
    size_t    count;
    size_t    cap;
} mode_collector;

static void collect_mode(const char *path, void *userdata) {
    mode_collector *mc = (mode_collector *)userdata;
    lp_mode *m = lp_mode_load(path);
    if (!m) return;
    if (mc->count >= mc->cap) {
        mc->cap = mc->cap ? mc->cap * 2 : 8;
        mc->modes = (lp_mode **)realloc(mc->modes, mc->cap * sizeof(lp_mode *));
    }
    mc->modes[mc->count++] = m;
}

lp_mode **lp_mode_load_dir(const char *dir, size_t *count) {
    mode_collector mc = { NULL, 0, 0 };
    lp_dir_iter(dir, ".toml", collect_mode, &mc);
    *count = mc.count;
    return mc.modes;
}

void lp_modes_free(lp_mode **modes, size_t count) {
    for (size_t i = 0; i < count; i++)
        lp_mode_free(modes[i]);
    free(modes);
}

const char *lp_mode_detect(const char **first_lines, size_t line_count,
                           lp_mode **modes, size_t mode_count) {
    const char *best_name = "generic";
    int best_score = 0;

    for (size_t m = 0; m < mode_count; m++) {
        if (!modes[m]->signatures || modes[m]->sig_count == 0) continue;
        int score = 0;
        for (size_t l = 0; l < line_count; l++) {
            for (size_t s = 0; s < modes[m]->sig_count; s++) {
                if (lp_str_contains(first_lines[l], modes[m]->signatures[s]))
                    score++;
            }
        }
        if (score > best_score) {
            best_score = score;
            best_name = modes[m]->name;
        }
    }
    return best_name;
}

lp_mode *lp_mode_find(lp_mode **modes, size_t mode_count, const char *name) {
    for (size_t i = 0; i < mode_count; i++) {
        if (modes[i]->name && strcmp(modes[i]->name, name) == 0)
            return modes[i];
    }
    return NULL;
}

char *lp_mode_find_dir(void) {
    /* Try ./modes first */
    if (lp_file_exists("modes")) return strdup("modes");

    /* Try $LOGPILOT_MODES */
    const char *env = getenv("LOGPILOT_MODES");
    if (env && lp_file_exists(env)) return strdup(env);

    /* Try relative to executable */
    char *exe_dir = lp_get_exe_dir();
    if (exe_dir) {
        /* Try <exe_dir>/modes */
        char *p = lp_path_join(exe_dir, "modes");
        if (lp_file_exists(p)) { free(exe_dir); return p; }
        free(p);
        /* Try <exe_dir>/../modes (exe in build/, modes next to it) */
        p = lp_path_join(exe_dir, "../modes");
        if (lp_file_exists(p)) { free(exe_dir); return p; }
        free(p);
        free(exe_dir);
    }

    /* Try global ~/.logpilot/modes */
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
#else
    const char *home = getenv("HOME");
#endif
    if (home) {
        char *p = lp_path_join(home, ".logpilot/modes");
        if (lp_file_exists(p)) return p;
        free(p);
    }

    return NULL;
}
