/*
 * fix.c — YAML fix database loader + matching
 *
 * Minimal YAML parser for our fix entry format:
 *   key: "value" or key: value
 *   key: |
 *     multiline value
 *   key: [tag1, tag2]
 */
#include "fix.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <re.h>

/* ---- Minimal YAML parser for fix files ---- */

static void yaml_skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

/* Read a YAML scalar value (rest of line, trimmed, unquoted if needed) */
static char *yaml_read_scalar(const char **p) {
    yaml_skip_ws(p);
    const char *start = *p;

    /* Handle quoted strings */
    if (*start == '"') {
        (*p)++;
        const char *qs = *p;
        while (**p && **p != '"') {
            if (**p == '\\') (*p)++;
            if (**p) (*p)++;
        }
        size_t len = (size_t)(*p - qs);
        char *val = (char *)malloc(len + 1);
        memcpy(val, qs, len);
        val[len] = '\0';
        if (**p == '"') (*p)++;
        return val;
    }

    /* Unquoted: read to end of line */
    while (**p && **p != '\n' && **p != '\r' && **p != '#') (*p)++;
    const char *end = *p;
    while (end > start && isspace((unsigned char)end[-1])) end--;
    size_t len = (size_t)(end - start);
    char *val = (char *)malloc(len + 1);
    memcpy(val, start, len);
    val[len] = '\0';
    return val;
}

/* Read a YAML block scalar (|) — indented multiline */
static char *yaml_read_block(const char **p) {
    /* Skip to next line */
    while (**p && **p != '\n') (*p)++;
    if (**p == '\n') (*p)++;

    lp_string result = lp_string_new(256);
    /* Detect indent level of first content line */
    int base_indent = 0;
    const char *first = *p;
    while (*first == ' ') { base_indent++; first++; }

    while (**p) {
        /* Check indent */
        int indent = 0;
        const char *line_start = *p;
        while (**p == ' ') { indent++; (*p)++; }

        if (**p == '\n' || **p == '\r') {
            /* Blank line within block */
            lp_string_append_cstr(&result, "\n");
            if (**p == '\r') (*p)++;
            if (**p == '\n') (*p)++;
            continue;
        }

        if (indent < base_indent && indent > 0) {
            /* Dedented — end of block */
            *p = line_start;
            break;
        }
        if (indent == 0 && !isspace((unsigned char)**p)) {
            /* Back to top level — end of block */
            *p = line_start;
            break;
        }

        /* Read the line content (skip the base indent) */
        const char *content_start = *p;
        while (**p && **p != '\n' && **p != '\r') (*p)++;
        size_t clen = (size_t)(*p - content_start);
        if (result.len > 0) lp_string_append_cstr(&result, "\n");
        lp_string_append(&result, content_start, clen);

        if (**p == '\r') (*p)++;
        if (**p == '\n') (*p)++;
    }

    char *out = strdup(lp_string_cstr(&result));
    lp_string_free(&result);
    return out;
}

/* Read a YAML flow sequence: [a, b, c] */
static char **yaml_read_flow_seq(const char **p, size_t *count) {
    *count = 0;
    if (**p != '[') return NULL;
    (*p)++;

    size_t cap = 8;
    char **arr = (char **)malloc(cap * sizeof(char *));

    while (**p) {
        yaml_skip_ws(p);
        if (**p == ']') { (*p)++; break; }
        if (**p == ',' || **p == '\n' || **p == '\r') { (*p)++; continue; }

        /* Read item */
        const char *start = *p;
        while (**p && **p != ',' && **p != ']' && **p != '\n') (*p)++;
        const char *end = *p;
        while (end > start && isspace((unsigned char)end[-1])) end--;
        while (start < end && isspace((unsigned char)*start)) start++;

        size_t len = (size_t)(end - start);
        if (len > 0) {
            if (*count >= cap) { cap *= 2; arr = (char **)realloc(arr, cap * sizeof(char *)); }
            arr[*count] = (char *)malloc(len + 1);
            memcpy(arr[*count], start, len);
            arr[*count][len] = '\0';
            (*count)++;
        }
    }
    return arr;
}

lp_fix *lp_fix_load(const char *path) {
    size_t file_len;
    char *data = lp_read_file(path, &file_len);
    if (!data) return NULL;

    lp_fix *f = (lp_fix *)calloc(1, sizeof(lp_fix));
    f->file_path = strdup(path);

    const char *p = data;
    while (*p) {
        /* Skip blank lines and comments */
        yaml_skip_ws(&p);
        if (*p == '#') { while (*p && *p != '\n') p++; if (*p) p++; continue; }
        if (*p == '\n' || *p == '\r') { p++; continue; }
        if (*p == '-' && *(p+1) == '-' && *(p+2) == '-') {
            /* Document separator --- */
            while (*p && *p != '\n') p++;
            if (*p) p++;
            continue;
        }

        /* Read key */
        const char *key_start = p;
        while (*p && *p != ':' && *p != '\n') p++;
        if (*p != ':') { while (*p && *p != '\n') p++; if (*p) p++; continue; }
        size_t key_len = (size_t)(p - key_start);
        while (key_len > 0 && isspace((unsigned char)key_start[key_len - 1])) key_len--;
        char key[64];
        if (key_len >= sizeof(key)) key_len = sizeof(key) - 1;
        memcpy(key, key_start, key_len);
        key[key_len] = '\0';

        p++; /* skip ':' */
        yaml_skip_ws(&p);

        if (strcmp(key, "tags") == 0 && *p == '[') {
            f->tags = yaml_read_flow_seq(&p, &f->tag_count);
        } else if (strcmp(key, "fix") == 0 && *p == '|') {
            f->fix_text = yaml_read_block(&p);
        } else if (strcmp(key, "context") == 0 && *p == '|') {
            f->context = yaml_read_block(&p);
        } else {
            char *val = yaml_read_scalar(&p);
            if (strcmp(key, "pattern") == 0) { free(f->pattern); f->pattern = val; }
            else if (strcmp(key, "regex") == 0) { free(f->regex); f->regex = val; }
            else if (strcmp(key, "fix") == 0) { free(f->fix_text); f->fix_text = val; }
            else if (strcmp(key, "context") == 0) { free(f->context); f->context = val; }
            else if (strcmp(key, "severity") == 0) { free(f->severity); f->severity = val; }
            else if (strcmp(key, "resolved") == 0) { free(f->resolved); f->resolved = val; }
            else if (strcmp(key, "commit_ref") == 0) { free(f->commit_ref); f->commit_ref = val; }
            else free(val);
        }

        /* Advance past newline */
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }

    free(data);
    return f;
}

void lp_fix_free(lp_fix *f) {
    if (!f) return;
    free(f->pattern);
    free(f->regex);
    lp_free_strings(f->tags, f->tag_count);
    free(f->fix_text);
    free(f->context);
    free(f->severity);
    free(f->resolved);
    free(f->commit_ref);
    free(f->file_path);
    free(f);
}

typedef struct {
    lp_fix **fixes;
    size_t   count;
    size_t   cap;
} fix_collector;

static void collect_fix(const char *path, void *userdata) {
    fix_collector *fc = (fix_collector *)userdata;
    lp_fix *f = lp_fix_load(path);
    if (!f) return;
    if (fc->count >= fc->cap) {
        fc->cap = fc->cap ? fc->cap * 2 : 8;
        fc->fixes = (lp_fix **)realloc(fc->fixes, fc->cap * sizeof(lp_fix *));
    }
    fc->fixes[fc->count++] = f;
}

lp_fix **lp_fix_load_dir(const char *dir, size_t *count) {
    fix_collector fc = { NULL, 0, 0 };
    lp_dir_iter_recursive(dir, ".yaml", collect_fix, &fc);
    *count = fc.count;
    return fc.fixes;
}

void lp_fixes_free(lp_fix **fixes, size_t count) {
    for (size_t i = 0; i < count; i++)
        lp_fix_free(fixes[i]);
    free(fixes);
}

/* ---- Fuzzy matching ---- */

/* Normalize for matching: lowercase, strip paths, hex, numbers */
static char *normalize_for_match(const char *text) {
    size_t len = strlen(text);
    char *out = (char *)malloc(len + 1);
    size_t j = 0;
    bool in_path = false;

    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        /* Skip absolute paths */
        if ((c == '/' || c == '\\') && i + 1 < len && (text[i+1] != ' ')) {
            in_path = true;
            continue;
        }
        if (in_path) {
            if (c == ' ' || c == ':' || c == '\n') in_path = false;
            else continue;
        }
        /* Skip hex: 0x... */
        if (c == '0' && i + 1 < len && text[i+1] == 'x') {
            i += 2;
            while (i < len && isxdigit((unsigned char)text[i])) i++;
            i--;
            out[j++] = ' ';
            continue;
        }
        /* Collapse digits */
        if (isdigit((unsigned char)c)) {
            while (i + 1 < len && isdigit((unsigned char)text[i+1])) i++;
            out[j++] = '#';
            continue;
        }
        out[j++] = (char)tolower((unsigned char)c);
    }
    out[j] = '\0';
    return out;
}

/* Longest Common Substring length */
static size_t lcs_length(const char *a, size_t alen, const char *b, size_t blen) {
    if (alen == 0 || blen == 0) return 0;
    /* Use rolling row to save memory */
    size_t *prev = (size_t *)calloc(blen + 1, sizeof(size_t));
    size_t *curr = (size_t *)calloc(blen + 1, sizeof(size_t));
    size_t best = 0;

    for (size_t i = 1; i <= alen; i++) {
        for (size_t j = 1; j <= blen; j++) {
            if (a[i-1] == b[j-1]) {
                curr[j] = prev[j-1] + 1;
                if (curr[j] > best) best = curr[j];
            } else {
                curr[j] = 0;
            }
        }
        size_t *tmp = prev; prev = curr; curr = tmp;
        memset(curr, 0, (blen + 1) * sizeof(size_t));
    }
    free(prev);
    free(curr);
    return best;
}

static int cmp_match_desc(const void *a, const void *b) {
    float fa = ((const lp_fix_match *)a)->confidence;
    float fb = ((const lp_fix_match *)b)->confidence;
    if (fa > fb) return -1;
    if (fa < fb) return 1;
    return 0;
}

lp_fix_match *lp_fix_match_all(const char *error_text, lp_fix **fixes, size_t fix_count,
                                size_t *match_count, float min_confidence) {
    *match_count = 0;
    if (!error_text || fix_count == 0) return NULL;

    char *norm_error = normalize_for_match(error_text);
    size_t norm_len = strlen(norm_error);

    size_t cap = 8;
    lp_fix_match *matches = (lp_fix_match *)malloc(cap * sizeof(lp_fix_match));

    for (size_t i = 0; i < fix_count; i++) {
        float conf = 0.0f;

        /* Try regex match first */
        if (fixes[i]->regex && fixes[i]->regex[0]) {
            re_t pat = re_compile(fixes[i]->regex);
            if (pat) {
                int match_len;
                int idx = re_matchp(pat, error_text, &match_len);
                if (idx >= 0) conf = 0.9f;
            }
        }

        /* Fuzzy: normalized substring / LCS */
        if (conf < 0.5f && fixes[i]->pattern) {
            /* Direct substring check */
            if (lp_str_contains_ci(error_text, fixes[i]->pattern)) {
                conf = 0.85f;
            } else {
                char *norm_pat = normalize_for_match(fixes[i]->pattern);
                size_t pat_len = strlen(norm_pat);
                size_t lcs = lcs_length(norm_error, norm_len, norm_pat, pat_len);
                size_t max_len = norm_len > pat_len ? norm_len : pat_len;
                if (max_len > 0) {
                    float fuzzy = (float)lcs / (float)max_len;
                    if (fuzzy > conf) conf = fuzzy;
                }
                free(norm_pat);
            }
        }

        if (conf >= min_confidence) {
            if (*match_count >= cap) {
                cap *= 2;
                matches = (lp_fix_match *)realloc(matches, cap * sizeof(lp_fix_match));
            }
            matches[*match_count].fix = fixes[i];
            matches[*match_count].confidence = conf;
            (*match_count)++;
        }
    }

    free(norm_error);

    qsort(matches, *match_count, sizeof(lp_fix_match), cmp_match_desc);
    return matches;
}

void lp_fix_matches_free(lp_fix_match *matches, size_t count) {
    (void)count;
    free(matches);
}

bool lp_fix_validate(const lp_fix *f, char *errbuf, size_t errlen) {
    if (!f->pattern || !f->pattern[0]) {
        snprintf(errbuf, errlen, "missing required field: pattern");
        return false;
    }
    if (!f->tags || f->tag_count == 0) {
        snprintf(errbuf, errlen, "missing required field: tags");
        return false;
    }
    if (!f->fix_text || !f->fix_text[0]) {
        snprintf(errbuf, errlen, "missing required field: fix");
        return false;
    }
    return true;
}

int lp_fix_write(const char *path, const lp_fix *f) {
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;

    fprintf(fp, "pattern: \"%s\"\n", f->pattern ? f->pattern : "");
    if (f->regex && f->regex[0])
        fprintf(fp, "regex: \"%s\"\n", f->regex);
    if (f->tags && f->tag_count > 0) {
        fprintf(fp, "tags: [");
        for (size_t i = 0; i < f->tag_count; i++) {
            if (i > 0) fprintf(fp, ", ");
            fprintf(fp, "%s", f->tags[i]);
        }
        fprintf(fp, "]\n");
    }
    if (f->fix_text) {
        fprintf(fp, "fix: |\n");
        /* Indent each line */
        const char *p = f->fix_text;
        while (*p) {
            fprintf(fp, "  ");
            while (*p && *p != '\n') { fputc(*p++, fp); }
            fputc('\n', fp);
            if (*p == '\n') p++;
        }
    }
    if (f->context) fprintf(fp, "context: \"%s\"\n", f->context);
    if (f->resolved) fprintf(fp, "resolved: %s\n", f->resolved);
    if (f->commit_ref) fprintf(fp, "commit_ref: \"%s\"\n", f->commit_ref);
    if (f->severity) fprintf(fp, "severity: %s\n", f->severity);

    fclose(fp);
    return 0;
}

char *lp_fix_find_dir(void) {
    if (lp_file_exists("fixes")) return strdup("fixes");
    const char *env = getenv("LOGPILOT_FIXES");
    if (env && lp_file_exists(env)) return strdup(env);
    return NULL;
}
