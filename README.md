# cpp_tricks

Small C++ examples, experiments, and tooling to explore how programs are built and run.

## Contents

- [Features](#features)
- [Project layout](#project-layout)
- [Prerequisites](#prerequisites)
- [Feature guides](#feature-guides)
  - [Makefile builds](#makefile-builds)
  - [IPC library & tests](#ipc-library--tests)
  - [UDP echo (standalone)](#udp-echo-standalone)
  - [Build pipeline walkthrough](#build-pipeline-walkthrough)

---

## Features

Catalog of what lives in this repo. Add a row when you introduce something new, then document it under [Feature guides](#feature-guides).

| Feature | Type | Status | Guide |
|---------|------|--------|-------|
| Makefile builds | workflow | active | [§ Makefile](#makefile-builds) |
| IPC library (`ipc`) | topic | active | [§ IPC](#ipc-library--tests) |
| `basic` | topic | active | [§ Makefile](#makefile-builds) |
| `udp_echo_server` | topic | active | [§ UDP echo](#udp-echo-standalone) |
| `udp_echo_client` | topic | active | [§ UDP echo](#udp-echo-standalone) |
| Build pipeline walkthrough | tool | active | [§ walkthrough](#build-pipeline-walkthrough) |

**Type:** `workflow` = everyday commands; `tool` = scripts/automation; `topic` = a C++ concept demonstrated under `cpp_tricks/<name>/`.

**Status:** `active` · `planned` · `deprecated`

---

## Project layout

```
cpp_tricks/                          # repo root
├── Makefile                         # build all programs and ipc tests
├── cpp_tricks/
│   ├── basic/src/main.cpp           # hello-world example
│   ├── ipc/src/ipc.h                # header-only UDP/UDS IPC library
│   ├── ipc/test/                    # echo benchmarks and two-process tests
│   ├── udp_echo_server/src/main.cpp
│   └── udp_echo_client/src/main.cpp
├── build/                           # generated binaries (gitignored)
├── scripts/
│   └── interrogate.py               # compile-stage walkthrough → HTML site
├── pyproject.toml                   # Python tooling (uv)
└── README.md
```

---

## Prerequisites

| Need | For |
|------|-----|
| `make`, `g++` (build-essential) | Makefile builds |
| [uv](https://docs.astral.sh/uv/) | Python env and `scripts/interrogate.py` |
| `nm`, `objdump`, `readelf`, `file`, `ldd`, `strings` | binary inspection (usually on PATH with binutils) |

---

## Feature guides

Each feature follows the same shape: **what it is**, **when to use it**, **how to run**, **outputs**.

### Makefile builds

**What:** Root `Makefile` discovers every program under `cpp_tricks/<name>/src/*.cpp` and builds `build/<name>/<name>`.

**When:** Everyday compile/link for any example program.

**How:**

```bash
make all              # build basic, udp_echo_client, udp_echo_server
make basic            # one program
make run-basic        # build and run
make help             # list targets
```

Use **`g++`**, not `gcc`, for C++ (e.g. `iostream`); otherwise you may see linker errors for `std::cout`.

**Outputs:** `build/<program>/<program>` executables.

---

### IPC library & tests

**What:** Header-only IPC library in `cpp_tricks/ipc/src/ipc.h` — UDP and Unix-domain datagram echo clients/servers via templates (`UdpEchoClient`, `UdsEchoServer`, etc.).

**When:** Learning sockets, comparing UDP vs UDS, or benchmarking round-trip latency.

#### About `ipc.h`

`ipc.h` is a single, header-only C++ library (no `.cpp` to link — just `#include "ipc.h"` with `-Icpp_tricks/ipc/src`). It wraps Linux datagram sockets behind a small template layer so the same client/server code works for two transports:

| Transport | Socket API | Address |
|-----------|------------|---------|
| `Udp` | `AF_INET` + `SOCK_DGRAM` | IP host + port |
| `Uds` | `AF_UNIX` + `SOCK_DGRAM` | filesystem path |

**Core pieces:**

- **`Buffer`** — non-owning view over caller memory (`writable` / `read_only`); no heap allocation on the hot path.
- **`Udp` / `Uds`** — transport traits with static `bind`, `connect_or_send`, `recv_from`, and `echo` methods plus per-transport `BindParams`, `SendParams`, and `RecvResult` types.
- **`Socket`** — RAII wrapper around the fd (opens in ctor, `close` in dtor; copy/move deleted).
- **`Client<Transport>` / `Server<Transport>`** — thin endpoints that forward to the transport static methods.
- **`EchoClient<Transport>`** — `exchange(send_params, recv_buf)` does one send + one recv (one round trip).
- **`EchoServer<Transport>`** — binds in the constructor; `run()` loops recv → echo.

**Type aliases:** `UdpEchoClient`, `UdsEchoClient`, `UdpEchoServer`, `UdsEchoServer`.

UDP needs no local bind on the client; UDS datagram clients bind a local socket path once (see `Uds::SendParams::client_path`) so the server knows where to reply.

**How:**

```bash
make test-ipc         # in-process threaded benchmark (UDP then UDS, 5s each)
make test-ipc-mp      # true two-process test: echo_server + echo_client
make ipc-echo-server  # build build/ipc/test/echo_server
make ipc-echo-client  # build build/ipc/test/echo_client
```

Two-process manual test (two terminals):

```bash
./build/ipc/test/echo_server udp
./build/ipc/test/echo_client udp

./build/ipc/test/echo_server uds
./build/ipc/test/echo_client uds
```

**Outputs:** Round-trip counts printed to stdout (e.g. `UDP round trips in 5s: …`).

---

### UDP echo (standalone)

**What:** Minimal UDP echo **udp_echo_server** and **udp_echo_client** programs (raw sockets, no `ipc.h`).

**When:** Stepping through socket API basics before the templated IPC library.

**How:**

```bash
make udp_echo_server udp_echo_client
./build/udp_echo_server/udp_echo_server 9000          # terminal 1
./build/udp_echo_client/udp_echo_client 127.0.0.1 9000 hello   # terminal 2
```

**Outputs:** Server logs datagrams to stderr; client prints `echo: …` to stdout.

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
| `build/interrogate/<program>/` | `.i`, `.s`, `.o`, binary per program |
| `build/interrogate/site/index.html` | walkthrough UI (commands + tool output per stage) |
| `build/interrogate/site/meta.json` | run metadata |

**Options:**

| Flag | Default | Purpose |
|------|---------|---------|
| `--programs-root` | `cpp_tricks` | root folder containing one directory per program |
| `--program` | all discovered | build only named program(s); repeat flag for multiple |
| `--std` | `c++20` | `-std=` passed to `g++` |
| `--artifacts-dir` | `build/interrogate` | where `.i` / `.s` / `.o` / binary go |
| `--site-dir` | `build/interrogate/site` | HTML output directory |
| `--clean` | off | remove artifact and site dirs before build |
| `--no-debug` | off | omit `-g -O0` (less useful `objdump -S` source lines) |

**Note:** The **Run** stage may fail for programs that block or require CLI args (e.g. `udp_echo_server`); compile/link stages and the HTML walkthrough are still valid.

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
