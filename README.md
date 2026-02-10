# 🛩️ LogPilot

**Semantic build log compression for LLM agents.**

Stop feeding 12,000-line build logs into your context window. LogPilot compresses build output into token-efficient summaries that preserve everything that matters — errors, warnings, frequency anomalies, and coherent diagnostic blocks — while eliminating noise.

---

## For Claude Code — Why and How to Use This

### Why

When you encounter a build failure, your instinct is to read the entire log file. **Don't.** A typical Zephyr RTOS build produces 10,000–50,000 lines of output. Most of it is identical between builds — repeated compiler warnings, progress counters, boilerplate cmake output. Reading all of it wastes your context window on noise.

LogPilot gives you a **300-line semantic summary** that preserves:
- Every error and warning block (complete, not truncated)
- Frequency counts for repeated lines (`[x94]` instead of 94 copies)
- Tabular data like memory maps
- The final build result (tail)

### How — The Build Failure Workflow

**Always follow this sequence when a build fails:**

```bash
# 1. Compress the log (auto-detects build system)
logparse build.log

# 2. Check for known fixes
logfix --query "the error pattern you see"

# 3. If no known fix, diagnose and fix the issue yourself

# 4. AFTER your fix works, record it for next time
logfix --add --tags zephyr,devicetree
```

**Key commands:**

```bash
# Force a specific build system mode and token budget
logparse build.log --mode zephyr --budget 400

# Pipe directly from a build command
west build -b nrf52840dk 2>&1 | logparse --mode zephyr

# Search for keywords you care about
logparse build.log --keywords "ord, overlay, pinctrl"

# JSON output for programmatic use
logparse build.log --json

# The default text output is already optimized for LLM consumption —
# language-native, not JSON. Use it as-is.

# Explore an unfamiliar log format before creating a mode
logexplore mystery-output.log --show-segments

# Generate a draft mode TOML from a sample log
logexplore build.log --suggest-mode > modes/new-format.toml

# Check all fix entries are valid
logfix --validate

# See database stats
logfix --stats
```

### Self-Extending

Every tool supports `--help agent` which returns machine-readable instructions for adding new build system modes, fix database entries, and format signatures. **Use these when you need to extend the toolkit:**

```bash
logparse --help agent   # How to add new modes
logexplore --help agent # How to improve segment detection
logfix --help agent     # How to add new fix entries
```

### What's Available

| Tool | Purpose |
|------|---------|
| `logparse` | Compress a build log into a token-efficient summary |
| `logexplore` | Discover structure in unfamiliar log formats |
| `logfix` | Match errors against a knowledge base of past fixes |

**Shipped modes:** `zephyr`, `gradle`, `pytest`, `cmake`, `generic`

**Fix database:** YAML files in `fixes/` — grows with every resolved build issue.

---

## Building from Source

LogPilot is implemented in C11 with no external dependencies (all vendored). It builds on Windows, Linux, and macOS.

### Requirements

- CMake 3.16+
- A C11 compiler (MSVC, GCC, or Clang)
- Ninja (recommended) or Make

### Build

```bash
# Configure
cmake -B build -G Ninja

# Build
cmake --build build

# Run tests (18 integration tests)
cd build && ctest --output-on-failure && cd ..
```

The executables are built to `build/logparse`, `build/logexplore`, and `build/logfix` (`.exe` on Windows).

### Install (optional)

```bash
cmake --install build --prefix /usr/local
```

---

## Project Structure

```
logpilot/
├── claude.md              ← Claude Code integration spec (read this first)
├── SKILL.md               ← Claude.ai skill variant
├── CMakeLists.txt         ← Build system
├── src/
│   ├── logparse.c         ← Main compression tool
│   ├── logexplore.c       ← Structure discovery tool
│   ├── logfix.c           ← Fix memory lookup/writer
│   └── lib/               ← Shared core library (8 modules)
│       ├── util.c/h       ← Strings, file I/O, platform shims
│       ├── token.c/h      ← Token estimation (~4 chars/token)
│       ├── dedup.c/h      ← FNV-1a hashing, frequency table
│       ├── segment.c/h    ← Block detection, type classification
│       ├── score.c/h      ← Interest scoring (keywords, frequency, type)
│       ├── budget.c/h     ← Greedy knapsack packing
│       ├── mode.c/h       ← TOML mode loader, auto-detect
│       └── fix.c/h        ← YAML fix database, fuzzy matching
├── modes/                 ← Build system mode definitions (TOML)
├── fixes/                 ← Fix knowledge base (YAML)
├── schema/                ← Schema docs for modes and fixes
├── examples/              ← Template mode and fix files
├── tests/
│   ├── CMakeLists.txt     ← 18 CTest integration tests
│   └── sample-logs/       ← Sample build logs for testing
└── vendor/                ← Vendored dependencies (tiny-regex-c)
```

## Architecture

The `logparse` pipeline:

1. **Auto-detect mode** — Sniff first 50 lines for signatures (`west build`, `BUILD SUCCESSFUL`, `pytest`, etc.)
2. **Deduplicate** — FNV-1a hash each normalized line, collapse repeats with counts
3. **Segment** — Identify coherent blocks by blank lines, indent shifts, phase markers
4. **Line fate** — Three-tier model classifies every line: KEEP (errors, warnings, diagnostics), KEEP_ONCE (summary facts), DROP (boilerplate, caret lines, SDK include chains). Controlled by `[elision]` section in mode TOML.
5. **Score** — Rate each segment: errors (+10), warnings (+5), keyword matches (+3), frequency outliers (+2)
6. **Pack** — Greedy knapsack: errors always included, fill remaining budget by score
7. **Within-segment dedup** — Collapse repeated warnings with same `-Wflag` to first instance + count

## Philosophy

- Unix single-purpose tools
- Flat files over databases
- Git-tracked knowledge
- Self-documenting for both humans and LLMs
- Zero external dependencies — vendored C, builds anywhere

## License

MIT

*Built by [digitaltoaster](https://youtube.com/@digitaltoaster) for the embedded systems community.*
