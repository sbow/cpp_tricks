# cpp_tricks

Small C++ examples, experiments, and tooling to explore how programs are built and run.

## Contents

- [Features](#features)
- [Project layout](#project-layout)
- [Prerequisites](#prerequisites)
- [Feature guides](#feature-guides)
  - [C++ compile & run](#cpp-compile--run)
  - [Build pipeline walkthrough](#build-pipeline-walkthrough)

---

## Features

Catalog of what lives in this repo. Add a row when you introduce something new, then document it under [Feature guides](#feature-guides).

| Feature | Type | Status | Guide |
|---------|------|--------|-------|
| C++ compile & run | workflow | active | [§ compile & run](#cpp-compile--run) |
| Build pipeline walkthrough | tool | active | [§ walkthrough](#build-pipeline-walkthrough) |

**Type:** `workflow` = everyday commands; `tool` = scripts/automation; `topic` = a C++ concept demonstrated in `src/` (add these as you learn).

**Status:** `active` · `planned` · `deprecated`

---

## Project layout

```
cpp_tricks/
├── src/                 # C++ sources (one or more examples)
├── include/             # headers (as the project grows)
├── build/               # generated binaries and reports (gitignored)
├── scripts/             # automation (e.g. interrogate)
├── pyproject.toml       # Python tooling (uv)
└── README.md
```

---

## Prerequisites

| Need | For |
|------|-----|
| `g++` (build-essential) | compiling and linking C++ |
| [uv](https://docs.astral.sh/uv/) | Python env and `scripts/interrogate.py` |
| `nm`, `objdump`, `readelf`, `file`, `ldd`, `strings` | binary inspection (usually on PATH with binutils) |

---

## Feature guides

Each feature follows the same shape: **what it is**, **when to use it**, **how to run**, **outputs**.

### C++ compile & run

**What:** Fastest path from source to a running program — single `g++` invocation (compile + link).

**When:** Editing `src/main.cpp` and you want immediate feedback without generating a report.

**How:**

```bash
g++ -std=c++20 -Wall -Wextra -o build/cpp_tricks src/main.cpp && ./build/cpp_tricks
```

Use **`g++`**, not `gcc`, for C++ (e.g. `iostream`); otherwise you may see linker errors for `std::cout`.

**Outputs:** `build/cpp_tricks` executable.

---

### Build pipeline walkthrough

**What:** `scripts/interrogate.py` drives the full toolchain in stages — preprocess (`.i`), assembly (`.s`), object (`.o`), link, run — and runs inspection tools on the artifacts. Results are written to a static HTML site you can read step by step.

**When:** Learning what the compiler and linker produce, or comparing symbol tables, relocations, and disassembly before vs after link.

**How:**

```bash
uv sync
uv run scripts/interrogate.py --clean
xdg-open build/interrogate/site/index.html   # Linux; or open the file in any browser
```

**Outputs:**

| Path | Contents |
|------|----------|
| `build/interrogate/` | `.i`, `.s`, `.o`, `cpp_tricks` binary |
| `build/interrogate/site/index.html` | walkthrough UI (commands + tool output per stage) |
| `build/interrogate/site/meta.json` | run metadata |

**Options:**

| Flag | Default | Purpose |
|------|---------|---------|
| `--source` | `src/main.cpp` | which translation unit to build |
| `--std` | `c++20` | `-std=` passed to `g++` |
| `--artifacts-dir` | `build/interrogate` | where `.i` / `.s` / `.o` / binary go |
| `--site-dir` | `build/interrogate/site` | HTML output directory |
| `--clean` | off | remove artifact and site dirs before build |
| `--no-debug` | off | omit `-g -O0` (less useful `objdump -S` source lines) |

**Site sections (in order):** Overview → Preprocess → Assembly → Object (with `nm` / `readelf` / `objdump`) → Executable (with `ldd`, `readelf`, disassembly) → Run.

---

<!-- Template for new features — copy into the table and add a ### section below:

| My feature | tool / workflow / topic | planned | [§ my feature](#my-feature) |

### My feature

**What:** …
**When:** …
**How:** …
**Outputs:** …

-->
