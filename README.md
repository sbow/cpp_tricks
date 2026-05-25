# cpp_tricks

Small C++ examples, experiments, and tooling to explore how programs are built and run.

## Contents

- [Features](#features)
- [Architecture](#architecture)
- [Project layout](#project-layout)
- [Prerequisites](#prerequisites)
- [Feature guides](#feature-guides)
  - [Makefile builds](#makefile-builds)
  - [IPC library & tests](#ipc-library--tests)
  - [Message router](#message-router)
  - [UDP echo (standalone)](#udp-echo-standalone)
  - [Build pipeline walkthrough](#build-pipeline-walkthrough)

---

## Features

Catalog of what lives in this repo. Add a row when you introduce something new, then document it under [Feature guides](#feature-guides).

| Feature | Type | Status | Guide |
|---------|------|--------|-------|
| Makefile builds | workflow | active | [§ Makefile](#makefile-builds) |
| IPC library (`ipc`) | topic | active | [§ IPC](#ipc-library--tests) |
| Message router | topic | active | [§ Router](#message-router) · [ADR 0001](docs/adr/0001-ipc-and-router.md) |
| `basic` | topic | active | [§ Makefile](#makefile-builds) |
| `udp_echo_server` | topic | active | [§ UDP echo](#udp-echo-standalone) |
| `udp_echo_client` | topic | active | [§ UDP echo](#udp-echo-standalone) |
| SHM SPSC transport (`ShmSpsc`) | topic | planned | [design note](cpp_tricks/ipc/SHM_SPSC_TRANSPORT.md) |
| Build pipeline walkthrough | tool | active | [§ walkthrough](#build-pipeline-walkthrough) |

**Type:** `workflow` = everyday commands; `tool` = scripts/automation; `topic` = a C++ concept demonstrated under `cpp_tricks/<name>/`.

**Status:** `active` · `planned` · `deprecated`

---

## Architecture

Design decisions for the IPC stack and router are recorded as [Architecture Decision Records (ADRs)](docs/adr/):

| ADR | Title |
|-----|-------|
| [0001](docs/adr/0001-ipc-and-router.md) | Header-only IPC library and message router |

---

## Project layout

```
cpp_tricks/                          # repo root
├── Makefile                         # build all programs and ipc tests
├── docs/adr/                        # architecture decision records
│   └── 0001-ipc-and-router.md
├── cpp_tricks/
│   ├── basic/src/main.cpp           # hello-world example
│   ├── ipc/src/ipc.h                # header-only UDP/UDS IPC library
│   ├── ipc/src/router_protocol.h    # generic 32-byte frames, registry, router_forward
│   ├── ipc/src/router_app.h           # signal handlers, ROUTER_TEST flag (demo binaries)
│   ├── ipc/test/                    # echo + router tests (see below)
│   │   ├── router_client_config.h   # sample app: sensor/controller/recorder topology
│   │   ├── router_client.cpp        # sample app: role behavior + CLI
│   │   ├── router_server.cpp        # sample app: router process
│   │   └── router_test.cpp          # integration test (fork/exec)
│   ├── ipc/SHM_SPSC_TRANSPORT.md    # planned ShmSpsc design (spin → eventfd)
│   ├── udp_echo_server/src/main.cpp
│   └── udp_echo_client/src/main.cpp
├── build/                           # generated binaries (gitignored)
├── scripts/
│   └── interrogate.py               # compile-stage walkthrough → HTML site
├── pyproject.toml                   # Python tooling (uv)
└── README.md
```

IPC test binaries under `build/ipc/test/`:

| Binary | Purpose |
|--------|---------|
| `echo_tests` | In-process threaded UDP + UDS echo benchmark |
| `echo_server`, `echo_client`, `echo_client_benchmark` | Multi-process echo |
| `router_server` | Central message router |
| `router_client` | Sensor / controller / recorder roles |
| `router_test` | Fork/exec integration test (UDS then UDP) |

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

**What:** Header-only IPC library in `cpp_tricks/ipc/src/ipc.h` — UDP and Unix-domain datagram clients/servers via transport traits and templates (`UdpEchoClient`, `UdsEchoServer`, etc.). The [message router](#message-router) builds on the same layer.

**When:** Learning sockets, comparing UDP vs UDS, benchmarking round-trip latency, or extending toward multi-process routing.

**Architecture:** See [ADR 0001: IPC and router](docs/adr/0001-ipc-and-router.md) for the layered design (transport → router protocol → sample application).

#### About `ipc.h`

`ipc.h` is a single, header-only C++ library (no `.cpp` to link — just `#include "ipc.h"` with `-Icpp_tricks/ipc/src`). It wraps Linux datagram sockets behind a small template layer so the same client/server code works for two transports:

| Transport | Socket API | Address |
|-----------|------------|---------|
| `Udp` | `AF_INET` + `SOCK_DGRAM` | IP host + port |
| `Uds` | `AF_UNIX` + `SOCK_DGRAM` | filesystem path |

**Core pieces:**

- **`Buffer`** — non-owning view over caller memory (`writable` / `read_only`); no heap allocation on the hot path.
- **`Udp` / `Uds`** — transport traits with static `bind`, `connect_or_send`, `recv_from`, `send_to`, and `echo` methods plus per-transport `BindParams`, `SendParams`, and `RecvResult` types.
- **`Socket`** — RAII wrapper around the fd (opens in ctor, `close` in dtor; copy/move deleted).
- **`Client<Transport>` / `Server<Transport>`** — reusable endpoints: own a `Socket`, expose `fd()`, forward to transport static methods. Both support `set_recv_timeout_ms` for poll/wait logic.
- **`EchoClient<Transport>`** — extends `Client`; `exchange(send_params, recv_buf)` does one send + one recv (one round trip).
- **`EchoServer<Transport>`** — extends `Server`; binds in the constructor; `echo(recv, payload)` and `run()` for recv → echo loops.

**Type aliases:** `UdpClient`, `UdsClient`, `UdpServer`, `UdsServer`, `UdpEchoClient`, `UdsEchoClient`, `UdpEchoServer`, `UdsEchoServer`.

UDP needs no local bind on the client; UDS datagram clients bind a local socket path once (see `Uds::SendParams::client_path`) so the server knows where to reply.

**Planned:** [`ShmSpsc` transport](cpp_tricks/ipc/SHM_SPSC_TRANSPORT.md) — Phase 1: shared-memory SPSC ring with spin; Phase 2: optional `eventfd` wake for idle (hybrid).

**How:**

```bash
make test-ipc         # in-process threaded benchmark (UDP then UDS, 5s each)
make test-ipc-mp      # true two-process benchmark (echo_client_benchmark)
make ipc-echo-server  # build build/ipc/test/echo_server
make ipc-echo-client  # build build/ipc/test/echo_client
make ipc-echo-client-benchmark  # build build/ipc/test/echo_client_benchmark
```

Two-process manual test (two terminals):

```bash
./build/ipc/test/echo_server udp
./build/ipc/test/echo_client udp          # one round trip (prints echo)
./build/ipc/test/echo_client udp 127.0.0.1 19000 10   # N round trips

./build/ipc/test/echo_server uds
./build/ipc/test/echo_client uds
```

Benchmark client (timed run, prints trips/sec summary):

```bash
./build/ipc/test/echo_client_benchmark udp 127.0.0.1 19000 5
./build/ipc/test/echo_client_benchmark uds
```

**Outputs:** Round-trip counts printed to stdout (e.g. `UDP round trips in 5s: …`).

---

### Message router

**What:** A multi-process routing demo on top of `ipc.h`. A central **router** forwards fixed **32-byte frames** to peers; clients bind well-known addresses and send via `RouterClient`. The **generic protocol** lives in `router_protocol.h`; the **sensor / controller / recorder sample app** lives under `cpp_tricks/ipc/test/` (`router_client_config.h`, `router_client.cpp`, `router_server.cpp`).

**When:** Exercising fan-out IPC, fixed binary messages, and transport-agnostic code (`RouterClient<Uds>` vs `RouterClient<Udp>`). Use the protocol headers for your own topology; copy or replace the sample app files for a different scenario.

**Architecture:** See [ADR 0001](docs/adr/0001-ipc-and-router.md). Layers:

| Layer | File(s) | Responsibility |
|-------|---------|----------------|
| Transport | `ipc.h` | `Udp` / `Uds` traits, `Client` / `Server`, echo helpers |
| Router protocol | `router_protocol.h` | `RouterFrame`, `EndpointRegistry`, `router_forward`, `RouterClient` |
| App utilities | `router_app.h` | Signal handlers, `ROUTER_TEST` mode (demo binaries) |
| Sample app | `router_client_config.h`, `router_*.cpp` | Endpoint table, routing rules, roles, integration test |

#### `router_protocol.h` (generic)

- **`RouterFrame`** — 32-byte frame: byte 0 = source id, bytes 1–9 = router timestamp (9-byte BE ns), bytes 10–31 = payload (22 bytes). Methods: `init`, `set_source`, `set_timestamp_ns`, `set_payload`, `describe`.
- **`EndpointRegistry`** — app-defined endpoint table + router bind address (`router_uds_path`, `router_udp_host`, `router_udp_port`). Lookup via `endpoint_by_id`, `endpoint_by_name`, `endpoint_by_uds_path`, `endpoint_by_udp_port`.
- **`RouteRule` / `route_targets_for`** — routing table supplied by the application (not hard-coded in the protocol).
- **`router_forward(server, reg, rules, rule_count, frame, ts)`** — recv, authenticate source from socket address, stamp frame, fan-out to targets.
- **`RouterClient<Transport>(reg, endpoint_id)`** — bind local address from registry; `send_message`, `recv_message`, `recv_message_blocking_until`.

Only **`kEndpointInvalid`** (0) and **`kEndpointServer`** (255) are protocol-level ids; all other endpoint ids and names are application-defined.

#### Sample app (`router_client_config.h`)

Demo topology (also used by `router_server` and `router_test`):

| Id | Role | Routing |
|----|------|---------|
| 1 | sensor | → controller and recorder |
| 2 | controller | → recorder |
| 3 | recorder | receive-only |

Well-known UDS paths and UDP ports are in `router_client_config.h`. The router identifies senders from **socket address**, not frame byte 0 (the router stamps byte 0 on forward).

**Roles:** `sensor` publishes readings; `controller` waits for a sensor packet before each control message; `recorder` logs everything it receives.

**How:**

```bash
make test-router          # integration test: fork 4 processes, UDS then UDP
make ipc-router-server    # build build/ipc/test/router_server
make ipc-router-client    # build build/ipc/test/router_client
```

Manual run (four terminals, UDS example):

```bash
./build/ipc/test/router_server uds
./build/ipc/test/router_client sensor uds
./build/ipc/test/router_client controller uds
./build/ipc/test/router_client recorder uds
```

Use `udp` instead of `uds` for the UDP transport. Paths, ports, and logs default to `router_client_config.h`.

**Outputs:** Manual runs log to stderr. Controller and recorder write CSV logs under `/tmp/cpp_tricks_router_*.log`. `router_test` sets `ROUTER_TEST=1`, silences child stdout/stderr, and prints `router test passed` on success.

**Further reading:** [ADR 0001: Header-only IPC library and message router](docs/adr/0001-ipc-and-router.md)

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
