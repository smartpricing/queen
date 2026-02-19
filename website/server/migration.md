# Database Migration

Queen MQ has a built-in database migration tool that lets you move the entire queen schema from one PostgreSQL instance to another — directly from the web dashboard, without any external tooling.

::: info Available since v0.12.12
The migration feature is embedded in the Queen server itself. No separate scripts or sidecars are required.
:::

## How it works

The migration uses `pg_dump` piped directly into `pg_restore` (`pg_dump | pg_restore`). This means:

- **No temp file is written** — data streams through a kernel pipe buffer in memory. There is no disk space requirement on the Queen pod regardless of database size.
- **Overlapped execution** — pg_dump reads from the source and pg_restore writes to the target simultaneously, which is faster than dump-then-restore.
- **No compression overhead** — since data never touches disk, compression is disabled for maximum throughput.
- **Table structures are always created** — all `CREATE TABLE`, functions, and procedures are always migrated. You can optionally skip the row data for specific table groups.
- **Safe to retry** — the target schema is always dropped and recreated before the stream starts, so any failed or partial migration can be reset and retried cleanly.

::: warning PostgreSQL client version
The Queen server container must have a `pg_dump`/`pg_restore` client version **equal to or greater than** the source PostgreSQL server version. From v0.12.12, the Docker image includes PostgreSQL 18 client tools via the official PGDG apt repository.
:::

---

## Migration procedure

### Step 1 — Enable Maintenance Mode (recommended)

For a consistent snapshot, enable both maintenance modes before migrating:

- **Push maintenance** — new messages are buffered to disk instead of PostgreSQL
- **Pop maintenance** — consumers receive empty arrays and stop processing

This ensures no data is written to the source database during the migration window.

::: tip Live migration
You can also migrate without maintenance mode. The source server continues operating normally — `pg_dump` is read-only and never blocks writes. The trade-off is a small data gap: messages pushed after the dump snapshot was taken will not appear on the new database.
:::

### Step 2 — Configure the target database

Enter the connection details for the destination PostgreSQL instance and click **Test Connection** to verify connectivity. The test checks credentials and returns the server version.

### Step 3 — Select data to migrate

All table structures (DDL) are always migrated. You can optionally skip the row data for large table groups to speed up the migration:

| Group | Tables | Skip when... |
|-------|--------|--------------|
| **Messages** | `messages`, `dead_letter_queue` | You want to start with an empty queue on the new DB |
| **Traces** | `message_traces`, `message_trace_names` | Trace/debug data is not needed on the new DB |
| **Consumption history** | `messages_consumed` | Audit log is not required; consumers work without it |
| **Metrics & stats** | `stats`, `stats_history`, `system_metrics`, `worker_metrics`, `worker_metrics_summary`, `queue_lag_metrics`, `retention_history` | Metrics can start accumulating fresh on the new DB |

The following **core tables are always fully migrated** (data + structure):

```
queues, partitions, partition_consumers, partition_lookup,
consumer_groups_metadata, system_state, dead_letter_queue
```

::: info system_state and maintenance mode
`system_state` stores the persistent maintenance mode flags. If you migrate with maintenance mode ON, the new server will also start with maintenance mode ON — it will not process traffic until you explicitly disable it. This is intentional: it gives you a safe window to validate the migration before going live.
:::

### Step 4 — Start the migration

Click **Start Migration**. The migration runs in the background; the dashboard polls status every 2 seconds and shows elapsed time and current step.

You can safely close the browser tab — the migration continues server-side and you can reopen the dashboard to check progress.

**If the pod is restarted mid-migration:** the process is killed cleanly (no data corruption on either database), the partial target schema is discarded on the next run, and you can start over via Reset.

### Step 5 — Validate

After the migration completes, click **Validate Row Counts**. This connects to both source and target databases and compares row counts for all tables.

::: warning Live migration validation
If you migrated without maintenance mode, the source database keeps changing and the row counts will differ — that is expected, not an error. The important thing is that all tables exist on the target and the counts are in the right ballpark.
:::

### Step 6 — Switch deployment

Once validated, redeploy Queen pointing to the new database:

1. Update the Kubernetes secret with new database credentials
2. Update the Helm values file (`db` field) to point to the new database name/instance
3. Run `./upgrade.sh <env> --no-build` to redeploy
4. Disable maintenance modes on the new deployment

---

## API Reference

All migration endpoints require admin authentication.

### Test connection

```bash
POST /api/v1/migration/test-connection
```

```json
{
  "host": "new-db.example.com",
  "port": "5432",
  "database": "mydb",
  "user": "myuser",
  "password": "mypassword",
  "ssl": true
}
```

Response:
```json
{
  "success": true,
  "message": "Connection successful",
  "version": "PostgreSQL 18.0 ..."
}
```

### Start migration

```bash
POST /api/v1/migration/start
```

```json
{
  "host": "new-db.example.com",
  "port": "5432",
  "database": "mydb",
  "user": "myuser",
  "password": "mypassword",
  "ssl": true,
  "schema": "queen",
  "excludeTableData": ["message_traces", "message_trace_names", "messages_consumed"]
}
```

`excludeTableData` is optional. Omit it to migrate all data. Accepted values:
`messages`, `dead_letter_queue`, `message_traces`, `message_trace_names`, `messages_consumed`, `stats`, `stats_history`, `system_metrics`, `worker_metrics`, `worker_metrics_summary`, `queue_lag_metrics`, `retention_history`.

Response (202 Accepted):
```json
{
  "status": "started",
  "message": "Migration started. Poll /api/v1/migration/status for progress."
}
```

### Poll status

```bash
GET /api/v1/migration/status
```

Response:
```json
{
  "status": "restoring",
  "currentStep": "Streaming: pg_dump | pg_restore (no temp file)...",
  "elapsedSeconds": 142.3
}
```

`status` values: `idle` | `dumping` | `restoring` | `complete` | `error`

### Validate row counts

```bash
POST /api/v1/migration/validate
```

Same body as test-connection. Returns per-table source vs target counts.

```json
{
  "success": true,
  "allMatch": true,
  "tables": [
    { "table": "queues", "sourceCount": 93, "targetCount": 93, "match": true },
    { "table": "messages", "sourceCount": 2847291, "targetCount": 2847291, "match": true }
  ]
}
```

### Reset

```bash
POST /api/v1/migration/reset
```

Resets migration state back to `idle`. Cannot be called while a migration is in progress.

---

## Performance expectations

Migration time depends on database size, network bandwidth between the Queen pod and both PostgreSQL instances, and how many table groups are included.

| Data included | Approximate time |
|---------------|-----------------|
| Full migration | 45 – 90 min per 40 GB |
| Skip traces + history + metrics | ~50% faster |
| Schema + core operational tables only | 3 – 5 min |

::: tip Largest tables
The `messages` table typically dominates total size. Skipping it (starting with an empty queue) is the fastest migration path when re-consumption of historical messages is not required.
:::

## Related

- [Maintenance Operations](/guide/maintenance-operations)
- [Failover & Recovery](/guide/failover)
- [Deployment](/server/deployment)
