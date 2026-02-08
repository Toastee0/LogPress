/*
 * logparse — Semantic build log compression
 * Part of LogPilot toolkit
 *
 * Algorithm:
 *   1. Auto-detect mode (sniff first 50 lines for signatures)
 *   2. Deduplicate and count (hash each line, collapse repeats)
 *   3. Segment detection (identify coherent blocks)
 *   4. Interest scoring (keyword, frequency, error/warning)
 *   5. Budget packing (fill token budget with best segments)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "util.h"
#include "mode.h"
#include "dedup.h"
#include "segment.h"
#include "score.h"
#include "budget.h"
#include "token.h"

#define DEFAULT_BUDGET_LINES 300
#define DEFAULT_FREQ_TOP     10
#define SNIFF_LINES          50
#define DEFAULT_TAIL_LINES   20

/* ---- Help text ---- */

static const char *HELP_TEXT =
    "logparse - Semantic build log compression\n"
    "\n"
    "Usage: logparse [OPTIONS] [FILE]\n"
    "       command | logparse [OPTIONS]\n"
    "\n"
    "Compresses build logs into token-efficient summaries.\n"
    "Reads from FILE, or stdin if no file given.\n"
    "\n"
    "Options:\n"
    "  --mode <name>      Force a specific build system mode\n"
    "  --budget <lines>   Target output size in lines (default: 300)\n"
    "  --keywords <csv>   Additional keywords to score as high-interest\n"
    "  --raw-freq         Show full frequency table, not just top N\n"
    "  --no-tail          Omit final lines of log\n"
    "  --json             Output as JSON\n"
    "  --help             Show this help\n"
    "  --help agent       Machine-readable self-update instructions\n"
    "\n"
    "Examples:\n"
    "  logparse build.log\n"
    "  logparse build.log --mode zephyr --budget 400\n"
    "  west build 2>&1 | logparse --mode zephyr\n";

static const char *HELP_AGENT_TEXT =
    "AGENT SELF-UPDATE INSTRUCTIONS\n"
    "===============================\n"
    "TO ADD A NEW BUILD SYSTEM MODE:\n"
    "  1. Run: logexplore <sample.log> --suggest-mode > modes/draft.toml\n"
    "  2. Edit the generated TOML to refine signatures, triggers, segments\n"
    "  3. Rename to modes/<name>.toml\n"
    "  4. Test: logparse <sample.log> --mode <name> and verify output quality\n"
    "\n"
    "MODE FILE SCHEMA (modes/*.toml):\n"
    "  [mode]\n"
    "  name = \"example\"\n"
    "  description = \"Example build system\"\n"
    "  \n"
    "  [detection]\n"
    "  signatures = [\"BUILD\", \"make\"]\n"
    "  \n"
    "  [dedup]\n"
    "  strip_patterns = [\"\\\"[^\\\"]*\\\"\", \"0x[0-9a-f]+\"]\n"
    "  \n"
    "  [segments]\n"
    "  phase_markers = [\"Configuring\", \"Compiling\", \"Linking\"]\n"
    "  block_triggers = [\"error:\", \"warning:\"]\n"
    "  \n"
    "  [interest]\n"
    "  keywords = [\"FAILED\", \"undefined\"]\n"
    "  error_patterns = [\"error:\", \"fatal:\"]\n"
    "  warning_patterns = [\"warning:\"]\n"
    "\n"
    "FULL SCHEMA: schema/mode.schema.toml\n"
    "EXAMPLES: examples/example-mode.toml, modes/zephyr.toml\n";

/* ---- Argument parsing ---- */

typedef struct {
    const char *input_file;
    const char *mode_name;
    size_t      budget_lines;
    char      **keywords;
    size_t      keyword_count;
    bool        raw_freq;
    bool        no_tail;
    bool        json_output;
    bool        show_help;
    bool        show_help_agent;
} logparse_args;

static logparse_args parse_args(int argc, char **argv) {
    logparse_args args;
    memset(&args, 0, sizeof(args));
    args.budget_lines = DEFAULT_BUDGET_LINES;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            if (i + 1 < argc && strcmp(argv[i + 1], "agent") == 0) {
                args.show_help_agent = true;
                i++;
            } else {
                args.show_help = true;
            }
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            args.mode_name = argv[++i];
        } else if (strcmp(argv[i], "--budget") == 0 && i + 1 < argc) {
            args.budget_lines = (size_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--keywords") == 0 && i + 1 < argc) {
            args.keywords = lp_split_csv(argv[++i], &args.keyword_count);
        } else if (strcmp(argv[i], "--raw-freq") == 0) {
            args.raw_freq = true;
        } else if (strcmp(argv[i], "--no-tail") == 0) {
            args.no_tail = true;
        } else if (strcmp(argv[i], "--json") == 0) {
            args.json_output = true;
        } else if (argv[i][0] != '-') {
            args.input_file = argv[i];
        }
    }
    return args;
}

/* ---- Read all lines from file or stdin ---- */

typedef struct {
    char  **lines;
    size_t  count;
    size_t  cap;
} line_array;

static line_array read_all_lines(FILE *fp) {
    line_array la;
    la.count = 0;
    la.cap = 1024;
    la.lines = (char **)malloc(la.cap * sizeof(char *));

    char *buf = NULL;
    size_t buf_cap = 0;
    int len;
    while ((len = lp_readline(fp, &buf, &buf_cap)) >= 0) {
        if (la.count >= la.cap) {
            la.cap *= 2;
            la.lines = (char **)realloc(la.lines, la.cap * sizeof(char *));
        }
        la.lines[la.count++] = strdup(buf);
    }
    free(buf);
    return la;
}

static void free_line_array(line_array *la) {
    for (size_t i = 0; i < la->count; i++)
        free(la->lines[i]);
    free(la->lines);
    la->lines = NULL;
    la->count = la->cap = 0;
}

/* ---- JSON escaping helper ---- */

static void print_json_string(FILE *out, const char *s) {
    fputc('"', out);
    if (s) {
        for (; *s; s++) {
            switch (*s) {
                case '"':  fputs("\\\"", out); break;
                case '\\': fputs("\\\\", out); break;
                case '\n': fputs("\\n", out);  break;
                case '\r': fputs("\\r", out);  break;
                case '\t': fputs("\\t", out);  break;
                default:
                    if ((unsigned char)*s < 0x20)
                        fprintf(out, "\\u%04x", (unsigned char)*s);
                    else
                        fputc(*s, out);
            }
        }
    }
    fputc('"', out);
}

/* ---- Segment type name ---- */

static const char *seg_type_name(lp_seg_type t) {
    switch (t) {
        case LP_SEG_ERROR:          return "error";
        case LP_SEG_WARNING:        return "warning";
        case LP_SEG_INFO:           return "info";
        case LP_SEG_DATA:           return "data";
        case LP_SEG_PHASE:          return "phase";
        case LP_SEG_BUILD_PROGRESS: return "build";
        case LP_SEG_BOILERPLATE:    return "boilerplate";
        case LP_SEG_NORMAL:         return "block";
    }
    return "unknown";
}

/* ---- Summary fact extraction ---- */

typedef struct {
    char board[256];
    char zephyr_version[64];
    char toolchain[256];
    char overlay[512];
    char memory_flash[128];
    char memory_ram[128];
    char output_file[256];
    size_t total_build_steps;
    size_t max_build_step;
    bool build_failed;
} build_summary;

static void extract_summary(build_summary *s, line_array *la) {
    memset(s, 0, sizeof(*s));

    for (size_t i = 0; i < la->count; i++) {
        const char *line = la->lines[i];

        /* Board */
        if (s->board[0] == '\0') {
            const char *p = strstr(line, "-- Board: ");
            if (p) {
                p += 10;
                const char *end = p;
                while (*end && *end != '\n' && *end != '\r') end++;
                size_t len = (size_t)(end - p);
                if (len >= sizeof(s->board)) len = sizeof(s->board) - 1;
                memcpy(s->board, p, len);
                s->board[len] = '\0';
            }
        }

        /* Zephyr version */
        if (s->zephyr_version[0] == '\0') {
            const char *p = strstr(line, "-- Zephyr version: ");
            if (p) {
                p += 19;
                const char *end = p;
                while (*end && *end != ' ' && *end != '\n') end++;
                size_t len = (size_t)(end - p);
                if (len >= sizeof(s->zephyr_version)) len = sizeof(s->zephyr_version) - 1;
                memcpy(s->zephyr_version, p, len);
                s->zephyr_version[len] = '\0';
            }
        }

        /* Overlay */
        if (s->overlay[0] == '\0') {
            const char *p = strstr(line, "-- Found devicetree overlay: ");
            if (p) {
                p += 29;
                /* Shorten: just keep filename relative to project */
                const char *last_slash = p;
                const char *end = p;
                while (*end && *end != '\n' && *end != '\r') end++;
                /* Find last path separator after common project root indicators */
                const char *short_name = p;
                const char *boards = strstr(p, "boards/");
                if (boards) short_name = boards;
                size_t len = (size_t)(end - short_name);
                if (len >= sizeof(s->overlay)) len = sizeof(s->overlay) - 1;
                memcpy(s->overlay, short_name, len);
                s->overlay[len] = '\0';
                (void)last_slash;
            }
        }

        /* Toolchain version - extract just the compiler */
        if (s->toolchain[0] == '\0') {
            const char *p = strstr(line, "The C compiler identification is ");
            if (p) {
                p += 33;
                const char *end = p;
                while (*end && *end != '\n' && *end != '\r') end++;
                size_t len = (size_t)(end - p);
                if (len >= sizeof(s->toolchain)) len = sizeof(s->toolchain) - 1;
                memcpy(s->toolchain, p, len);
                s->toolchain[len] = '\0';
            }
        }

        /* Memory: FLASH */
        if (s->memory_flash[0] == '\0') {
            const char *p = strstr(line, "FLASH:");
            if (p && strstr(line, "Used Size")) {
                /* This is the header line, skip */
            } else if (p) {
                p += 6;
                while (*p == ' ') p++;
                const char *end = p;
                while (*end && *end != '\n' && *end != '\r') end++;
                size_t len = (size_t)(end - p);
                if (len >= sizeof(s->memory_flash)) len = sizeof(s->memory_flash) - 1;
                memcpy(s->memory_flash, p, len);
                s->memory_flash[len] = '\0';
                /* Trim trailing spaces */
                while (len > 0 && s->memory_flash[len-1] == ' ') s->memory_flash[--len] = '\0';
            }
        }

        /* Memory: RAM */
        if (s->memory_ram[0] == '\0') {
            const char *p = strstr(line, "RAM:");
            if (p && !strstr(line, "Used Size")) {
                p += 4;
                while (*p == ' ') p++;
                const char *end = p;
                while (*end && *end != '\n' && *end != '\r') end++;
                size_t len = (size_t)(end - p);
                if (len >= sizeof(s->memory_ram)) len = sizeof(s->memory_ram) - 1;
                memcpy(s->memory_ram, p, len);
                s->memory_ram[len] = '\0';
                while (len > 0 && s->memory_ram[len-1] == ' ') s->memory_ram[--len] = '\0';
            }
        }

        /* Output file */
        if (s->output_file[0] == '\0') {
            const char *p = strstr(line, "Wrote ");
            if (p && strstr(p, " bytes to ")) {
                const char *end = p;
                while (*end && *end != '\n' && *end != '\r') end++;
                size_t len = (size_t)(end - p);
                if (len >= sizeof(s->output_file)) len = sizeof(s->output_file) - 1;
                memcpy(s->output_file, p, len);
                s->output_file[len] = '\0';
            }
        }

        /* Build step counts */
        if (lp_is_build_progress(line)) {
            const char *p = line;
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == '[') {
                p++;
                size_t current = (size_t)atoi(p);
                while (*p && *p != '/') p++;
                if (*p == '/') {
                    p++;
                    size_t total = (size_t)atoi(p);
                    if (current > s->total_build_steps) s->total_build_steps = current;
                    if (total > s->max_build_step) s->max_build_step = total;
                }
            }
        }

        /* Build failure */
        if (lp_str_contains_ci(line, "ninja: build stopped") ||
            (lp_str_contains(line, "FAILED:") && !lp_str_contains(line, "FAILED: _"))) {
            s->build_failed = true;
        }
        if (lp_str_contains(line, "FATAL ERROR:")) {
            s->build_failed = true;
        }
    }
}

/* ---- Output: plain text ---- */

/* Check if an error segment contains only build-system wrapper noise */
static bool is_wrapper_error(lp_segment *seg) {
    if (seg->type != LP_SEG_ERROR) return false;
    for (size_t l = 0; l < seg->line_count; l++) {
        const char *ln = seg->lines[l];
        if (strstr(ln, "ninja: build stopped") ||
            strstr(ln, "FATAL ERROR:") ||
            strstr(ln, "_sysbuild/sysbuild/images/") ||
            strstr(ln, "cmd.exe /C") ||
            strstr(ln, "cmake.exe --build") ||
            strstr(ln, "cmake.EXE")) {
            continue;  /* wrapper line */
        }
        return false;
    }
    return true;
}

static void output_text(FILE *out, const logparse_args *args,
                        const char *mode_name,
                        line_array *la,
                        lp_dedup_table *dedup,
                        lp_segment *segs, size_t seg_count,
                        lp_budget_result *budget,
                        size_t error_count, size_t warning_count,
                        const struct lp_mode *mode) {

    (void)seg_count;

    /* Extract summary facts from the full log */
    build_summary summary;
    extract_summary(&summary, la);

    /* Count output lines — matches the actual filtering in the output loop */
    size_t output_lines = 0;
    size_t real_error_count = 0;
    for (size_t i = 0; i < budget->count; i++) {
        lp_segment *seg = &segs[budget->indices[i]];
        if (seg->type == LP_SEG_BUILD_PROGRESS || seg->type == LP_SEG_BOILERPLATE)
            continue;
        if (is_wrapper_error(seg)) continue;

        if (seg->type != LP_SEG_ERROR && seg->type != LP_SEG_WARNING) {
            if (seg->score < 3.0f) continue;
        }
        if (seg->type == LP_SEG_ERROR) real_error_count++;

        /* Count non-noise lines within the segment */
        for (size_t l = 0; l < seg->line_count; l++) {
            if (lp_is_build_progress(seg->lines[l])) continue;
            if (lp_is_boilerplate(seg->lines[l], mode)) continue;
            output_lines++;
        }
    }
    /* Add summary header lines */
    output_lines += 6;

    float reduction = la->count > 0
        ? (1.0f - (float)output_lines / (float)la->count) * 100.0f
        : 0.0f;
    if (reduction < 0.0f) reduction = 0.0f;

    /* --- Header --- */
    fprintf(out, "[LOGPARSE] mode: %s | %zu lines -> ~%zu lines (%.1f%% reduction)\n",
            mode_name, la->count, output_lines, reduction);
    fprintf(out, "[STATS] %zu errors | %zu warnings\n",
            real_error_count, warning_count);
    fprintf(out, "\n");

    /* --- Build summary --- */
    if (summary.board[0]) {
        fprintf(out, "  Board: %s", summary.board);
        if (summary.zephyr_version[0])
            fprintf(out, " | Zephyr %s", summary.zephyr_version);
        if (summary.toolchain[0])
            fprintf(out, " | %s", summary.toolchain);
        fprintf(out, "\n");
    }
    if (summary.overlay[0])
        fprintf(out, "  Overlay: %s\n", summary.overlay);

    /* Build steps summary */
    if (summary.max_build_step > 0) {
        if (error_count > 0 || summary.build_failed) {
            fprintf(out, "  Build: FAILED at step %zu/%zu\n",
                    summary.total_build_steps, summary.max_build_step);
        } else {
            fprintf(out, "  Build: %zu/%zu steps OK\n",
                    summary.total_build_steps, summary.max_build_step);
        }
    }

    /* Memory summary */
    if (summary.memory_flash[0]) {
        fprintf(out, "  FLASH: %s\n", summary.memory_flash);
    }
    if (summary.memory_ram[0]) {
        fprintf(out, "  RAM:   %s\n", summary.memory_ram);
    }
    if (summary.output_file[0]) {
        fprintf(out, "  Output: %s\n", summary.output_file);
    }
    fprintf(out, "\n");

    /* --- Frequency table: only if genuinely interesting (3+ repeats) --- */
    size_t sorted_count;
    lp_dedup_entry **sorted = lp_dedup_sorted(dedup, &sorted_count);
    size_t freq_top = args->raw_freq ? sorted_count : DEFAULT_FREQ_TOP;
    if (freq_top > sorted_count) freq_top = sorted_count;
    size_t freq_shown = 0;
    for (size_t i = 0; i < freq_top; i++) {
        if (sorted[i]->count < 3 && !args->raw_freq) continue;
        /* Skip boilerplate/progress noise */
        if (lp_is_build_progress(sorted[i]->original)) continue;
        if (lp_is_blank(sorted[i]->original)) continue;
        /* Skip lines that are just decorative */
        const char *trimmed = sorted[i]->original;
        while (*trimmed == ' ' || *trimmed == '-' || *trimmed == '*') trimmed++;
        if (*trimmed == '\0') continue;
        fprintf(out, "[FREQ x%zu] %s\n", sorted[i]->count, sorted[i]->original);
        freq_shown++;
    }
    if (freq_shown > 0) fprintf(out, "\n");

    /* --- Segments --- */
    for (size_t b = 0; b < budget->count; b++) {
        size_t si = budget->indices[b];
        lp_segment *seg = &segs[si];

        /* Skip build progress and boilerplate — already captured in summary */
        if (seg->type == LP_SEG_BUILD_PROGRESS || seg->type == LP_SEG_BOILERPLATE)
            continue;

        /* Skip wrapper error segments (ninja/cmake build system noise) */
        if (is_wrapper_error(seg)) continue;

        /* For non-error/warning segments, only show if high-scoring and not
           something we already captured in the summary */
        if (seg->type != LP_SEG_ERROR && seg->type != LP_SEG_WARNING) {
            if (seg->score < 3.0f) continue;
            /* Skip segments whose content is already in the summary */
            bool all_summarized = true;
            for (size_t l = 0; l < seg->line_count; l++) {
                const char *ln = seg->lines[l];
                if (lp_is_blank(ln)) continue;
                if (lp_is_boilerplate(ln, mode)) continue;
                /* Already in summary? */
                if (strstr(ln, "FLASH:") || strstr(ln, "RAM:") ||
                    strstr(ln, "IDT_LIST:") || strstr(ln, "Used Size") ||
                    strstr(ln, "Memory region") ||
                    strstr(ln, "Wrote ") || strstr(ln, "Converted to uf2") ||
                    strstr(ln, "Generating files from") ||
                    strstr(ln, "merged.hex") ||
                    lp_is_build_progress(ln)) {
                    continue;
                }
                all_summarized = false;
                break;
            }
            if (all_summarized) continue;
        }

        fprintf(out, "[%s] lines %zu-%zu\n",
                seg_type_name(seg->type),
                seg->start_line + 1, seg->end_line + 1);

        for (size_t l = 0; l < seg->line_count; l++) {
            const char *line = seg->lines[l];

            /* Skip noise lines within segments */
            if (lp_is_build_progress(line)) continue;
            if (lp_is_boilerplate(line, mode)) continue;
            if (seg->type != LP_SEG_ERROR && seg->type != LP_SEG_WARNING) {
                if (lp_is_blank(line)) continue;
            }

            /* Show dedup count for repeated lines */
            size_t line_len = strlen(line);
            uint64_t h = lp_fnv1a(line, line_len);
            size_t idx = (size_t)(h & (dedup->capacity - 1));
            size_t dup_count = 1;
            size_t line_num = seg->start_line + l;
            while (dedup->buckets[idx].occupied) {
                if (dedup->buckets[idx].hash == h &&
                    strcmp(dedup->buckets[idx].original, line) == 0) {
                    dup_count = dedup->buckets[idx].count;
                    break;
                }
                idx = (idx + 1) & (dedup->capacity - 1);
            }
            if (dup_count > 1 && line_num == dedup->buckets[idx].first_line) {
                fprintf(out, "  [x%zu] %s\n", dup_count, line);
            } else if (dup_count <= 1) {
                fprintf(out, "  %s\n", line);
            }
        }
        fprintf(out, "\n");
    }

    free(sorted);
}

/* ---- Output: JSON ---- */

static void output_json(FILE *out, const logparse_args *args,
                        const char *mode_name,
                        line_array *la,
                        lp_dedup_table *dedup,
                        lp_segment *segs, size_t seg_count,
                        lp_budget_result *budget,
                        size_t error_count, size_t warning_count) {
    (void)seg_count;

    build_summary summary;
    extract_summary(&summary, la);

    size_t compressed_lines = 0;
    for (size_t i = 0; i < budget->count; i++)
        compressed_lines += segs[budget->indices[i]].line_count;

    float reduction = la->count > 0
        ? (1.0f - (float)compressed_lines / (float)la->count) * 100.0f
        : 0.0f;

    fprintf(out, "{\n");
    fprintf(out, "  \"mode\": \"%s\",\n", mode_name);
    fprintf(out, "  \"total_lines\": %zu,\n", la->count);
    fprintf(out, "  \"compressed_lines\": %zu,\n", compressed_lines);
    fprintf(out, "  \"reduction_pct\": %.1f,\n", reduction);
    fprintf(out, "  \"error_blocks\": %zu,\n", error_count);
    fprintf(out, "  \"warning_blocks\": %zu,\n", warning_count);

    /* Summary */
    fprintf(out, "  \"summary\": {\n");
    if (summary.board[0]) {
        fprintf(out, "    \"board\": ");
        print_json_string(out, summary.board);
        fprintf(out, ",\n");
    }
    if (summary.zephyr_version[0]) {
        fprintf(out, "    \"zephyr_version\": ");
        print_json_string(out, summary.zephyr_version);
        fprintf(out, ",\n");
    }
    if (summary.memory_flash[0]) {
        fprintf(out, "    \"flash\": ");
        print_json_string(out, summary.memory_flash);
        fprintf(out, ",\n");
    }
    if (summary.memory_ram[0]) {
        fprintf(out, "    \"ram\": ");
        print_json_string(out, summary.memory_ram);
        fprintf(out, ",\n");
    }
    fprintf(out, "    \"build_steps\": %zu,\n", summary.max_build_step);
    fprintf(out, "    \"build_failed\": %s\n", summary.build_failed ? "true" : "false");
    fprintf(out, "  },\n");

    /* Frequency table */
    size_t sorted_count;
    lp_dedup_entry **sorted = lp_dedup_sorted(dedup, &sorted_count);
    size_t freq_top = args->raw_freq ? sorted_count : DEFAULT_FREQ_TOP;
    if (freq_top > sorted_count) freq_top = sorted_count;

    fprintf(out, "  \"frequency\": [\n");
    bool first = true;
    for (size_t i = 0; i < freq_top; i++) {
        if (sorted[i]->count <= 1 && !args->raw_freq) continue;
        if (!first) fprintf(out, ",\n");
        fprintf(out, "    {\"count\": %zu, \"line\": ", sorted[i]->count);
        print_json_string(out, sorted[i]->original);
        fprintf(out, "}");
        first = false;
    }
    fprintf(out, "\n  ],\n");

    /* Segments — only errors/warnings */
    fprintf(out, "  \"segments\": [\n");
    first = true;
    for (size_t b = 0; b < budget->count; b++) {
        size_t si = budget->indices[b];
        lp_segment *seg = &segs[si];
        if (seg->type == LP_SEG_BOILERPLATE || seg->type == LP_SEG_BUILD_PROGRESS)
            continue;

        if (!first) fprintf(out, ",\n");
        first = false;
        fprintf(out, "    {\n");
        fprintf(out, "      \"type\": \"%s\",\n", seg_type_name(seg->type));
        fprintf(out, "      \"start_line\": %zu,\n", seg->start_line + 1);
        fprintf(out, "      \"end_line\": %zu,\n", seg->end_line + 1);
        fprintf(out, "      \"score\": %.1f,\n", seg->score);
        fprintf(out, "      \"lines\": [\n");
        for (size_t l = 0; l < seg->line_count; l++) {
            if (l > 0) fprintf(out, ",\n");
            fprintf(out, "        ");
            print_json_string(out, seg->lines[l]);
        }
        fprintf(out, "\n      ]\n");
        fprintf(out, "    }");
    }
    fprintf(out, "\n  ]\n");

    fprintf(out, "}\n");

    free(sorted);
}

/* ---- Main ---- */

int main(int argc, char **argv) {
    logparse_args args = parse_args(argc, argv);

    if (args.show_help_agent) {
        fputs(HELP_AGENT_TEXT, stdout);
        return 0;
    }
    if (args.show_help) {
        fputs(HELP_TEXT, stdout);
        return 0;
    }

    /* Open input */
    FILE *fp;
    if (args.input_file) {
        fp = fopen(args.input_file, "r");
        if (!fp) {
            fprintf(stderr, "logparse: cannot open '%s'\n", args.input_file);
            return 1;
        }
    } else {
        fp = stdin;
    }

    /* Read all lines */
    line_array la = read_all_lines(fp);
    if (args.input_file) fclose(fp);

    if (la.count == 0) {
        fprintf(stderr, "logparse: empty input\n");
        free_line_array(&la);
        return 1;
    }

    /* Load modes */
    char *mode_dir = lp_mode_find_dir();
    lp_mode **modes = NULL;
    size_t mode_count = 0;
    if (mode_dir) {
        modes = lp_mode_load_dir(mode_dir, &mode_count);
        free(mode_dir);
    }

    /* Detect or select mode */
    const char *mode_name = "generic";
    lp_mode *active_mode = NULL;

    if (args.mode_name) {
        mode_name = args.mode_name;
        active_mode = lp_mode_find(modes, mode_count, args.mode_name);
        if (!active_mode) {
            fprintf(stderr, "logparse: warning: mode '%s' not found, using generic\n",
                    args.mode_name);
            mode_name = "generic";
        }
    } else if (mode_count > 0) {
        size_t sniff = la.count < SNIFF_LINES ? la.count : SNIFF_LINES;
        mode_name = lp_mode_detect((const char **)la.lines, sniff,
                                    modes, mode_count);
        active_mode = lp_mode_find(modes, mode_count, mode_name);
    }

    /* Get strip patterns from mode */
    const char **strip_pats = NULL;
    size_t strip_count = 0;
    if (active_mode) {
        strip_pats = (const char **)active_mode->strip_patterns;
        strip_count = active_mode->strip_count;
    }

    /* Step 1: Deduplication */
    lp_dedup_table dedup;
    lp_dedup_init(&dedup, la.count / 2 + 64);
    for (size_t i = 0; i < la.count; i++) {
        lp_dedup_insert(&dedup, la.lines[i], i, strip_pats, strip_count);
    }

    /* Step 2: Segment detection */
    size_t seg_count;
    lp_segment *segs = lp_segment_detect((const char **)la.lines, la.count,
                                          (const struct lp_mode *)active_mode,
                                          &seg_count);

    /* Step 3: Scoring */
    lp_score_all(segs, seg_count,
                 (const struct lp_mode *)active_mode,
                 (const char **)args.keywords, args.keyword_count,
                 &dedup);

    /* Count error/warning segments */
    size_t error_count = 0, warning_count = 0;
    for (size_t i = 0; i < seg_count; i++) {
        if (segs[i].type == LP_SEG_ERROR) error_count++;
        if (segs[i].type == LP_SEG_WARNING) warning_count++;
    }

    /* Step 4: Budget packing */
    size_t budget_tokens = args.budget_lines * 10;
    size_t reserve_tokens = 200;

    lp_budget_result budget = lp_budget_pack(segs, seg_count,
                                              budget_tokens, reserve_tokens);

    /* Step 5: Output */
    if (args.json_output) {
        output_json(stdout, &args, mode_name, &la, &dedup,
                    segs, seg_count, &budget, error_count, warning_count);
    } else {
        output_text(stdout, &args, mode_name, &la, &dedup,
                    segs, seg_count, &budget, error_count, warning_count,
                    active_mode);
    }

    /* Cleanup */
    lp_budget_result_free(&budget);
    lp_segments_free(segs, seg_count);
    lp_dedup_free(&dedup);
    if (modes) lp_modes_free(modes, mode_count);
    if (args.keywords) lp_free_strings(args.keywords, args.keyword_count);
    free_line_array(&la);

    return 0;
}
