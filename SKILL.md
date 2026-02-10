---
name: logpilot
description: Use this skill whenever the user provides build logs, compiler output, test results, or any large diagnostic output that needs to be analyzed efficiently. Instead of reading raw logs line by line, use LogPilot's tools to semantically compress the log, identify known fix patterns, and explore unfamiliar log formats. Triggers include: build failures, compiler errors, test output, CI/CD logs, or any file ending in .log or containing build diagnostic output. Also use when the user mentions "build failed", "compile error", "west build", "gradle", "pytest", or asks you to look at a log.
license: MIT
---

# LogPilot — Semantic Build Log Compression

## Overview

LogPilot is a three-tool system for token-efficient build log analysis. Never read raw logs into context — always use these tools first.

## Quick Start

```bash
# Compress a build log (auto-detects build system)
logparse build.log

# Explore an unfamiliar log format
logexplore unknown.log

# Check errors against known fixes
logparse build.log | logfix --check
```

## Tools

### logparse — Compress the log
Deduplicates, segments, scores, and packs a build log into a token budget. Run with `--help agent` to learn how to add new build system modes.

### logexplore — Discover structure  
Analyzes unfamiliar logs to show phases, frequency tables, and segment boundaries. Use before creating new logparse modes.

### logfix — Fix memory
Matches error patterns against a flat-file YAML knowledge base. Run with `--help agent` to learn how to add new fix entries.

## Full Documentation

See `claude.md` in this repository for complete specifications, algorithm details, self-update protocols, and bootstrap instructions.
