# LogPilot — Semantic Build Log Compression Toolkit

## What This Is

LogPilot is a set of three Unix-philosophy CLI tools that sit between raw build output and the LLM context window. Instead of consuming 12,000-line build logs raw, Claude Code uses these tools to get a token-efficient, semantically compressed view of what happened in a build.

**The three tools:**

| Tool | Purpose | Analogy |
|------|---------|---------|
| `logparse` | Compress a log, tell me the story | `grep` + `awk` + an engineer who knows what matters |
| `logexplore` | Discover structure in unfamiliar logs | `less` + `wc` + "show me what's in here" |
| `logfix` | Match errors to known fixes | `git log --grep` + institutional memory |

**All tools are self-documenting and self-extending.** Run any tool with `--help agent` to get machine-readable instructions for adding modes, patterns, and fix entries.

---

## Directory Structure

```
logpilot/
├── claude.md              ← You are here (Claude Code instructions)
├── SKILL.md               ← Claude.ai skill variant
├── bin/
│   ├── logparse           ← Main compression tool
│   ├── logexplore         ← Structure discovery tool
│   └── logfix             ← Fix memory lookup/writer
├── modes/
│   ├── zephyr.toml        ← Zephyr/west build mode
│   ├── gradle.toml        ← Gradle/Android build mode
│   ├── pytest.toml        ← Python test output mode
│   ├── cmake.toml         ← Raw CMake mode
│   └── generic.toml       ← Fallback mode (always present)
├── fixes/
│   ├── zephyr/
│   │   ├── devicetree-ord-undefined.yaml
│   │   └── kconfig-redefined-symbol.yaml
│   ├── gradle/
│   └── python/
├── schema/
│   ├── mode.schema.toml   ← What a valid mode file looks like
│   └── fix.schema.yaml    ← What a valid fix entry looks like
├── examples/
│   ├── example-mode.toml
│   └── example-fix.yaml
└── tests/
    ├── sample-logs/       ← Real log snippets for testing
    └── test_logparse.py
```

---

## When and How Claude Code Should Use These Tools

### MANDATORY: Always use `logparse` instead of reading raw build logs

When a build fails or produces warnings, **never** `cat` or read the full log. Always:

```bash
# Auto-detect build system, default budget
logparse build.log

# Explicit mode, custom token budget
logparse build.log --mode zephyr --budget 400

# With keywords you're specifically hunting for
logparse build.log --mode zephyr --keywords "ord, overlay, pinctrl"

# Pipe directly from build command
west build -b nrf52840dk 2>&1 | logparse --mode zephyr
```

### Use `logexplore` when encountering unfamiliar log formats

Before creating a new mode, explore the log structure first:

```bash
# Show structure: phases, frequency table, segment boundaries
logexplore build.log

# More detail on repetition patterns
logexplore build.log --show-freq --top 20

# Dump detected segments with line ranges
logexplore build.log --show-segments
```

### Use `logfix` after `logparse` identifies errors

```bash
# Check parsed errors against known fixes
logparse build.log --mode zephyr | logfix --check

# Query directly with an error pattern
logfix --query "undefined node 'ord,"

# Add a new fix entry after resolving an issue
logfix --add --tags zephyr,devicetree

# Validate all fix entries
logfix --validate
```

### The Build Failure Workflow

When Claude Code encounters a build failure, follow this sequence:

```
1. logparse <logfile> [--mode <mode>]     → Get the compressed story
2. logfix --check < parsed_output         → Check for known fixes  
3. IF match found → Apply known fix
4. IF no match → Reason about the error, implement fix
5. AFTER fix works → logfix --add         → Write new fix entry
```

**Never skip step 5.** The fix database grows with every resolved issue.

---

## Tool Specifications

### `logparse` — Semantic Log Compression

**Algorithm:**

1. **Auto-detect mode** — Sniff first 50 lines for signatures (`west build`, `BUILD SUCCESSFUL`, `pytest`, `ninja`, etc.). Use explicit `--mode` to override.
2. **Deduplicate and count** — Hash every line (stripped of paths/numbers that vary between builds), track frequency. Lines appearing N+ times collapse to one instance with `[×N]` annotation.
3. **Segment detection** — Identify coherent blocks using: blank-line boundaries, indentation changes, mode-specific phase markers. A segment is an atomic unit — it gets included or excluded whole.
4. **Interest scoring** — Each segment scores based on: keyword matches, frequency outliers (both high and low), error/warning pattern matches, mode-specific triggers.
5. **Budget packing** — Fill the token budget with highest-scoring content first. Always include: stats header, frequency table (top N), all error/warning segments, tail of build output.

**Output format:**

```
[LOGPARSE] mode: zephyr | 12,847 lines → 340 lines (97.4% reduction)
[STATS] 14 unique repeated patterns | 3 error blocks | 2 warning cascades
[STATS] Build phases detected: cmake, kconfig, devicetree, compilation, linking

[FREQ ×94] warning: unused variable 'ctx' in sensor_hub.c
[FREQ ×31] note: in expansion of macro 'DT_INST_FOREACH_STATUS_OKAY'
[FREQ ×2]  warning: overflow in implicit constant conversion

[SEGMENT: devicetree-error] lines 2041-2087
/home/adrian/project/build/zephyr/zephyr.dts:847:
  node '/soc/i2c@40003000/sensor@44' depends on undefined node 'ord,3'
  ... full block preserved ...

[SEGMENT: linker-map] lines 8822-8830
Memory region         Used    Size   %Used
  FLASH:            248312  1048576  23.68%
  RAM:               65432   262144  24.96%

[TAIL: last 20 lines]
... final build output ...
```

**Flags:**

| Flag | Description |
|------|-------------|
| `--mode <name>` | Force a specific build system mode |
| `--budget <lines>` | Target output size in lines (default: 300) |
| `--keywords <csv>` | Additional keywords to score as high-interest |
| `--raw-freq` | Show full frequency table, not just top N |
| `--no-tail` | Omit final lines of log |
| `--json` | Output as JSON for piping to other tools |
| `--help` | Human help |
| `--help agent` | Machine-readable self-update instructions |

---

### `logexplore` — Structure Discovery

Used to analyze unfamiliar log formats before creating a new `logparse` mode.

**Output format:**

```
[LOGEXPLORE] 12,847 lines | 487 unique | 12,360 duplicates
[ENCODING] UTF-8 | longest line: 342 chars | avg: 67 chars

[PHASE BOUNDARIES] (detected by blank lines + pattern shifts)
  Phase 1: lines 1-204      (cmake configuration)
  Phase 2: lines 205-847    (kconfig resolution)  
  Phase 3: lines 848-2100   (devicetree compilation)
  Phase 4: lines 2101-12300 (C compilation)
  Phase 5: lines 12301-12847 (linking)

[FREQUENCY TABLE: top 15]
  ×94  warning: unused variable 'ctx' in sensor_hub.c
  ×31  note: in expansion of macro 'DT_INST_FOREACH_STATUS_OKAY'
  ...

[SEGMENTS DETECTED: 23]
  #1  lines 2041-2087  (47 lines, indented block, contains 'error')
  #2  lines 8822-8830  (9 lines, tabular data)
  ...

[SIGNATURES FOUND]
  Line 1: "-- west build: making build dir"  → suggests: zephyr mode
  Line 205: "Parsing /home/.../Kconfig"       → confirms: zephyr mode
```

**Flags:**

| Flag | Description |
|------|-------------|
| `--show-freq` | Full frequency table |
| `--show-segments` | All detected segments with preview |
| `--show-phases` | Phase boundary analysis only |
| `--top <N>` | Number of frequency entries to show (default: 15) |
| `--suggest-mode` | Output a draft TOML mode file based on analysis |
| `--help agent` | Machine-readable self-update instructions |

---

### `logfix` — Fix Memory

A flat-file knowledge base of error patterns and their resolutions.

**Fix entry format (YAML):**

```yaml
pattern: "depends on undefined node 'ord,"
regex: "node '.+' depends on undefined node 'ord,"
tags: [zephyr, devicetree, nordic]
fix: |
  Missing or mismatched node reference in devicetree overlay.
  Check that the overlay file references nodes that exist in the 
  base board DTS. Common cause: pin numbering changed between 
  board revisions or SoC families (e.g., nRF52840 → nRF54L15).
context: "Hit this migrating nRF52840 overlays to nRF54L15 for AI Quit hardware"
resolved: 2025-01-14
commit_ref: "abc123f"
severity: error
```

**Matching algorithm:**

1. Strip variable content (paths, line numbers, hex addresses) from error text
2. Fuzzy match skeleton against `pattern` fields
3. If `regex` field exists, try regex match for precision
4. Return matches ranked by similarity score
5. Multiple matches are valid — show all with confidence scores

**Flags:**

| Flag | Description |
|------|-------------|
| `--check` | Read parsed logparse output from stdin, match all errors |
| `--query "<text>"` | Match a single error string |
| `--add` | Interactive: create a new fix entry |
| `--add-from <file>` | Create fix entry from a YAML file |
| `--tags <csv>` | Filter matches by tags |
| `--validate` | Check all fix entries against schema |
| `--stats` | Show database statistics |
| `--help agent` | Machine-readable self-update instructions |

---

## Self-Update Protocol

### The `--help agent` Contract

Every tool, when called with `--help agent`, returns structured instructions that tell Claude Code exactly how to extend the tool. This is the mechanism that makes the toolkit self-improving.

**`logparse --help agent` returns:**

```
AGENT SELF-UPDATE INSTRUCTIONS
===============================
TO ADD A NEW BUILD SYSTEM MODE:
  1. Run: logexplore <sample.log> --suggest-mode > modes/draft.toml
  2. Edit the generated TOML to refine signatures, triggers, segments
  3. Rename to modes/<name>.toml
  4. Validate: logparse --validate-mode <name>
  5. Test: logparse <sample.log> --mode <name> and verify output quality

MODE FILE SCHEMA (modes/*.toml):
  [mode]
  name = "zephyr"
  description = "Zephyr RTOS / west build system"
  
  [detection]
  signatures = ["west build", "-- Found Zephyr", "ninja: build"]
  
  [dedup]
  strip_patterns = ['"/home/[^"]*"', "0x[0-9a-f]+"]
  
  [segments]
  phase_markers = ["Parsing Kconfig", "devicetree", "Linking"]
  block_triggers = ["error:", "warning:", "Memory region"]
  
  [interest]
  keywords = ["ord,", "overlay", "CONFIG_", "FLASH:", "RAM:"]
  error_patterns = ["error:", "fatal:", "undefined reference"]
  warning_patterns = ["warning:"]

FULL SCHEMA: schema/mode.schema.toml
EXAMPLES: examples/example-mode.toml, modes/zephyr.toml
```

**`logfix --help agent` returns:**

```
AGENT SELF-UPDATE INSTRUCTIONS
===============================
TO ADD A NEW FIX ENTRY:
  1. Create: fixes/<primary-tag>/<slug>.yaml
  2. Required fields: pattern, tags, fix
  3. Optional fields: regex, context, resolved, commit_ref, severity
  4. Validate: logfix --validate
  5. Or use interactive: logfix --add

TO UPDATE AN EXISTING FIX:
  1. Locate: logfix --query "<pattern>" --show-path
  2. Edit the YAML file directly
  3. Validate: logfix --validate

FIX ENTRY SCHEMA: schema/fix.schema.yaml
EXAMPLES: examples/example-fix.yaml

NAMING CONVENTION:
  File: fixes/{tag}/{short-description}.yaml
  Example: fixes/zephyr/devicetree-ord-undefined.yaml

PATTERN WRITING GUIDELINES:
  - Use the shortest unique substring that identifies the error
  - Strip paths, line numbers, and hex values
  - Include 'ord,' style fragments that are build-system-specific
  - The regex field is optional but improves precision
```

**`logexplore --help agent` returns:**

```
AGENT SELF-UPDATE INSTRUCTIONS  
===============================
TO ADD NEW FORMAT SIGNATURES:
  1. Edit: modes/generic.toml → [detection] → signatures
  2. Or create a new mode file (see logparse --help agent)

TO IMPROVE SEGMENT DETECTION HEURISTICS:
  1. Segment detection uses these signals:
     - Blank line boundaries
     - Indentation level changes (>2 level shift)
     - Mode-specific phase markers
     - Tabular data detection (consistent column alignment)
  2. To add custom heuristics, add to [segments] in mode TOML:
     segment_start_patterns = ["^=+$", "^-+$"]
     segment_end_patterns = ["^$"]

TO REGISTER A NEW LOG FORMAT:
  1. Run: logexplore <sample.log> --suggest-mode
  2. Review and edit the generated TOML
  3. Save to modes/<name>.toml
  4. Test: logparse <sample.log> --mode <name>
```

---

## Mode Files Reference

### Shipped Modes

**`modes/zephyr.toml`** — Zephyr RTOS / west / Nordic SDK
- Detects: `west build`, `Found Zephyr`, `ninja: build`
- Key triggers: `ord,`, `overlay`, `CONFIG_`, DTS resolution blocks
- Segments: Kconfig phase, devicetree compilation, linker memory map
- Dedup strips: absolute paths, hex addresses, build timestamps

**`modes/gradle.toml`** — Gradle / Android builds
- Detects: `BUILD SUCCESSFUL`, `BUILD FAILED`, `> Task :`
- Key triggers: dependency resolution trees, task failure chains
- Segments: dependency blocks, compilation error groups, test results

**`modes/pytest.toml`** — Python test output
- Detects: `pytest`, `PASSED`, `FAILED`, `ERROR`, `collected`
- Key triggers: full tracebacks, assertion diffs
- Segments: individual test failure blocks (traceback + assert)

**`modes/cmake.toml`** — Raw CMake (not west-wrapped)
- Detects: `cmake`, `-- Configuring`, `-- Generating`
- Key triggers: find_package failures, variable resolution

**`modes/generic.toml`** — Fallback for unknown formats
- No signatures (catches everything)
- Basic heuristics: error/warning keywords, indentation blocks
- Conservative budget allocation

---

## Implementation Notes for Bootstrap

### Language & Dependencies

- **Python 3.10+**, stdlib only for core functionality
- No external dependencies for `logparse` and `logexplore`
- `logfix` uses `pyyaml` for fix entry parsing (or stdlib `json` if YAML is unavailable — fix entries can be JSON as fallback)
- `tomllib` (stdlib in 3.11+) for mode files. For 3.10, `tomli` as fallback.

### Performance Targets

- `logparse` should handle a 50,000-line log in under 2 seconds
- Memory: stream the log, don't load it all at once for files over 100MB
- The frequency hash table is the main memory consumer — that's fine

### Testing Strategy

- `tests/sample-logs/` contains real log snippets (sanitized of paths/secrets)
- Each mode should have at least one sample log and expected output
- `test_logparse.py` runs each sample through its mode and validates: line reduction ratio, presence of key error segments, correct frequency counts
- Claude Code should run tests after modifying any tool or mode

---

## Bootstrap Instructions

If the tools don't exist yet, Claude Code should build them from this specification.

**Build order:**

1. Create directory structure as specified above
2. Implement `logexplore` first — it's the simplest and validates log parsing fundamentals
3. Implement `logparse` second — it depends on the same parsing primitives
4. Implement `logfix` third — it depends on `logparse` output format
5. Create `modes/generic.toml` and `modes/zephyr.toml` as initial modes
6. Create `schema/` files so `--validate` commands work
7. Add sample logs and tests
8. Run the test suite

**Each tool should be a single Python file in `bin/`.** Keep it simple. If a tool grows beyond 500 lines, that's a smell — split parsing primitives into a shared `lib/` module.

Make all tools executable: `chmod +x bin/logparse bin/logexplore bin/logfix`

Use shebangs: `#!/usr/bin/env python3`

---

## License

MIT. This toolkit is designed to be shared, forked, and extended.
