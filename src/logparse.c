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
        case LP_SEG_ERROR:   return "error";
        case LP_SEG_WARNING: return "warning";
        case LP_SEG_INFO:    return "info";
        case LP_SEG_DATA:    return "data";
        case LP_SEG_PHASE:   return "phase";
        case LP_SEG_NORMAL:  return "block";
    }
    return "unknown";
}

/* ---- Output: plain text ---- */

static void output_text(FILE *out, const logparse_args *args,
                        const char *mode_name,
                        line_array *la,
                        lp_dedup_table *dedup,
                        lp_segment *segs, size_t seg_count,
                        lp_budget_result *budget,
                        size_t error_count, size_t warning_count) {
    /* Stats header */
    size_t compressed_lines = 0;
    for (size_t i = 0; i < budget->count; i++)
        compressed_lines += segs[budget->indices[i]].line_count;

    float reduction = la->count > 0
        ? (1.0f - (float)compressed_lines / (float)la->count) * 100.0f
        : 0.0f;

    fprintf(out, "[LOGPARSE] mode: %s | %zu lines -> %zu lines (%.1f%% reduction)\n",
            mode_name, la->count, compressed_lines, reduction);

    /* Count unique repeated and frequency stats */
    size_t sorted_count;
    lp_dedup_entry **sorted = lp_dedup_sorted(dedup, &sorted_count);
    size_t repeat_count = 0;
    for (size_t i = 0; i < sorted_count; i++) {
        if (sorted[i]->count > 1) repeat_count++;
    }

    fprintf(out, "[STATS] %zu unique repeated patterns | %zu error blocks | %zu warning segments\n",
            repeat_count, error_count, warning_count);

    /* Phase detection line */
    bool has_phases = false;
    for (size_t i = 0; i < seg_count; i++) {
        if (segs[i].type == LP_SEG_PHASE) { has_phases = true; break; }
    }
    if (has_phases) {
        fprintf(out, "[STATS] Build phases detected:");
        for (size_t i = 0; i < seg_count; i++) {
            if (segs[i].type == LP_SEG_PHASE && segs[i].line_count > 0) {
                fprintf(out, " %s,", segs[i].lines[0]);
            }
        }
        fprintf(out, "\n");
    }

    fprintf(out, "\n");

    /* Frequency table */
    size_t freq_top = args->raw_freq ? sorted_count : DEFAULT_FREQ_TOP;
    if (freq_top > sorted_count) freq_top = sorted_count;
    size_t freq_shown = 0;
    for (size_t i = 0; i < freq_top; i++) {
        if (sorted[i]->count <= 1 && !args->raw_freq) continue;
        fprintf(out, "[FREQ x%zu] %s\n", sorted[i]->count, sorted[i]->original);
        freq_shown++;
    }
    if (freq_shown > 0) fprintf(out, "\n");

    /* Packed segments */
    for (size_t b = 0; b < budget->count; b++) {
        size_t si = budget->indices[b];
        lp_segment *seg = &segs[si];

        fprintf(out, "[SEGMENT: %s] lines %zu-%zu",
                seg_type_name(seg->type), seg->start_line + 1, seg->end_line + 1);
        if (seg->label && strcmp(seg->label, seg_type_name(seg->type)) != 0)
            fprintf(out, " (%s)", seg->label);
        fprintf(out, "\n");

        for (size_t l = 0; l < seg->line_count; l++) {
            size_t line_num = seg->start_line + l;
            const char *line = seg->lines[l];
            size_t line_len = strlen(line);
            uint64_t h = lp_fnv1a(line, line_len);
            size_t idx = (size_t)(h & (dedup->capacity - 1));
            size_t dup_count = 1;
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

    /* Tail */
    if (!args->no_tail && la->count > 0) {
        size_t tail_start = la->count > DEFAULT_TAIL_LINES
                          ? la->count - DEFAULT_TAIL_LINES : 0;
        bool tail_covered = false;
        for (size_t b = 0; b < budget->count; b++) {
            size_t si = budget->indices[b];
            if (segs[si].end_line >= la->count - 1 &&
                segs[si].start_line <= tail_start) {
                tail_covered = true;
                break;
            }
        }
        if (!tail_covered) {
            fprintf(out, "[TAIL: last %zu lines]\n", la->count - tail_start);
            for (size_t i = tail_start; i < la->count; i++)
                fprintf(out, "  %s\n", la->lines[i]);
        }
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

    /* Segments */
    fprintf(out, "  \"segments\": [\n");
    for (size_t b = 0; b < budget->count; b++) {
        size_t si = budget->indices[b];
        lp_segment *seg = &segs[si];

        if (b > 0) fprintf(out, ",\n");
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
                    segs, seg_count, &budget, error_count, warning_count);
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
