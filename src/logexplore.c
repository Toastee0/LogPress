/*
 * logexplore — Structure discovery for unfamiliar logs
 * Part of LogPilot toolkit
 *
 * Analyzes log files to reveal structure: phases, frequency tables,
 * segment boundaries, and encoding info. Used before creating new
 * logparse modes.
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
#include "token.h"

#define DEFAULT_TOP     15
#define SNIFF_LINES     50

/* ---- Help text ---- */

static const char *HELP_TEXT =
    "logexplore - Structure discovery for unfamiliar logs\n"
    "\n"
    "Usage: logexplore [OPTIONS] <FILE>\n"
    "\n"
    "Analyzes log files to reveal structure, frequency patterns,\n"
    "and segment boundaries. Use before creating new logparse modes.\n"
    "\n"
    "Options:\n"
    "  --show-freq        Full frequency table\n"
    "  --show-segments    All detected segments with preview\n"
    "  --show-phases      Phase boundary analysis only\n"
    "  --top <N>          Number of frequency entries to show (default: 15)\n"
    "  --suggest-mode     Output a draft TOML mode file based on analysis\n"
    "  --help             Show this help\n"
    "  --help agent       Machine-readable self-update instructions\n"
    "\n"
    "Examples:\n"
    "  logexplore build.log\n"
    "  logexplore build.log --show-freq --top 20\n"
    "  logexplore build.log --suggest-mode > modes/draft.toml\n";

static const char *HELP_AGENT_TEXT =
    "AGENT SELF-UPDATE INSTRUCTIONS\n"
    "===============================\n"
    "TO ADD NEW FORMAT SIGNATURES:\n"
    "  1. Edit: modes/generic.toml -> [detection] -> signatures\n"
    "  2. Or create a new mode file (see logparse --help agent)\n"
    "\n"
    "TO IMPROVE SEGMENT DETECTION HEURISTICS:\n"
    "  1. Segment detection uses these signals:\n"
    "     - Blank line boundaries\n"
    "     - Indentation level changes (>2 level shift)\n"
    "     - Mode-specific phase markers\n"
    "     - Tabular data detection (consistent column alignment)\n"
    "  2. To add custom heuristics, add to [segments] in mode TOML:\n"
    "     segment_start_patterns = [\"^=+$\", \"^-+$\"]\n"
    "     segment_end_patterns = [\"^$\"]\n"
    "\n"
    "TO REGISTER A NEW LOG FORMAT:\n"
    "  1. Run: logexplore <sample.log> --suggest-mode\n"
    "  2. Review and edit the generated TOML\n"
    "  3. Save to modes/<name>.toml\n"
    "  4. Test: logparse <sample.log> --mode <name>\n";

/* ---- Argument parsing ---- */

typedef struct {
    const char *input_file;
    size_t      top_n;
    bool        show_freq;
    bool        show_segments;
    bool        show_phases;
    bool        suggest_mode;
    bool        show_help;
    bool        show_help_agent;
} logexplore_args;

static logexplore_args parse_args(int argc, char **argv) {
    logexplore_args args;
    memset(&args, 0, sizeof(args));
    args.top_n = DEFAULT_TOP;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            if (i + 1 < argc && strcmp(argv[i + 1], "agent") == 0) {
                args.show_help_agent = true;
                i++;
            } else {
                args.show_help = true;
            }
        } else if (strcmp(argv[i], "--show-freq") == 0) {
            args.show_freq = true;
        } else if (strcmp(argv[i], "--show-segments") == 0) {
            args.show_segments = true;
        } else if (strcmp(argv[i], "--show-phases") == 0) {
            args.show_phases = true;
        } else if (strcmp(argv[i], "--suggest-mode") == 0) {
            args.suggest_mode = true;
        } else if (strcmp(argv[i], "--top") == 0 && i + 1 < argc) {
            args.top_n = (size_t)atoi(argv[++i]);
        } else if (argv[i][0] != '-') {
            args.input_file = argv[i];
        }
    }
    return args;
}

/* ---- Read all lines ---- */

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

/* ---- Encoding analysis ---- */

static void analyze_encoding(FILE *out, line_array *la) {
    size_t longest = 0;
    size_t total_len = 0;
    bool all_ascii = true;

    for (size_t i = 0; i < la->count; i++) {
        size_t len = strlen(la->lines[i]);
        total_len += len;
        if (len > longest) longest = len;
        for (size_t j = 0; j < len; j++) {
            if ((unsigned char)la->lines[i][j] > 127) {
                all_ascii = false;
            }
        }
    }

    size_t avg = la->count > 0 ? total_len / la->count : 0;
    fprintf(out, "[ENCODING] %s | longest line: %zu chars | avg: %zu chars\n",
            all_ascii ? "ASCII" : "UTF-8", longest, avg);
}

/* ---- Phase boundary detection ---- */

typedef struct {
    size_t start_line;
    size_t end_line;
    char   label[128];
} phase_info;

static void detect_phases(FILE *out, line_array *la, lp_segment *segs,
                          size_t seg_count, bool detailed) {
    /* Identify phase boundaries from segments */
    fprintf(out, "\n[PHASE BOUNDARIES] (detected by blank lines + pattern shifts)\n");

    /* Group consecutive segments into phases separated by large gaps or phase markers */
    size_t phase_num = 0;
    size_t phase_start = 0;

    for (size_t i = 0; i < seg_count; i++) {
        bool new_phase = false;

        if (i == 0) {
            new_phase = true;
        } else if (segs[i].type == LP_SEG_PHASE) {
            new_phase = true;
        } else if (segs[i].start_line > segs[i-1].end_line + 10) {
            /* Large gap between segments */
            new_phase = true;
        }

        if (new_phase && i > 0) {
            /* End previous phase */
            phase_num++;
        }

        if (new_phase) {
            phase_start = segs[i].start_line;
            size_t phase_end = segs[i].end_line;
            /* Extend phase to next boundary */
            for (size_t j = i + 1; j < seg_count; j++) {
                if (segs[j].type == LP_SEG_PHASE) break;
                if (segs[j].start_line > segs[j-1].end_line + 10) break;
                phase_end = segs[j].end_line;
                i = j;
            }

            /* Get a label from the first line */
            char label[128] = "";
            if (phase_start < la->count) {
                const char *first = la->lines[phase_start];
                size_t llen = strlen(first);
                if (llen > 100) llen = 100;
                /* Trim leading whitespace for label */
                while (*first && isspace((unsigned char)*first)) { first++; llen--; }
                snprintf(label, sizeof(label), "%.*s", (int)llen, first);
            }

            fprintf(out, "  Phase %zu: lines %zu-%zu      (%s)\n",
                    phase_num + 1, phase_start + 1, phase_end + 1, label);

            if (detailed && phase_start < la->count) {
                /* Show first 3 lines */
                size_t preview = 3;
                if (phase_start + preview > la->count)
                    preview = la->count - phase_start;
                for (size_t p = 0; p < preview; p++) {
                    fprintf(out, "    | %s\n", la->lines[phase_start + p]);
                }
            }
        }
    }
}

/* ---- Suggest mode ---- */

static void suggest_mode_toml(FILE *out, line_array *la, lp_dedup_table *dedup,
                              lp_segment *segs, size_t seg_count) {
    (void)dedup;

    fprintf(out, "# Draft mode generated by logexplore\n");
    fprintf(out, "# Review and customize before using\n\n");
    fprintf(out, "[mode]\n");
    fprintf(out, "name = \"draft\"\n");
    fprintf(out, "description = \"Auto-generated mode\"\n\n");

    /* Suggest signatures from first few non-blank lines */
    fprintf(out, "[detection]\n");
    fprintf(out, "signatures = [");
    int sig_count = 0;
    for (size_t i = 0; i < la->count && i < 20 && sig_count < 3; i++) {
        if (lp_is_blank(la->lines[i])) continue;
        char *trimmed = lp_strtrim(la->lines[i]);
        size_t tlen = strlen(trimmed);
        if (tlen > 5 && tlen < 80) {
            if (sig_count > 0) fprintf(out, ", ");
            /* Use first 40 chars as signature candidate */
            size_t use = tlen > 40 ? 40 : tlen;
            fprintf(out, "\"");
            for (size_t c = 0; c < use; c++) {
                if (trimmed[c] == '"') fprintf(out, "\\\"");
                else fputc(trimmed[c], out);
            }
            fprintf(out, "\"");
            sig_count++;
        }
        free(trimmed);
    }
    fprintf(out, "]\n\n");

    /* Dedup strip patterns */
    fprintf(out, "[dedup]\n");
    fprintf(out, "strip_patterns = [\"\\\"[^\\\"]*\\\"\", \"0x[0-9a-f]+\"]\n\n");

    /* Phase markers from phase segments */
    fprintf(out, "[segments]\n");
    fprintf(out, "phase_markers = [");
    int pm_count = 0;
    for (size_t i = 0; i < seg_count && pm_count < 5; i++) {
        if (segs[i].type == LP_SEG_PHASE && segs[i].line_count > 0) {
            if (pm_count > 0) fprintf(out, ", ");
            char *trimmed = lp_strtrim(segs[i].lines[0]);
            fprintf(out, "\"%s\"", trimmed);
            free(trimmed);
            pm_count++;
        }
    }
    fprintf(out, "]\n");
    fprintf(out, "block_triggers = [\"error:\", \"warning:\", \"FAILED\"]\n\n");

    /* Interest keywords — look for common interesting terms */
    fprintf(out, "[interest]\n");
    fprintf(out, "keywords = [\"error\", \"warning\", \"FAILED\", \"undefined\"]\n");
    fprintf(out, "error_patterns = [\"error:\", \"fatal:\", \"FAILED\", \"undefined reference\"]\n");
    fprintf(out, "warning_patterns = [\"warning:\"]\n");
}

/* ---- Main ---- */

int main(int argc, char **argv) {
    logexplore_args args = parse_args(argc, argv);

    if (args.show_help_agent) {
        fputs(HELP_AGENT_TEXT, stdout);
        return 0;
    }
    if (args.show_help) {
        fputs(HELP_TEXT, stdout);
        return 0;
    }

    if (!args.input_file) {
        fprintf(stderr, "logexplore: no input file specified\n");
        fprintf(stderr, "Usage: logexplore [OPTIONS] <FILE>\n");
        return 1;
    }

    FILE *fp = fopen(args.input_file, "r");
    if (!fp) {
        fprintf(stderr, "logexplore: cannot open '%s'\n", args.input_file);
        return 1;
    }

    line_array la = read_all_lines(fp);
    fclose(fp);

    if (la.count == 0) {
        fprintf(stderr, "logexplore: empty file\n");
        free_line_array(&la);
        return 1;
    }

    /* Dedup analysis */
    lp_dedup_table dedup;
    lp_dedup_init(&dedup, la.count / 2 + 64);
    for (size_t i = 0; i < la.count; i++) {
        lp_dedup_insert(&dedup, la.lines[i], i, NULL, 0);
    }

    /* Try to detect mode */
    char *mode_dir = lp_mode_find_dir();
    lp_mode **modes = NULL;
    size_t mode_count = 0;
    lp_mode *active_mode = NULL;

    if (mode_dir) {
        modes = lp_mode_load_dir(mode_dir, &mode_count);
        free(mode_dir);
        if (mode_count > 0) {
            size_t sniff = la.count < SNIFF_LINES ? la.count : SNIFF_LINES;
            const char *detected = lp_mode_detect((const char **)la.lines, sniff,
                                                   modes, mode_count);
            active_mode = lp_mode_find(modes, mode_count, detected);
        }
    }

    /* Segment detection */
    size_t seg_count;
    lp_segment *segs = lp_segment_detect((const char **)la.lines, la.count,
                                          (const struct lp_mode *)active_mode,
                                          &seg_count);

    /* Suggest mode output (different from normal output) */
    if (args.suggest_mode) {
        suggest_mode_toml(stdout, &la, &dedup, segs, seg_count);
        goto cleanup;
    }

    /* Normal output */
    size_t sorted_count;
    lp_dedup_entry **sorted = lp_dedup_sorted(&dedup, &sorted_count);

    size_t unique = sorted_count;
    size_t duplicates = la.count - unique;

    fprintf(stdout, "[LOGEXPLORE] %zu lines | %zu unique | %zu duplicates\n",
            la.count, unique, duplicates);

    analyze_encoding(stdout, &la);

    /* Phase analysis */
    if (!args.show_freq || args.show_phases) {
        detect_phases(stdout, &la, segs, seg_count, args.show_phases);
    }

    /* Frequency table */
    if (!args.show_phases || args.show_freq) {
        size_t top = args.show_freq ? sorted_count : args.top_n;
        if (top > sorted_count) top = sorted_count;

        fprintf(stdout, "\n[FREQUENCY TABLE: top %zu]\n", top);
        for (size_t i = 0; i < top; i++) {
            fprintf(stdout, "  x%-4zu %s\n", sorted[i]->count, sorted[i]->original);
        }
    }

    /* Segment listing */
    if (args.show_segments || (!args.show_freq && !args.show_phases)) {
        fprintf(stdout, "\n[SEGMENTS DETECTED: %zu]\n", seg_count);
        for (size_t i = 0; i < seg_count; i++) {
            const char *type_name = "block";
            switch (segs[i].type) {
                case LP_SEG_ERROR:   type_name = "error"; break;
                case LP_SEG_WARNING: type_name = "warning"; break;
                case LP_SEG_DATA:    type_name = "tabular data"; break;
                case LP_SEG_PHASE:   type_name = "phase marker"; break;
                case LP_SEG_INFO:    type_name = "info"; break;
                case LP_SEG_NORMAL:  type_name = "block"; break;
            }
            fprintf(stdout, "  #%-3zu lines %zu-%zu  (%zu lines, %s)\n",
                    i + 1, segs[i].start_line + 1, segs[i].end_line + 1,
                    segs[i].line_count, type_name);

            if (args.show_segments && segs[i].line_count > 0) {
                /* Show first 2 lines as preview */
                size_t preview = segs[i].line_count < 2 ? segs[i].line_count : 2;
                for (size_t p = 0; p < preview; p++) {
                    fprintf(stdout, "    | %s\n", segs[i].lines[p]);
                }
                if (segs[i].line_count > 2)
                    fprintf(stdout, "    | ... (%zu more lines)\n",
                            segs[i].line_count - 2);
            }
        }
    }

    /* Signature detection hint */
    if (!args.show_phases && !args.show_freq && !args.show_segments) {
        fprintf(stdout, "\n[SIGNATURES FOUND]\n");
        if (active_mode) {
            fprintf(stdout, "  Detected mode: %s\n",
                    active_mode->name ? active_mode->name : "unknown");
        } else {
            fprintf(stdout, "  No matching mode found. Use --suggest-mode to generate a draft.\n");
        }
    }

    free(sorted);

cleanup:
    lp_segments_free(segs, seg_count);
    lp_dedup_free(&dedup);
    if (modes) lp_modes_free(modes, mode_count);
    free_line_array(&la);

    return 0;
}
