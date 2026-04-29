# 01 — Getting started

This page takes you from "fresh clone" to "broker running, message pushed, message popped" in about 5–10 minutes. If you've already done this, jump to [02 — Architecture](02-architecture.md).

---

## 1. Prerequisites


| Tool                          | Version              | Why                           |
| ----------------------------- | -------------------- | ----------------------------- |
| C++ compiler                  | g++ 9+ or clang 10+  | Broker is C++17               |
| `make`                        | any                  | Broker build system           |
| `curl`, `unzip`               | any                  | Vendor dependency download    |
| **PostgreSQL client headers** | 12+                  | `libpq-fe.h` for the broker   |
| **OpenSSL headers**           | 1.1.1 / 3.x          | TLS for clients/Postgres      |
| **zlib headers**              | any                  | Used by uWebSockets           |
| Postgres server               | 14+ (16 recommended) | The actual database           |
| Node.js                       | **22+**              | Frontend, JS client, examples |
| Python                        | 3.8+                 | Python client                 |
| Go                            | 1.24+                | Go client                     |
| PHP                           | 8.3+ + Composer      | PHP/Laravel client            |


> Use `nvm use 22 &&` before any `npm` command so versions match CI and the JS client's `engines` field.

### Install on macOS

```bash
brew install postgresql openssl curl unzip
# Optional: pinned PostgreSQL major version
# brew install postgresql@16
```

### Install on Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential libpq-dev libssl-dev zlib1g-dev curl unzip
```

The broker's Makefile (`server/Makefile`) also offers helpers:

```bash
cd server
make install-deps-macos   # or
make install-deps-ubuntu
```

---

## 2. Set up PostgreSQL

You don't have to do anything special to "prepare" the database. **The broker bootstraps its own schema on first start.** It will:

1. Create the `queen` schema (`CREATE SCHEMA IF NOT EXISTS queen`)
2. Apply `[lib/schema/schema.sql](../lib/schema/schema.sql)` (idempotent — safe to re-run)
3. Load every file in `[lib/schema/procedures/*.sql](../lib/schema/procedures/)` using `CREATE OR REPLACE FUNCTION` (also idempotent)

You just need a Postgres reachable on a known host/port with credentials.

### Option A — Docker (recommended for dev)

```bash
docker network create queen 2>/dev/null || true
docker run --name qpg --network queen \
  -e POSTGRES_PASSWORD=postgres \
  -p 5433:5432 \
  -d postgres:16
```

> Port `5433` (not `5432`) so it doesn't clash with a Postgres you may already have on your laptop.

### Option B — local Homebrew

```bash
brew services start postgresql@16
createdb -p 5432 queen_dev
```

### Option C — already have a remote Postgres

You only need: a host, port, user, password, and a database the broker can write to. The broker will create the `queen` schema inside it.

> The broker user does **not** need superuser. It needs `CREATE` on the database (to create the `queen` schema), and `ALL` on the `queen` schema afterward. For Cloud SQL/RDS, granting the regular owner role is enough.

### Recommended `postgresql.conf` (production only)

For a real deployment, see `[server/README.md` § PostgreSQL Server Tuning](../server/README.md#postgresql-server-tuning). For local dev, the defaults are fine.

---

## 3. Build the broker

```bash
cd server
make all          # downloads vendor deps, then compiles
```

First run downloads ~6 archives (uWebSockets, uSockets, libuv, spdlog, json.hpp, jwt-cpp, cpp-httplib) into `server/vendor/`. Subsequent rebuilds are incremental.

Output: `server/bin/queen-server`.

If the build fails see [03 — Building the server](03-build-server.md).

---

## 4. Run the broker

```bash
cd server
PG_HOST=localhost \
PG_PORT=5433 \
PG_USER=postgres \
PG_PASSWORD=postgres \
PG_DB=postgres \
./bin/queen-server
```

You should see something like:

```
[info] Initializing schema: queen
[info] Running base schema initialization...
[info] Base schema applied successfully
[info] Loading 18 stored procedure(s)...
[info] Queen server listening on 0.0.0.0:6632
[info] ✅ Queen server is ready and listening
```

Useful environment variables for dev (full reference: `[server/ENV_VARIABLES.md](../server/ENV_VARIABLES.md)`):


| Variable       | Default | When to change                                   |
| -------------- | ------- | ------------------------------------------------ |
| `PORT`         | `6632`  | Run two brokers on the same host                 |
| `NUM_WORKERS`  | `10`    | Stress-test or low-spec laptop (`NUM_WORKERS=2`) |
| `DB_POOL_SIZE` | `150`   | Lower for laptop dev (`DB_POOL_SIZE=20`)         |
| `LOG_LEVEL`    | `info`  | `debug` while changing the broker                |


A relaxed dev recipe:

```bash
LOG_LEVEL=debug NUM_WORKERS=2 DB_POOL_SIZE=20 \
PG_PORT=5433 PG_PASSWORD=postgres \
./bin/queen-server
```

---

## 5. Smoke test

```bash
curl http://localhost:6632/health

curl -X POST http://localhost:6632/api/v1/push \
  -H 'Content-Type: application/json' \
  -d '{"items":[{"queue":"demo","payload":{"hello":"world"}}]}'

curl 'http://localhost:6632/api/v1/pop?queue=demo&batch=1&wait=false'
```

Open the dashboard at [http://localhost:6632/](http://localhost:6632/) (the broker serves the prebuilt Vue app embedded in the binary).

---

## 6. Run a client

Pick the language you're working in. The full per-language setup (including `venv` for Python) is in [06 — Client SDKs](06-clients.md). The shortest possible path:

**JavaScript** (from the repo root):

```bash
nvm use 22
cd examples
npm install --prefix ../clients/client-js
node 01-basic-usage.js
```

**Python**:

```bash
cd clients/client-py
python -m venv venv && source venv/bin/activate
pip install -e ".[dev]"
python example.py
```

**Go**:

```bash
cd clients/client-go
go run ./tests/...
```

---

## 7. Where to go next

- Want to understand what runs where? → [02 — Architecture](02-architecture.md)
- Build is failing? → [03 — Building the server](03-build-server.md)
- About to change the schema? → [05 — Database schema](05-database-schema.md) (read the warning first)
- Adding a partition feature? → [08 — Partitions](08-partitions.md)
- Working on retention? → [09 — Retention](09-retention.md)

