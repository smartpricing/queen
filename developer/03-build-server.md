# 03 — Building the server

This page is about building the C++ broker (`server/bin/queen-server`). For the runtime/operator perspective (tuning, env vars, systemd), see `[server/README.md](../server/README.md)`.

---

## What you're building

```
server/
├── Makefile                  ← drives everything
├── build.sh                  ← convenience wrapper around make
├── bin/queen-server          ← the output
├── build/                    ← object files + PCH (gitignored)
├── vendor/                   ← downloaded deps (gitignored)
├── src/                      ← broker source
└── include/                  ← broker headers + PCH source
```

Build inputs:

- **System libraries:** `libpq` (Postgres client), `libssl`/`libcrypto`, `libz`. Installed via your package manager.
- **Vendored libraries:** uWebSockets, uSockets, libuv, spdlog, json.hpp, jwt-cpp, cpp-httplib. Downloaded automatically by `make deps` into `server/vendor/`.
- **libqueen** headers from `lib/`. Included via `-I../lib`.

---

## Quick build

```bash
cd server
make all
```

What this does:

1. `make deps` — downloads + extracts vendor libs (idempotent; skipped on subsequent runs)
2. Builds `vendor/libuv/build/libuv.a` (the only vendored library compiled as a static lib)
3. Compiles uSockets `.c`/`.cpp` sources (uWebSockets is header-only)
4. Compiles the broker sources with a precompiled header (`server/include/pch.hpp`) so spdlog and `json.hpp` are parsed once instead of 30+ times
5. Links → `bin/queen-server`

Cold build: ~3 minutes. Warm rebuild after touching one `.cpp`: ~5 seconds.

### Make targets

```bash
make all          # full build (deps + binary)
make build-only   # build, skip dep download
make deps         # only download deps
make clean        # remove build/ and bin/
make distclean    # also remove vendor/
make dev          # build + run with relaxed flags
make test         # run tests (delegates to lib/Makefile)
make debug-paths  # show detected include/lib paths
make help         # list all targets
```

### Build script (alternative)

`server/build.sh` is a thin wrapper that prints status messages and verifies dependencies before calling `make`. Useful when you want clearer output:

```bash
./build.sh                  # release build
./build.sh --debug          # -O0 -g
./build.sh --clean          # clean before building
./build.sh --deps-only      # download deps without compiling
./build.sh --verbose        # echo every command
```

---

## Compiler flags

From `server/Makefile`:

```make
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -pthread -DWITH_OPENSSL=1 -DCPPHTTPLIB_OPENSSL_SUPPORT
```

Override on the command line:

```bash
# Maximum optimization (per-host binary)
CXXFLAGS="-std=c++17 -O3 -march=native" make build-only

# Debug build
CXXFLAGS="-std=c++17 -g -O0 -DDEBUG" make build-only

# Sanitizers
CXXFLAGS="-std=c++17 -g -O1 -fsanitize=address,undefined" \
LDFLAGS="-fsanitize=address,undefined" \
  make build-only
```

The Makefile auto-detects (run `make debug-paths` to see what it found):

- macOS Homebrew prefix (`/opt/homebrew` on Apple Silicon, `/usr/local` on Intel)
- Pinned Postgres versions (`postgresql@14`, `postgresql@16`, …)
- OpenSSL location (`openssl@3` preferred)

---

## Vendor dependencies

Downloaded by `make deps`:


| Library       | Version       | Used for                           |
| ------------- | ------------- | ---------------------------------- |
| uWebSockets   | `v20.76.0`    | HTTP/WebSocket server              |
| uSockets      | pinned commit | uWebSockets transport              |
| libuv         | `v1.48.0`     | event loop (built as static lib)   |
| spdlog        | `v1.14.1`     | logging                            |
| nlohmann/json | `v3.11.3`     | single-header JSON                 |
| jwt-cpp       | `v0.7.0`      | JWT validation (when auth enabled) |
| cpp-httplib   | latest master | only used by some tests            |


To bump a version, edit the `*_VERSION` / `*_URL` variables at the top of `server/Makefile`, then `make distclean && make all`.

> Downloads go to `server/vendor/`. This folder is `.gitignore`d. Don't commit it.

---

## Linux build

The Linux path is the simpler of the two. Required system packages:

```bash
sudo apt-get install -y \
  build-essential libpq-dev libssl-dev zlib1g-dev curl unzip
```

The Makefile assumes Postgres headers in `/usr/include/postgresql` (the `libpq-dev` default).

For Alpine, install `postgresql-dev openssl-dev zlib-dev g++ make curl unzip`. The build works but you'll want to compile with `-D_GLIBCXX_USE_CXX11_ABI=1`.

---

## macOS build

```bash
brew install postgresql openssl curl unzip
```

If you've installed a versioned Postgres (`postgresql@14`, `postgresql@16`), the Makefile finds it automatically — verify with:

```bash
make debug-paths
```

You should see lines like:

```
PG includes: /opt/homebrew/include/postgresql@16
SSL includes: /opt/homebrew/opt/openssl@3/include
```

If those are wrong, set `PG_CONFIG`:

```bash
PG_CONFIG=/opt/homebrew/opt/postgresql@16/bin/pg_config make
```

---

## Docker build

The repo's `[Dockerfile](../Dockerfile)` is a multi-stage build that:

1. Builds the Vue dashboard (Node 22 stage)
2. Builds the C++ broker (Ubuntu 24.04 + ccache stage)
3. Copies binary + dashboard `dist/` + `lib/schema/` into a slim runtime image
4. Final image is ~250 MB and includes `psql` / `pg_dump` / `pg_restore` (PostgreSQL 18 client tools)

Build it:

```bash
DOCKER_BUILDKIT=1 docker build -t queen-mq:dev .
```

The `ccache` mount and split copy layers (Makefile copied first, sources copied second) mean source-only changes rebuild in about 30 seconds.

---

## Troubleshooting

### `pg_config: command not found` (macOS)

```bash
brew install postgresql
# or
brew install postgresql@16 && brew link postgresql@16
```

### `libpq-fe.h: No such file or directory`

Linux: `sudo apt-get install libpq-dev`.
macOS: see above; then `make debug-paths` to confirm the Makefile sees them.

### `undefined reference to SSL_CTX_*`

OpenSSL not found or wrong version. Linux: `sudo apt-get install libssl-dev`. macOS: `brew install openssl@3`.

### Build is slow on every rebuild

Make sure you're using the precompiled header. After a fresh `make distclean`, the *first* compile builds the PCH (~5 s) and *subsequent* TUs reuse it. If rebuilds are slow, your `build/pch.hpp.gch` is being invalidated — check that you're not running `make clean` between builds.

### "Linking error: file too short" on macOS

Almost always means a `.o` was killed mid-compile (out-of-memory or interrupted build). `make clean && make all` fixes it.

### Container build pulls deps every time

Make sure you're using BuildKit (`DOCKER_BUILDKIT=1`) so the `--mount=type=cache` for ccache works, and that you haven't reordered the layers in `Dockerfile`. Vendor dep download is cached on the `Makefile` layer alone.

---

## Building libqueen on its own

libqueen has its own `lib/Makefile` for running its tests in isolation (no broker required). See [07 — Testing](07-testing.md) — but you don't compile libqueen as part of the broker build; the broker just `#include`s its headers.

---

## Cross-component dependencies

```
DEVELOPING.md
        │
        ▼
  developer/03  ← you are here
        │
        ▼
  server/Makefile
        │
        ├──▶ server/vendor/   (downloaded; gitignored)
        ├──▶ server/src/*.cpp + server/include/*.hpp
        ├──▶ lib/queen.hpp + lib/queen/*.hpp     (header-only libqueen)
        └──▶ system: libpq, libssl, libz, pthread
```

Once compiled, the binary is **fully self-contained except for**:

- `libpq.so` / `libssl.so` / `libcrypto.so` / `libz.so` from the OS (statically link if you really need to ship a single binary)
- `lib/schema/schema.sql` and `lib/schema/procedures/*.sql` at runtime (the broker reads them on startup; the Docker image bundles them at `/app/schema`)

