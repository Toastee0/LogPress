/*
 * logfix — Fix memory lookup/writer
 * Part of LogPilot toolkit
 *
 * Matches error patterns against a flat-file YAML knowledge base
 * of past fixes. Grows automatically as issues are resolved.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>

#include "util.h"
#include "fix.h"

#define MIN_CONFIDENCE 0.3f

/* ---- Help text ---- */

static const char *HELP_TEXT =
    "logfix - Fix memory lookup/writer\n"
    "\n"
    "Usage: logfix [OPTIONS]\n"
    "\n"
    "Matches error patterns against a YAML knowledge base of fixes.\n"
    "\n"
    "Options:\n"
    "  --check            Read logparse output from stdin, match all errors\n"
    "  --query <text>     Match a single error string\n"
    "  --add              Interactive: create a new fix entry\n"
    "  --add-from <file>  Create fix entry from a YAML file\n"
    "  --tags <csv>       Filter matches by tags\n"
    "  --validate         Check all fix entries against schema\n"
    "  --stats            Show database statistics\n"
    "  --help             Show this help\n"
    "  --help agent       Machine-readable self-update instructions\n"
    "\n"
    "Examples:\n"
    "  logparse build.log | logfix --check\n"
    "  logfix --query \"undefined node 'ord,\"\n"
    "  logfix --add --tags zephyr,devicetree\n"
    "  logfix --validate\n";

static const char *HELP_AGENT_TEXT =
    "AGENT SELF-UPDATE INSTRUCTIONS\n"
    "===============================\n"
    "TO ADD A NEW FIX ENTRY:\n"
    "  1. Create: fixes/<primary-tag>/<slug>.yaml\n"
    "  2. Required fields: pattern, tags, fix\n"
    "  3. Optional fields: regex, context, resolved, commit_ref, severity\n"
    "  4. Validate: logfix --validate\n"
    "  5. Or use interactive: logfix --add\n"
    "\n"
    "TO UPDATE AN EXISTING FIX:\n"
    "  1. Locate: logfix --query \"<pattern>\" --show-path\n"
    "  2. Edit the YAML file directly\n"
    "  3. Validate: logfix --validate\n"
    "\n"
    "FIX ENTRY SCHEMA: schema/fix.schema.yaml\n"
    "EXAMPLES: examples/example-fix.yaml\n"
    "\n"
    "NAMING CONVENTION:\n"
    "  File: fixes/{tag}/{short-description}.yaml\n"
    "  Example: fixes/zephyr/devicetree-ord-undefined.yaml\n"
    "\n"
    "PATTERN WRITING GUIDELINES:\n"
    "  - Use the shortest unique substring that identifies the error\n"
    "  - Strip paths, line numbers, and hex values\n"
    "  - Include build-system-specific fragments\n"
    "  - The regex field is optional but improves precision\n";

/* ---- Argument parsing ---- */

typedef struct {
    bool        check_mode;
    const char *query_text;
    bool        add_mode;
    const char *add_from;
    char      **filter_tags;
    size_t      filter_tag_count;
    bool        validate_mode;
    bool        stats_mode;
    bool        show_help;
    bool        show_help_agent;
} logfix_args;

static logfix_args parse_args(int argc, char **argv) {
    logfix_args args;
    memset(&args, 0, sizeof(args));

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            if (i + 1 < argc && strcmp(argv[i + 1], "agent") == 0) {
                args.show_help_agent = true;
                i++;
            } else {
                args.show_help = true;
            }
        } else if (strcmp(argv[i], "--check") == 0) {
            args.check_mode = true;
        } else if (strcmp(argv[i], "--query") == 0 && i + 1 < argc) {
            args.query_text = argv[++i];
        } else if (strcmp(argv[i], "--add") == 0) {
            args.add_mode = true;
        } else if (strcmp(argv[i], "--add-from") == 0 && i + 1 < argc) {
            args.add_from = argv[++i];
        } else if (strcmp(argv[i], "--tags") == 0 && i + 1 < argc) {
            args.filter_tags = lp_split_csv(argv[++i], &args.filter_tag_count);
        } else if (strcmp(argv[i], "--validate") == 0) {
            args.validate_mode = true;
        } else if (strcmp(argv[i], "--stats") == 0) {
            args.stats_mode = true;
        }
    }
    return args;
}

/* ---- Read stdin lines ---- */

static char *read_stdin_all(void) {
    lp_string buf = lp_string_new(4096);
    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        lp_string_append_cstr(&buf, line);
    }
    char *result = strdup(lp_string_cstr(&buf));
    lp_string_free(&buf);
    return result;
}

/* ---- Extract error lines from logparse output ---- */

typedef struct {
    char **errors;
    size_t count;
    size_t cap;
} error_list;

static error_list extract_errors(const char *text) {
    error_list el;
    el.count = 0;
    el.cap = 16;
    el.errors = (char **)malloc(el.cap * sizeof(char *));

    /* Look for [SEGMENT: error] blocks and extract their content */
    const char *p = text;
    while (*p) {
        /* Find error segment markers */
        if (lp_str_starts_with(p, "[SEGMENT: error]") ||
            lp_str_contains_ci(p, "error:") ||
            lp_str_contains_ci(p, "fatal:") ||
            lp_str_contains_ci(p, "undefined reference")) {

            /* Read this line */
            const char *line_start = p;
            while (*p && *p != '\n') p++;
            size_t len = (size_t)(p - line_start);
            if (len > 0) {
                if (el.count >= el.cap) {
                    el.cap *= 2;
                    el.errors = (char **)realloc(el.errors, el.cap * sizeof(char *));
                }
                el.errors[el.count] = (char *)malloc(len + 1);
                memcpy(el.errors[el.count], line_start, len);
                el.errors[el.count][len] = '\0';
                el.count++;
            }
        }
        /* Advance to next line */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return el;
}

/* ---- Print match result ---- */

static void print_match(const lp_fix_match *m, bool show_path) {
    printf("  [%.0f%% confidence] ", m->confidence * 100.0f);
    if (m->fix->severity)
        printf("(%s) ", m->fix->severity);
    printf("Pattern: %s\n", m->fix->pattern);

    /* Show tags */
    if (m->fix->tag_count > 0) {
        printf("    Tags: ");
        for (size_t t = 0; t < m->fix->tag_count; t++) {
            if (t > 0) printf(", ");
            printf("%s", m->fix->tags[t]);
        }
        printf("\n");
    }

    /* Show fix */
    if (m->fix->fix_text) {
        printf("    Fix: %s\n", m->fix->fix_text);
    }

    /* Show context */
    if (m->fix->context) {
        printf("    Context: %s\n", m->fix->context);
    }

    if (show_path && m->fix->file_path) {
        printf("    File: %s\n", m->fix->file_path);
    }
    printf("\n");
}

/* ---- Tag filter ---- */

static bool matches_tag_filter(const lp_fix *f, char **tags, size_t tag_count) {
    if (!tags || tag_count == 0) return true;
    for (size_t t = 0; t < tag_count; t++) {
        for (size_t ft = 0; ft < f->tag_count; ft++) {
            if (strcmp(f->tags[ft], tags[t]) == 0)
                return true;
        }
    }
    return false;
}

/* ---- Interactive add ---- */

static char *prompt_line(const char *prompt) {
    printf("%s", prompt);
    fflush(stdout);
    char buf[1024];
    if (!fgets(buf, sizeof(buf), stdin)) return NULL;
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
    if (len > 0 && buf[len - 1] == '\r') buf[--len] = '\0';
    return strdup(buf);
}

static int do_interactive_add(char **filter_tags, size_t filter_tag_count) {
    lp_fix f;
    memset(&f, 0, sizeof(f));

    printf("=== Add new fix entry ===\n\n");

    f.pattern = prompt_line("Error pattern (shortest unique substring): ");
    if (!f.pattern || !f.pattern[0]) {
        fprintf(stderr, "logfix: pattern is required\n");
        return 1;
    }

    char *regex_str = prompt_line("Regex pattern (optional, Enter to skip): ");
    if (regex_str && regex_str[0]) f.regex = regex_str;
    else free(regex_str);

    /* Use filter tags or prompt */
    if (filter_tags && filter_tag_count > 0) {
        f.tags = filter_tags;
        f.tag_count = filter_tag_count;
    } else {
        char *tag_str = prompt_line("Tags (comma-separated): ");
        if (tag_str) {
            f.tags = lp_split_csv(tag_str, &f.tag_count);
            free(tag_str);
        }
    }

    char *fix_text = prompt_line("Fix description: ");
    f.fix_text = fix_text;

    char *context = prompt_line("Context (when/why encountered, optional): ");
    if (context && context[0]) f.context = context;
    else free(context);

    char *severity = prompt_line("Severity (error/warning, default: error): ");
    if (severity && severity[0]) f.severity = severity;
    else { free(severity); f.severity = strdup("error"); }

    /* Generate date */
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char date_buf[32];
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", tm);
    f.resolved = strdup(date_buf);

    /* Validate */
    char errbuf[256];
    if (!lp_fix_validate(&f, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "logfix: validation failed: %s\n", errbuf);
        return 1;
    }

    /* Generate filename */
    char *fix_dir = lp_fix_find_dir();
    if (!fix_dir) fix_dir = strdup("fixes");

    /* Use primary tag as subdirectory */
    const char *primary_tag = (f.tag_count > 0) ? f.tags[0] : "general";
    char *subdir = lp_path_join(fix_dir, primary_tag);

    /* Slugify pattern for filename */
    char slug[64];
    size_t si = 0;
    for (const char *p = f.pattern; *p && si < sizeof(slug) - 6; p++) {
        if (isalnum((unsigned char)*p)) slug[si++] = (char)tolower((unsigned char)*p);
        else if (si > 0 && slug[si - 1] != '-') slug[si++] = '-';
    }
    while (si > 0 && slug[si - 1] == '-') si--;
    slug[si] = '\0';

    char filename[128];
    snprintf(filename, sizeof(filename), "%s.yaml", slug);
    char *filepath = lp_path_join(subdir, filename);

    printf("\nWriting fix to: %s\n", filepath);
    int result = lp_fix_write(filepath, &f);
    if (result == 0) {
        printf("Fix entry created successfully.\n");
    } else {
        fprintf(stderr, "logfix: failed to write fix file\n");
    }

    free(filepath);
    free(subdir);
    free(fix_dir);
    free(f.pattern);
    free(f.regex);
    if (f.tags != filter_tags)
        lp_free_strings(f.tags, f.tag_count);
    free(f.fix_text);
    free(f.context);
    free(f.severity);
    free(f.resolved);

    return result;
}

/* ---- Main ---- */

int main(int argc, char **argv) {
    logfix_args args = parse_args(argc, argv);

    if (args.show_help_agent) {
        fputs(HELP_AGENT_TEXT, stdout);
        return 0;
    }
    if (args.show_help) {
        fputs(HELP_TEXT, stdout);
        return 0;
    }

    /* Interactive add mode */
    if (args.add_mode) {
        int ret = do_interactive_add(args.filter_tags, args.filter_tag_count);
        if (args.filter_tags) lp_free_strings(args.filter_tags, args.filter_tag_count);
        return ret;
    }

    /* Load fix database (local + global) */
    char *fix_dir = lp_fix_find_dir();
    if (!fix_dir) {
        if (args.stats_mode || args.validate_mode) {
            fprintf(stderr, "logfix: no fixes directory found\n");
            return 1;
        }
        /* For query/check, just report no fixes */
        fix_dir = strdup("fixes");
    }

    size_t fix_count = 0;
    lp_fix **fixes = lp_fix_load_dir(fix_dir, &fix_count);

    /* Also load from global ~/.logpilot/fixes/ */
    char *global_dir = lp_fix_find_global_dir();
    if (global_dir && (!fix_dir || strcmp(global_dir, fix_dir) != 0)) {
        size_t global_count = 0;
        lp_fix **global_fixes = lp_fix_load_dir(global_dir, &global_count);
        if (global_fixes && global_count > 0) {
            fixes = realloc(fixes, (fix_count + global_count) * sizeof(lp_fix *));
            for (size_t i = 0; i < global_count; i++)
                fixes[fix_count + i] = global_fixes[i];
            fix_count += global_count;
            free(global_fixes); /* only free array, not entries */
        }
        free(global_dir);
    }

    /* Add from file */
    if (args.add_from) {
        lp_fix *f = lp_fix_load(args.add_from);
        if (!f) {
            fprintf(stderr, "logfix: cannot load '%s'\n", args.add_from);
            free(fix_dir);
            if (fixes) lp_fixes_free(fixes, fix_count);
            return 1;
        }
        char errbuf[256];
        if (!lp_fix_validate(f, errbuf, sizeof(errbuf))) {
            fprintf(stderr, "logfix: validation failed: %s\n", errbuf);
            lp_fix_free(f);
            free(fix_dir);
            if (fixes) lp_fixes_free(fixes, fix_count);
            return 1;
        }
        printf("Fix entry loaded and validated from: %s\n", args.add_from);
        printf("  Pattern: %s\n", f->pattern);
        printf("  Tags: ");
        for (size_t i = 0; i < f->tag_count; i++) {
            if (i > 0) printf(", ");
            printf("%s", f->tags[i]);
        }
        printf("\n");
        lp_fix_free(f);
        free(fix_dir);
        if (fixes) lp_fixes_free(fixes, fix_count);
        return 0;
    }

    /* Stats mode */
    if (args.stats_mode) {
        printf("[LOGFIX STATS]\n");
        printf("  Fix directory: %s\n", fix_dir);
        printf("  Total entries: %zu\n", fix_count);

        /* Count by severity */
        size_t errors = 0, warnings = 0, other = 0;
        for (size_t i = 0; i < fix_count; i++) {
            if (fixes[i]->severity && strcmp(fixes[i]->severity, "error") == 0) errors++;
            else if (fixes[i]->severity && strcmp(fixes[i]->severity, "warning") == 0) warnings++;
            else other++;
        }
        printf("  Errors: %zu | Warnings: %zu | Other: %zu\n", errors, warnings, other);

        /* Unique tags */
        char *seen_tags[256];
        size_t seen_count = 0;
        for (size_t i = 0; i < fix_count; i++) {
            for (size_t t = 0; t < fixes[i]->tag_count; t++) {
                bool found = false;
                for (size_t s = 0; s < seen_count; s++) {
                    if (strcmp(seen_tags[s], fixes[i]->tags[t]) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found && seen_count < 256) {
                    seen_tags[seen_count++] = fixes[i]->tags[t];
                }
            }
        }
        printf("  Unique tags: %zu (", seen_count);
        for (size_t i = 0; i < seen_count; i++) {
            if (i > 0) printf(", ");
            printf("%s", seen_tags[i]);
        }
        printf(")\n");

        goto cleanup;
    }

    /* Validate mode */
    if (args.validate_mode) {
        printf("[LOGFIX VALIDATE] Checking %zu entries...\n", fix_count);
        bool all_valid = true;
        for (size_t i = 0; i < fix_count; i++) {
            char errbuf[256];
            if (!lp_fix_validate(fixes[i], errbuf, sizeof(errbuf))) {
                printf("  INVALID: %s -- %s\n",
                       fixes[i]->file_path ? fixes[i]->file_path : "(unknown)",
                       errbuf);
                all_valid = false;
            }
        }
        if (all_valid) {
            printf("  All %zu entries are valid.\n", fix_count);
        }
        goto cleanup;
    }

    /* Query mode */
    if (args.query_text) {
        if (fix_count == 0) {
            printf("logfix: no fix entries found (fixes directory: %s)\n", fix_dir);
            goto cleanup;
        }

        size_t match_count;
        lp_fix_match *matches = lp_fix_match_all(args.query_text, fixes, fix_count,
                                                  &match_count, MIN_CONFIDENCE);

        /* Filter by tags */
        printf("[LOGFIX] Query: %s\n", args.query_text);
        printf("[LOGFIX] %zu matches found:\n\n", match_count);

        for (size_t i = 0; i < match_count; i++) {
            if (!matches_tag_filter(matches[i].fix, args.filter_tags, args.filter_tag_count))
                continue;
            print_match(&matches[i], true);
        }

        if (match_count == 0) {
            printf("  No matching fixes found.\n");
        }

        lp_fix_matches_free(matches, match_count);
        goto cleanup;
    }

    /* Check mode (read from stdin) */
    if (args.check_mode) {
        char *input = read_stdin_all();
        error_list el = extract_errors(input);

        printf("[LOGFIX CHECK] Scanning %zu error lines against %zu fix entries...\n\n",
               el.count, fix_count);

        size_t total_matches = 0;
        for (size_t i = 0; i < el.count; i++) {
            size_t match_count;
            lp_fix_match *matches = lp_fix_match_all(el.errors[i], fixes, fix_count,
                                                      &match_count, MIN_CONFIDENCE);
            if (match_count > 0) {
                printf("Error: %s\n", el.errors[i]);
                for (size_t m = 0; m < match_count; m++) {
                    if (!matches_tag_filter(matches[m].fix,
                                            args.filter_tags, args.filter_tag_count))
                        continue;
                    print_match(&matches[m], false);
                    total_matches++;
                }
            }
            lp_fix_matches_free(matches, match_count);
        }

        if (total_matches == 0) {
            printf("No known fixes matched the errors.\n");
        }

        /* Free error list */
        for (size_t i = 0; i < el.count; i++)
            free(el.errors[i]);
        free(el.errors);
        free(input);
        goto cleanup;
    }

    /* Default: show help */
    fputs(HELP_TEXT, stdout);

cleanup:
    free(fix_dir);
    if (fixes) lp_fixes_free(fixes, fix_count);
    if (args.filter_tags) lp_free_strings(args.filter_tags, args.filter_tag_count);
    return 0;
}
