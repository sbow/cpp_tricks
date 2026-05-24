---
name: cpp-tricks-health
description: >-
  Validates cpp_tricks repo health: make all builds, README.md documents
  current programs and targets, and scripts/interrogate.py generates the
  walkthrough site. Use before commits or PRs, when finishing a feature,
  or when the user asks if make/README/interrogate are up to date.
---

# cpp_tricks health checks

Run all checks from the repo root:

```bash
bash .cursor/skills/cpp-tricks-health/scripts/check.sh
```

Fix failures before declaring work complete. Re-run until all pass.

## Checklist

```
- [ ] make all
- [ ] README.md updated
- [ ] interrogate.py works
```

## 1. make works

```bash
make all
```

Must exit 0. Builds every program under `cpp_tricks/*/src/*.cpp`.

Optional smoke tests (not part of the default script):

```bash
make test-ipc      # in-process UDP + UDS benchmark
make test-ipc-mp    # two-process UDP + UDS benchmark
```

If `make all` fails, read the linker/compiler error, fix sources or Makefile, rebuild.

## 2. README.md updated

Run the README validator:

```bash
python3 .cursor/skills/cpp-tricks-health/scripts/check_readme.py
```

README must:

- Document `make` / Makefile usage (`make all`, IPC test targets)
- Describe `scripts/interrogate.py` with current flags (`--programs-root`, not legacy single-file `--source` only)
- Reflect layout: `cpp_tricks/<program>/src/`, not a lone `src/main.cpp`
- Mention every program with `cpp_tricks/<name>/src/*.cpp` in the features table or guides
- Mention `ipc` (header-only library under `cpp_tricks/ipc/src/ipc.h`, tests in `cpp_tricks/ipc/test/`)

When adding a program or make target, update **both** the features table and a `###` guide section using the template at the bottom of README.md.

## 3. interrogate.py works

```bash
uv sync
uv run scripts/interrogate.py --clean
```

Success criteria:

| Requirement | Path / signal |
|-------------|----------------|
| Site generated | `build/interrogate/site/index.html` |
| Metadata written | `build/interrogate/site/meta.json` |
| Pipeline OK | Exit 0, **or** only `run` stage failed for programs that block or need CLI args (e.g. `udp_echo_server`) |

If compile/link stages fail, fix sources or `interrogate.py` — do not ignore.

If only **Run** fails for server/client binaries, that is acceptable; the walkthrough site still documents preprocess → link correctly.

Open the site locally:

```bash
xdg-open build/interrogate/site/index.html
```

## Reporting results

After running `check.sh`, summarize:

```markdown
## Health check

- make all: pass / fail
- README.md: pass / fail (list missing items)
- interrogate.py: pass / fail (note which programs/stages failed)

### Fixes applied
(bullet list, or "none")
```

## Fixing common failures

| Failure | Action |
|---------|--------|
| `undefined reference to main` | Add `main.cpp` or remove empty `src/*.cpp` stub |
| README missing program | Add row to features table + guide section |
| README outdated layout | Replace `src/main.cpp` examples with `cpp_tricks/<program>/src/` |
| interrogate compile error | Fix C++ sources; confirm `g++` on PATH |
| interrogate run failure (server) | Expected if binary blocks; verify other stages OK in site UI |
