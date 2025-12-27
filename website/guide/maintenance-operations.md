# Maintenance Operations

Emergency procedures for Push/Pop maintenance modes and consumer group operations.

::: warning When Things Go Wrong
This guide is for emergency situations. Bookmark it.
:::

## Push Maintenance Mode

**What it does:** Routes all PUSH operations to file buffer instead of PostgreSQL.

### When to Use

- PostgreSQL maintenance/upgrade
- Database migration (schema changes)
- Major version upgrade
- Emergency database operations

### How to Enable

1. **Webapp:** Sidebar → Click **Push Maint.** toggle
2. **API:** `POST /api/v1/system/maintenance` with `{ "enabled": true }`


### Behavior When Active

- All PUSH operations return `"status": "buffered"`
- Messages written to file buffer on disk
- Consumers continue draining existing queue
- Other servers notified via UDP broadcast

### Migration Procedure

```
1. Enable Push Maintenance
   ↓
2. Wait for consumers to drain remaining messages
   (monitor lag → 0 on all consumer groups)
   ↓
3. Perform PostgreSQL maintenance/upgrade
   ↓
4. Verify database is healthy
   ↓
5. Disable Push Maintenance
   ↓
6. Buffered messages drain automatically
```

### Buffer File Locations

| Platform | Default Path |
|----------|--------------|
| Linux | `/var/lib/queen/buffers/` |
| macOS | `/tmp/queen/` |
| Custom | `FILE_BUFFER_DIR` env var |

```bash
# Check buffer files on server
ls -lh /var/lib/queen/buffers/

# File types:
# *.buf.tmp  - Being written (active)
# *.buf      - Complete, ready to drain
# failed/*   - Failed, will retry
```

::: danger Buffer Files Cannot Be Deleted from Webapp
Buffered files must be managed directly on the server filesystem. On clustered deployments, check **all replica servers**.
:::

---

## Pop Maintenance Mode

**What it does:** All POP operations return empty arrays.

### When to Use

- Queue flooded with unwanted messages
- Consumer bugs causing infinite loops
- Need time to fix data without processing
- Emergency pause of all consumption

### How to Enable

1. **Webapp:** Sidebar → Click **Pop Maint.** toggle
2. **API:** `POST /api/v1/system/maintenance/pop` with `{ "enabled": true }`

### Behavior When Active

- All consumers receive empty arrays `[]`
- Messages remain in database untouched
- Consumers keep polling but get nothing
- Time to perform seek/cleanup operations

### Recovery Procedure

```
1. Enable Pop Maintenance
   (consumers now receive empty arrays)
   ↓
2. Identify problematic consumer groups
   ↓
3. Use Seek or Move to Now (see below)
   ↓
4. Fix underlying issue
   ↓
5. Disable Pop Maintenance
   ↓
6. Consumption resumes normally
```

---

## Consumer Group Operations

Manage consumer group cursors from the webapp.


### Move to Now

**Skips all pending messages** - cursor jumps to the latest message.

1. Go to **Consumer Groups**
2. Select the group
3. Click **Move to Now** button on the queue
4. Confirm action

::: tip When to Use
Use when you want to abandon all pending messages and start fresh with new messages only.
:::

### Seek to Timestamp

**Moves cursor to a specific point in time** - can go backwards or forwards.

1. Go to **Consumer Groups**
2. Select the group
3. Click **Seek** button on the queue
4. Select target timestamp
5. Confirm action

::: warning Seek Behavior
Cursor moves to the last message **at or before** the timestamp. Messages after that point will be (re-)consumed.
:::

### Delete Consumer Group

**Removes consumer group state** for a specific queue.

1. Go to **Consumer Groups**
2. Select the group
3. Toggle **Delete metadata** option:
   - **ON:** Removes partition state AND subscription metadata
   - **OFF:** Removes partition state, preserves metadata for future use
4. Click **Delete** button
5. Confirm action

::: danger Irreversible
Deletion cannot be undone. The consumer group will start fresh if recreated.
:::

---

## Emergency Playbook

### Scenario: Queue Flooded with Bad Messages

```
1. Enable Pop Maintenance
2. Navigate to Consumer Groups
3. Select affected group
4. Click "Move to Now" to skip bad messages
5. Disable Pop Maintenance
```

### Scenario: PostgreSQL Emergency Upgrade

```
1. Enable Push Maintenance
2. Monitor consumer group lag → wait until 0
3. Stop Queen servers
4. Upgrade PostgreSQL
5. Start Queen servers
6. Disable Push Maintenance
7. Monitor buffer drain
```

### Scenario: Consumer Processing Wrong Data

```
1. Enable Pop Maintenance (stops consumption)
2. Fix consumer code
3. Use "Seek" to go back to correct timestamp
4. Deploy fixed consumer
5. Disable Pop Maintenance
```

---

## API Reference

### Push Maintenance

```bash
# Get status
curl http://localhost:6632/api/v1/system/maintenance

# Enable
curl -X POST http://localhost:6632/api/v1/system/maintenance \
  -H "Content-Type: application/json" \
  -d '{"enabled": true}'

# Disable
curl -X POST http://localhost:6632/api/v1/system/maintenance \
  -H "Content-Type: application/json" \
  -d '{"enabled": false}'
```

### Pop Maintenance

```bash
# Get status
curl http://localhost:6632/api/v1/system/maintenance/pop

# Enable
curl -X POST http://localhost:6632/api/v1/system/maintenance/pop \
  -H "Content-Type: application/json" \
  -d '{"enabled": true}'

# Disable
curl -X POST http://localhost:6632/api/v1/system/maintenance/pop \
  -H "Content-Type: application/json" \
  -d '{"enabled": false}'
```

### Consumer Group Seek

```bash
# Move to now (skip all pending)
curl -X POST http://localhost:6632/api/v1/consumer-groups/{group}/queues/{queue}/seek \
  -H "Content-Type: application/json" \
  -d '{"toEnd": true}'

# Seek to timestamp
curl -X POST http://localhost:6632/api/v1/consumer-groups/{group}/queues/{queue}/seek \
  -H "Content-Type: application/json" \
  -d '{"timestamp": "2025-01-15T10:00:00.000Z"}'
```

### Delete Consumer Group

```bash
# Delete with metadata
curl -X DELETE "http://localhost:6632/api/v1/consumer-groups/{group}/queues/{queue}?deleteMetadata=true"

# Delete preserving metadata
curl -X DELETE "http://localhost:6632/api/v1/consumer-groups/{group}/queues/{queue}?deleteMetadata=false"
```

## Related

- [Failover & Recovery](/guide/failover)
- [Consumer Groups](/guide/consumer-groups)
- [Webapp Features](/webapp/features)

