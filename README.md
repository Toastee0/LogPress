# 🛩️ LogPilot

**Semantic build log compression for LLM agents.**

Stop feeding 12,000-line build logs into your context window. LogPilot compresses build output into token-efficient summaries that preserve everything that matters — errors, warnings, frequency anomalies, and coherent diagnostic blocks — while eliminating noise.

## The Problem

When an AI coding agent hits a build failure, it reads the entire log. A typical Zephyr RTOS build produces 10,000–50,000 lines of output. Most of it is identical between builds. Your context window fills up with noise, and the agent burns tokens re-reading information that could be expressed in 300 lines.

## The Solution

Three Unix-philosophy tools:

| Tool | What It Does |
|------|-------------|
| **`logparse`** | Compresses a build log. Deduplicates repeated lines (showing `[×94]` counts), extracts coherent error/diagnostic blocks whole, and packs the most important content into a configurable token budget. |
| **`logexplore`** | Analyzes unfamiliar log formats. Shows phase boundaries, frequency tables, and segment structure so you can create new `logparse` modes. |
| **`logfix`** | Matches errors against a flat-file knowledge base of past fixes. Grows automatically as you resolve issues. |

## Quick Start

```bash
# Clone the repo
git clone https://github.com/toastee0/logpilot.git
cd logpilot

# Compress a build log (auto-detects build system)
bin/logparse build.log

# Explore an unfamiliar log
bin/logexplore mystery-output.log

# Check errors against known fixes
bin/logparse build.log | bin/logfix --check
```

## For Claude Code Users

Drop this repo into your project (or symlink the `claude.md`). Claude Code will automatically use LogPilot instead of reading raw build logs. See `claude.md` for the full agent integration spec.

## For Claude.ai Users

The `SKILL.md` file allows Claude's web interface to use these tools when they're available on the system.

## Self-Extending

Every tool supports `--help agent` which returns machine-readable instructions for adding new build system modes, fix database entries, and format signatures. The tools teach your AI agent how to improve them.

## Build System Modes

Shipped modes: `zephyr`, `gradle`, `pytest`, `cmake`, `generic`

Add your own by creating a TOML file in `modes/`. See `schema/mode.schema.toml` for the format, or let `logexplore --suggest-mode` generate a draft from a sample log.

## Fix Database

Human-readable YAML files in `fixes/`. Each entry maps an error pattern to an actionable fix. The database is git-tracked and grows with every resolved build issue.

## Requirements

- Python 3.10+
- No external dependencies (stdlib only, with optional `pyyaml`)

## Philosophy

- Unix single-purpose tools
- Flat files over databases
- Git-tracked knowledge
- Self-documenting for both humans and LLMs
- Simple enough to debug, powerful enough to save hours

## License

MIT

---

*Built by [digitaltoaster](https://youtube.com/@digitaltoaster) for the embedded systems community.*
