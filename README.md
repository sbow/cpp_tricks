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
| Message router | topic | active | [§ Router](#message-router) · [ADR 0001](docs/adr/0001-ipc-and-router.md) · [ADR 0002](docs/adr/0002-ipc-router-refactor.md) |
| `basic` | topic | active | [§ Makefile](#makefile-builds) |
| `udp_echo_server` | topic | active | [§ UDP echo](#udp-echo-standalone) |
| `udp_echo_client` | topic | active | [§ UDP echo](#udp-echo-standalone) |
| SHM SPSC transport (`ShmSpsc`) | topic | active | [§ IPC](#ipc-library--tests) · [design note](cpp_tricks/ipc/SHM_SPSC_TRANSPORT.md) |
| Build pipeline walkthrough | tool | active | [§ walkthrough](#build-pipeline-walkthrough) |

**Type:** `workflow` = everyday commands; `tool` = scripts/automation; `topic` = a C++ concept demonstrated under `cpp_tricks/<name>/`.

**Status:** `active` · `planned` · `deprecated`

---

## Architecture

Design decisions for the IPC stack and router are recorded as [Architecture Decision Records (ADRs)](docs/adr/):

| ADR | Title |
|-----|-------|
| [0001](docs/adr/0001-ipc-and-router.md) | Header-only IPC library and message router |
| [0002](docs/adr/0002-ipc-router-refactor.md) | IPC/router refactor (transport concept, topology, links) |
| [0003](docs/adr/0003-transport-agnostic-router.md) | Peer-address adapters, `RouterLink`, `TransportKind` factories |

---

## Project layout

```
cpp_tricks/                          # repo root
├── Makefile                         # build all programs and ipc tests
├── docs/adr/                        # architecture decision records
│   ├── 0001-ipc-and-router.md
│   ├── 0002-ipc-router-refactor.md
│   └── 0003-transport-agnostic-router.md
├── cpp_tricks/
│   ├── basic/src/main.cpp           # hello-world example
│   ├── ipc/src/ipc.h                # umbrella include → ipc/
│   ├── ipc/src/ipc/                 # buffer, transport, datagram, endpoint, echo, shm_spsc
│   ├── ipc/src/router_protocol.h    # umbrella include → router/
│   ├── ipc/src/router/              # frame, routing, peer_table, link, adapters, factory
│   ├── ipc/src/router_app.h         # signal handlers, ROUTER_TEST flag (demo binaries)
│   ├── ipc/test/                    # echo + router tests (see below)
│   │   ├── router_client_config.h   # sample app: RouterTopology + route rules
│   │   ├── router_client.cpp        # sample app: RouterClient + roles
│   │   ├── router_server.cpp        # sample app: RouterServer + forward loop
│   │   └── router_test.cpp          # integration test (fork/exec)
│   ├── ipc/SHM_SPSC_TRANSPORT.md    # ShmSpsc design (spin; eventfd later)
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
| `echo_tests` | In-process threaded UDP + UDS + SHM echo benchmark |
| `echo_server`, `echo_client`, `echo_client_benchmark` | Multi-process echo (udp / uds / shm) |
| `router_server` | Central message router |
| `router_client` | Sensor / controller / recorder roles |
| `router_test` | Fork/exec integration test (UDS, UDP, SHM) |

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

**What:** Header-only IPC library (`#include "ipc.h"` → `ipc/`) — UDP, UDS, and shared-memory SPSC echo via a C++20 **`Transport`** concept and **`IpcEndpoint<Transport>`**. The [message router](#message-router) builds on the datagram layer.

**When:** Learning sockets and SHM rings, comparing transport latency, or extending toward multi-process routing.

**Architecture:** [ADR 0001](docs/adr/0001-ipc-and-router.md) (original story) and [ADR 0002](docs/adr/0002-ipc-router-refactor.md) (current layout).

#### About `ipc.h` / `ipc/`

Include with `-Icpp_tricks/ipc/src`. No `.cpp` to link for datagram modes; SHM uses `-lrt` (`shm_open`).

| Transport | Handle | Address |
|-----------|--------|---------|
| `Udp` | `Socket` (fd) | IP host + port |
| `Uds` | `Socket` (fd) | filesystem path |
| `ShmSpsc` | `unique_ptr<ShmRegion>` | POSIX SHM name (`/cpp_tricks_shm_echo`) |

**Core pieces:**

- **`ipc/buffer.hpp`** — `Buffer` (non-owning view).
- **`ipc/transport.hpp`** — `Transport` and `DatagramTransport` concepts.
- **`ipc/datagram.hpp`** — `Udp`, `Uds`, `Socket` (movable fd for router links).
- **`ipc/endpoint.hpp`** — `IpcEndpoint<Transport>`; aliases `Client` / `Server`.
- **`ipc/echo.hpp`** — `EchoClient` / `EchoServer` (`exchange`, `run`).
- **`ipc/shm_spsc.hpp`** — spin-only SPSC rings (see [SHM_SPSC_TRANSPORT.md](cpp_tricks/ipc/SHM_SPSC_TRANSPORT.md)).

UDP clients need no local bind; UDS clients bind a path once (`Uds::SendParams::client_path`). SHM uses creator (`create=true`) server and joining client.

**How:**

```bash
make test-ipc         # in-process threaded benchmark (UDP, UDS, SHM — 5s each)
make test-ipc-mp      # two-process udp + uds + shm (echo_client_benchmark)
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

./build/ipc/test/echo_server shm
./build/ipc/test/echo_client_benchmark shm /cpp_tricks_shm_echo 5
```

Benchmark client (timed run, prints trips/sec summary):

```bash
./build/ipc/test/echo_client_benchmark udp 127.0.0.1 19000 5
./build/ipc/test/echo_client_benchmark uds
./build/ipc/test/echo_client_benchmark shm /cpp_tricks_shm_echo 5
```

**Outputs:** Round-trip counts printed to stdout (e.g. `UDP round trips in 5s: …`, `SHM round trips in 5s: …`).

#### Shutdown & cleanup

Long-running test binaries (`echo_server`, `router_server`, `router_client`) install **SIGTERM/SIGINT** handlers via `ipc/app_shutdown.hpp` (re-exported from `router_app.h` as `router_stop_flag()`). They do not rely on killing a thread stuck in a blocking `recv`; loops **poll** with a short recv timeout (200 ms for datagrams) or **`ShmSpsc::try_recv`** + `yield` for SHM, and check the stop flag each iteration.

| Mechanism | Purpose |
|-----------|---------|
| `EchoServer::run_until(stop)` | Echo server exits cleanly after `kill` or Ctrl+C |
| `RouterServer` recv timeout | Router forward loop observes SIGTERM between polls |
| `recv_message_until(..., stop)` | Controller waits for sensor without blocking forever |
| `SO_REUSEADDR` on bind | Faster UDP/UDS restart after a crashed process |
| `unlink` before UDS bind | Stale socket paths removed on startup (router, echo, link) |
| SHM creator destructor / `shm_unlink` | Region removed when creator exits normally |
| `make test-ipc-mp` `ipc_stop_pid` | SIGTERM, wait up to 5 s, then SIGKILL; cleans UDS paths and `/dev/shm/cpp_tricks_shm_echo` |
| `router_test` `cleanup_paths()` | UDS sockets and demo log files removed between scenarios |

**Stopping manually**

```bash
# Prefer SIGTERM so handlers run and sockets close
kill -TERM <pid>

# Ctrl+C in the foreground is equivalent (SIGINT)
```

**Caveats:** `SIGKILL` skips destructors — UDS paths or SHM objects may linger until the next `unlink`/`cleanup_paths` or reboot. Benchmark code may still use **blocking** SHM `recv` on the hot path; that is intentional for throughput, not for shutdown. Integration tests set `ROUTER_TEST=1` so the router and recorder can **idle-exit** after traffic stops, in addition to signal handling.

---

### Message router

**What:** A multi-process routing demo on top of `ipc.h`. A central **router** forwards fixed **32-byte frames**; peers use **`RouterServer`** / **`RouterClient`** facades. Generic code lives under `router/` (included via `router_protocol.h`); the **sensor / controller / recorder** sample is in `cpp_tricks/ipc/test/`.

**When:** Fan-out IPC, fixed binary messages, and transport-agnostic routing. Define your own `RouterTopology` in app config; pick **`TransportKind`** at the CLI boundary — no `Udp`/`Uds` templates in role code.

**Architecture:** [ADR 0002](docs/adr/0002-ipc-router-refactor.md), [ADR 0003](docs/adr/0003-transport-agnostic-router.md). Layers:

| Layer | Path | Responsibility |
|-------|------|----------------|
| Transport | `ipc/` | `Udp` / `Uds` / `ShmSpsc`, `IpcEndpoint`, echo |
| Router core | `router/` | `RouterFrame`, `RouteRule`, `RouterTopology`, `RouterLink` concept |
| Router adapters | `peer_address_io.hpp`, `datagram_peer_resolver.hpp` | Bind/send/identity per `PeerAddress` |
| Router I/O | `link.hpp`, `shm_router_link.hpp`, `node.hpp`, `factory.hpp` | `DatagramRouterLink`, `ShmRouterLink` (one SPSC ring per peer), facades, `TransportKind` dispatch |
| App utilities | `router_app.h` | Signal handlers, `ROUTER_TEST` mode (see [Shutdown & cleanup](#shutdown--cleanup)) |
| Sample app | `router_client_config.h`, `router_*.cpp` | Topology, rules, roles, integration test |

#### `router/` (generic)

- **`RouterFrame`** (`frame.hpp`) — 32-byte wire format; router stamps source and timestamp on forward.
- **`RouterTopology` / `PeerAddress`** (`peer_table.hpp`) — transport-agnostic peer table + router listen address.
- **`RouteRule` / `route_targets_for`** (`routing.hpp`) — application-supplied routing table.
- **`DatagramRouterLink<Transport>`** (`link.hpp`) — recv, stamp, fan-out via peer-address adapters (no `Uds`/`Udp` branches in link).
- **`RouterServer` / `RouterClient`** (`node.hpp`) — `run()`, `send_message`, `recv_message_until`.
- **`factory.hpp`** — `dispatch_transport_kind`, `make_router_server`, `bind_router_listen`.

Only **`kEndpointInvalid`** (0) and **`kEndpointServer`** (255) are protocol-level ids.

#### App integration (sketch)

```cpp
#include "router/factory.hpp"
#include "router_client_config.h"

install_router_stop_handlers();

// Templates only inside functors passed to dispatch_transport_kind.
return dispatch_transport_kind(argv[1], ServerRunner{argc, argv});

// Inside ServerRunner::operator()<Transport>():
const RouterTopology& topo = demo_topology(
    std::is_same_v<Transport, Udp> ? TransportKind::Udp : TransportKind::Uds);
auto server = make_router_server<Transport>(topo);
bind_router_listen(server, topo);
server.run(rules, rule_count, now_ns, on_forward, opts);
```

See [`router_server.cpp`](cpp_tricks/ipc/test/router_server.cpp) for the full pattern: `RouterServer::run` + `RouterRunOptions` for poll/idle-exit.

#### Sample app (`router_client_config.h`)

`demo_topology(TransportKind)` and `kDemoRouteRules` (used by `router_server` and `router_test`):

| Id | Role | Routing |
|----|------|---------|
| 1 | sensor | → controller and recorder |
| 2 | controller | → recorder |
| 3 | recorder | receive-only |

Well-known UDS paths and UDP ports are in `router_client_config.h`. The router identifies senders from **socket address**, not frame byte 0 (the router stamps byte 0 on forward).

**Roles:** `sensor` publishes readings; `controller` waits for a sensor packet before each control message; `recorder` logs everything it receives.

**How:**

```bash
make test-router          # integration test: fork 4 processes, UDS + UDP + SHM
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

Use `udp` instead of `uds` for the UDP transport, or `shm` for shared-memory rings (one POSIX SHM object per peer; the router creates each ring, clients join their own). SHM peer identity on receive is **which ring** had data, not a socket address. Paths, ports, SHM names, and logs default to `router_client_config.h`.

```bash
./build/ipc/test/router_server shm
./build/ipc/test/router_client sensor shm
./build/ipc/test/router_client controller shm
./build/ipc/test/router_client recorder shm
```

**Outputs:** Manual runs log to stderr. Controller and recorder write CSV logs under `/tmp/cpp_tricks_router_*.log`. `router_test` sets `ROUTER_TEST=1`, silences child stdout/stderr, and prints `router test passed` on success.

**Further reading:** [ADR 0001](docs/adr/0001-ipc-and-router.md) · [ADR 0002](docs/adr/0002-ipc-router-refactor.md) · [ADR 0003](docs/adr/0003-transport-agnostic-router.md)

**Robotics module roadmap (AI-executable):** [robotics-ipc-module/](robotics-ipc-module/) — design principles, lessons learned, system vision (Jetson/x86/HIL/Python/Node/MAVLink/cameras), phases A–F, skills (`@ipc-robotics-orchestrator`), `./robotics-ipc-module/install.sh`.

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
