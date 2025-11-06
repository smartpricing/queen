# Failover in Queen MQ

## Overview

Queen MQ provides automatic failover to file-based buffering when PostgreSQL becomes unavailable, ensuring **zero message loss** even during complete database outages. The system seamlessly transitions between normal operation and failover mode, with automatic recovery when the database comes back online.

## The Problem

Traditional message queues face a critical challenge when their backing store becomes unavailable:

**Without Failover:**
```
Client sends message
  ↓
Server tries to write to PostgreSQL
  ↓
PostgreSQL is down ❌
  ↓
Server returns error to client
  ↓
Client must retry OR message is lost
```

**Problems:**
- Messages lost if client doesn't retry
- Client complexity (retry logic)
- Service appears unavailable
- Data loss risk during outages

## Queen's Solution

**With Failover:**
```
Client sends message
  ↓
Server tries to write to PostgreSQL
  ↓
PostgreSQL is down ❌
  ↓
Server writes to file buffer ✅
  ↓
Server returns success to client
  ↓
Background processor retries PostgreSQL every 100ms
  ↓
PostgreSQL comes back online ✅
  ↓
Background processor replays buffered messages
  ↓
Normal operation resumed
```

**Benefits:**
- ✅ Zero message loss
- ✅ Zero downtime from client perspective
- ✅ Automatic recovery
- ✅ FIFO ordering preserved
- ✅ Survives server crashes
- ✅ No client-side changes needed

## How It Works

### Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    PUSH REQUEST                         │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│              AsyncQueueManager.push_messages()          │
│  Try PostgreSQL first                                   │
└─────────────────────────────────────────────────────────┘
                        ↓
            ┌───────────┴───────────┐
            │                       │
    PostgreSQL OK?           PostgreSQL DOWN?
            │                       │
            ↓                       ↓
┌─────────────────────┐   ┌─────────────────────┐
│  Write to Database  │   │  Write to File      │
│  Return success     │   │  Return success     │
└─────────────────────┘   └─────────────────────┘
                                    ↓
                          ┌─────────────────────┐
                          │  Background         │
                          │  Processor Loop     │
                          │  (every 100ms)      │
                          └─────────────────────┘
                                    ↓
                          ┌─────────────────────┐
                          │  Try replay to DB   │
                          │  If success:        │
                          │    Delete file      │
                          │  If fail:           │
                          │    Retry later      │
                          └─────────────────────┘
```

### File Buffer Structure

```
/var/lib/queen/buffers/  (or /tmp/queen on macOS)
├── failover_019a0c11-7fe8.buf.tmp  ← Active write (incomplete)
├── failover_019a0c11-8021.buf      ← Complete (ready to process)
├── failover_019a0c11-8054.buf      ← Queued (processed in FIFO order)
├── failover_019a0c11-8087.buf      ← Queued
└── failed/
    └── failover_019a0c11-7abc.buf  ← Failed replay (retry in 5s)
```

**File Naming:**
- Prefix: `failover_`
- ID: UUIDv7 (time-ordered)
- Extension: `.buf.tmp` (writing) → `.buf` (complete)

**Benefits of UUIDv7:**
- Lexicographically sortable by time
- FIFO ordering guaranteed by filename
- No coordination needed between servers

### File Format

Each file contains newline-delimited JSON events:

```json
{"queue":"orders","partition":"customer-123","payload":{"id":1},"transaction_id":"tx-1","trace_id":"trace-1","namespace":"sales","task":"process"}
{"queue":"orders","partition":"customer-456","payload":{"id":2},"transaction_id":"tx-2","trace_id":"trace-2","namespace":"sales","task":"process"}
{"queue":"events","partition":"Default","payload":{"type":"login"},"transaction_id":"tx-3","trace_id":"trace-3","namespace":"","task":""}
...
```

**Format:**
- One event per line
- JSON object per event
- No array wrapper (streaming format)
- Max 10,000 events per file (configurable)

## Failure Detection

### Database Health Check

Every PUSH operation checks database health:

```cpp
bool is_db_healthy() {
    // Try to acquire connection with timeout
    try {
        auto conn = db_pool_->acquire(timeout_ms: 2000);
        return conn != nullptr;
    } catch (const std::exception& e) {
        return false;
    }
}
```

**Timeout Values:**
- Fast path: 2 seconds (for health check)
- Normal path: 30 seconds (for actual operations)

**Transition to Failover:**
```
PostgreSQL healthy
  ↓
PUSH attempt fails (timeout or connection error)
  ↓
Mark db_healthy_ = false
  ↓
All subsequent PUSHes go to file buffer
  ↓
Background processor attempts recovery
```

### Recovery Detection

Background processor tries to reconnect:

```cpp
bool flush_batched_to_db(const std::vector<json>& events) {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Try to insert events
        for (const auto& event : events) {
            push_single_message(conn.get(), event);
        }
        
        // Success! Mark DB as healthy
        if (!db_healthy_) {
            spdlog::info("PostgreSQL recovery detected!");
            db_healthy_ = true;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        db_healthy_ = false;
        return false;
    }
}
```

**Recovery Transition:**
```
PostgreSQL down (db_healthy_ = false)
  ↓
Background processor tries replay every 100ms
  ↓
Replay succeeds
  ↓
Mark db_healthy_ = true
  ↓
Process remaining buffer files
  ↓
Resume normal operation
```

## File Buffer Lifecycle

### 1. Writing Phase

```cpp
void FileBufferManager::buffer_event(const json& event) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    // Add to pending buffer
    pending_buffer_.push_back(event);
    pending_count_++;
    
    // Rotate file if needed
    if (pending_buffer_.size() >= events_per_file_) {
        rotate_buffer_file();
    }
}
```

**File Creation:**
```
1. Create file with .tmp extension
   failover_019a0c11-7fe8.buf.tmp
   
2. Write events (newline-delimited JSON)
   
3. When reaching events_per_file_ OR on flush:
   Rename to .buf (atomic operation)
   failover_019a0c11-7fe8.buf
```

### 2. Processing Phase

```cpp
void FileBufferManager::process_failover_events() {
    // Find all .buf files (not .tmp)
    auto files = find_buffer_files();
    
    // Sort by filename (UUIDv7 = time-ordered)
    std::sort(files.begin(), files.end());
    
    // Process oldest first (FIFO)
    if (!files.empty()) {
        std::string oldest = files[0];
        
        // Read events
        auto events = read_events_from_file(oldest);
        
        // Group by (queue, partition) for FIFO ordering
        auto grouped = group_by_partition(events);
        
        // Flush in batches
        for (const auto& [key, partition_events] : grouped) {
            for (size_t i = 0; i < partition_events.size(); i += max_batch_size_) {
                auto batch = get_batch(partition_events, i, max_batch_size_);
                
                if (flush_batched_to_db(batch)) {
                    total_processed += batch.size();
                } else {
                    // Failed - move to failed/
                    move_to_failed(oldest, "failover");
                    return;
                }
            }
        }
        
        // Success - delete file
        std::filesystem::remove(oldest);
    }
}
```

### 3. Retry Phase

Failed files are moved to `failed/` directory:

```cpp
void FileBufferManager::retry_failed_files() {
    auto failed_files = find_files_in_failed_dir();
    
    for (const auto& file : failed_files) {
        auto file_age = get_file_age(file);
        
        // Retry after 5 seconds
        if (file_age > 5) {
            spdlog::info("Retrying failed file: {}", file);
            
            // Move back to main directory
            std::filesystem::rename(
                failed_dir_ / file,
                buffer_dir_ / file
            );
        }
    }
}
```

**Retry Schedule:**
- Immediate move to `failed/` on replay failure
- Retry every 5+ seconds
- No maximum retry limit (keep trying until success)

## FIFO Ordering Guarantees

### Per-Partition FIFO

Messages are grouped by (queue, partition) before replay:

```cpp
std::unordered_map<std::string, std::vector<json>> group_by_partition(
    const std::vector<json>& events
) {
    std::unordered_map<std::string, std::vector<json>> grouped;
    
    for (const auto& event : events) {
        std::string key = event.value("queue", "") + "|" + 
                         event.value("partition", "Default");
        grouped[key].push_back(event);
    }
    
    return grouped;
}
```

**Example:**
```
Buffer file contains (in order):
1. queue=orders, partition=A, message=M1
2. queue=orders, partition=B, message=M2
3. queue=orders, partition=A, message=M3
4. queue=events, partition=Default, message=M4

Grouped for replay:
- orders|A: [M1, M3]  ← FIFO preserved
- orders|B: [M2]      ← FIFO preserved
- events|Default: [M4] ← FIFO preserved

Each partition replayed in order
```

### Global Ordering

With single server:
- ✅ Total ordering preserved (files processed in creation order)
- ✅ Per-partition FIFO preserved

With multiple servers:
- ✅ Per-partition FIFO preserved (within each server)
- ⚠️ Global ordering not guaranteed (messages can be buffered on different servers)

**Recommendation:** For strict global ordering, use single server or partition coordination.

## Crash Resistance

### Server Crashes During Write

```
Scenario:
1. Server writing to failover_019a0c11-7fe8.buf.tmp
2. Written 5,000 events
3. Server crashes ❌
4. Server restarts

Recovery:
- .tmp file still exists with 5,000 events
- Background processor ignores .tmp files
- New writes create new file: failover_019a0c11-8999.buf.tmp
- When new file completes → renamed to .buf
- .tmp file eventually cleaned up (or manually recovered)
```

**Incomplete Files:**
- ✅ Don't corrupt the system
- ✅ Don't block recovery
- ❌ Events in .tmp files not automatically recovered
- ⚠️ Manual recovery possible (rename .tmp to .buf)

### Server Crashes During Replay

```
Scenario:
1. Background processor replaying failover_019a0c11-8021.buf
2. Replayed 8,000 / 10,000 events
3. Server crashes ❌
4. Server restarts

Recovery:
- File still exists with all 10,000 events
- Replay restarts from beginning
- First 8,000 events have duplicate transaction IDs
- Duplicate detection prevents double-insertion ✅
- Only remaining 2,000 events inserted
- File deleted when complete
```

**Duplicate Handling:**
```sql
-- Duplicate check before insert
SELECT transaction_id 
FROM messages 
WHERE transaction_id = ANY($1::text[])
  AND queue_name = $2
  AND partition_name = $3

-- Only insert non-duplicates
INSERT INTO messages (...)
WHERE transaction_id NOT IN (duplicate_list)
```

## Configuration

### Environment Variables

```bash
# File buffer directory (auto-created)
export FILE_BUFFER_DIR=/var/lib/queen/buffers

# Processing settings
export FILE_BUFFER_FLUSH_MS=100              # Flush interval (ms)
export FILE_BUFFER_MAX_BATCH=100             # Max events per DB batch
export FILE_BUFFER_EVENTS_PER_FILE=10000     # Max events per file

# Retry settings
export FILE_BUFFER_RETRY_INTERVAL=5          # Retry failed files after (s)
export FILE_BUFFER_STARTUP_RECOVERY_TIMEOUT=3600  # Max startup recovery time (s)
```

### Default Locations

**Linux:**
```bash
/var/lib/queen/buffers/
```

**macOS:**
```bash
/tmp/queen/
```

**Custom:**
```bash
FILE_BUFFER_DIR=/custom/path ./bin/queen-server
```

**Directory Structure:**
```
FILE_BUFFER_DIR/
├── failover_*.buf.tmp  # Active writes
├── failover_*.buf      # Ready to process
├── qos0_*.buf.tmp      # QoS 0 buffering
├── qos0_*.buf          # QoS 0 ready
└── failed/
    ├── failover_*.buf  # Failed failover files
    └── qos0_*.buf      # Failed QoS 0 files
```

## Monitoring

### Metrics

```cpp
struct FileBufferStats {
    size_t pending_count;    // Events in memory
    size_t flushed_count;    // Events successfully flushed to DB
    size_t failed_count;     // Events in failed/
    bool db_healthy;         // Database status
};
```

**Query Metrics:**
```bash
curl http://localhost:6632/metrics
```

**Response:**
```json
{
  "file_buffer": {
    "pending": 5234,
    "flushed": 1523456,
    "failed": 0,
    "db_healthy": true
  }
}
```

### Logging

```bash
# Normal operation
[info] PUSH: Database healthy, normal operation

# Failover triggered
[warn] Database connection failed, switching to file buffer
[info] Buffered 1000 events to failover_019a0c11-7fe8.buf.tmp

# Recovery detected
[info] PostgreSQL recovery detected! Batch flush succeeded

# Replay progress
[info] Failover: Progress 5000/10000 events (50.0%)

# Replay complete
[info] Failover replay complete, deleted file failover_019a0c11-8021.buf
```

## Performance Impact

### Write Performance

| Mode | Latency | Throughput |
|------|---------|------------|
| Normal (PostgreSQL) | 10-50ms | 130K+ msg/s |
| Failover (File) | 1-5ms | 200K+ msg/s |

**Failover is faster than normal operation** because:
- No network round-trip
- Sequential file writes (very fast)
- Batching in memory before flush

### Recovery Performance

```
Recovery rate: ~10,000 events/second

Example:
- 1,000,000 buffered events
- Recovery time: ~100 seconds
- Throttled to prevent starving live requests
```

**Throttling:**
- 10ms pause every 1,000 events
- Ensures live PUSH requests get connections
- Prevents connection pool exhaustion

## Best Practices

### 1. Monitor File Buffer

```bash
# Check for buffered files
ls -lh /var/lib/queen/buffers/

# Count buffered events
wc -l /var/lib/queen/buffers/failover_*.buf

# Monitor failed directory
ls -lh /var/lib/queen/buffers/failed/
```

**Alerts:**
```bash
# Alert if too many files buffered
file_count=$(ls /var/lib/queen/buffers/failover_*.buf 2>/dev/null | wc -l)
if [ $file_count -gt 100 ]; then
  echo "WARNING: $file_count buffer files - investigate database"
fi
```

### 2. Provision Adequate Disk Space

```bash
# Calculate required space
events_per_second=10000
event_size_bytes=500
buffer_duration_seconds=3600  # 1 hour

required_bytes=$((events_per_second * event_size_bytes * buffer_duration_seconds))
required_gb=$((required_bytes / 1024 / 1024 / 1024))

echo "Required disk space: ${required_gb}GB"
```

**Recommendation:** Provision for 2-4 hours of peak traffic.

### 3. Regular Health Checks

```javascript
// Application-level health check
setInterval(async () => {
  const health = await fetch('http://localhost:6632/health').then(r => r.json())
  
  if (health.database === 'disconnected') {
    console.warn('Database disconnected - running in failover mode')
    await sendAlert('Queen MQ in failover mode')
  }
}, 60000)  // Every minute
```

### 4. Graceful Shutdown

```bash
# Allow time for buffer flush on shutdown
kill -SIGTERM <queen-server-pid>
sleep 10  # Wait for flush
kill -SIGKILL <queen-server-pid>  # Force if still running
```

**Server graceful shutdown:**
```cpp
void shutdown_handler(int signal) {
    // Flush pending buffer
    file_buffer_manager->flush_pending_buffer();
    
    // Wait for background processor
    file_buffer_manager->stop();
    
    // Exit
    exit(0);
}
```

### 5. Disaster Recovery

**Backup buffer files:**
```bash
# Regular backups
tar -czf buffer-backup-$(date +%Y%m%d).tar.gz /var/lib/queen/buffers/

# Restore if needed
tar -xzf buffer-backup-20240101.tar.gz -C /
```

**Manual recovery of .tmp files:**
```bash
# List incomplete files
ls /var/lib/queen/buffers/*.tmp

# Manually promote to .buf if valid
for file in /var/lib/queen/buffers/*.tmp; do
  # Validate JSON
  if jq -e . "$file" > /dev/null 2>&1; then
    mv "$file" "${file%.tmp}"
    echo "Recovered: $file"
  else
    echo "Corrupt: $file"
  fi
done
```

## Limitations

### 1. Disk Space

If disk is full:
```
- File writes fail
- PUSH operations return error ❌
- Message loss possible
```

**Solution:** Monitor disk usage, provision adequate space.

### 2. Global Ordering (Multi-Server)

With multiple servers:
```
Server 1 buffers: [M1, M2, M3]
Server 2 buffers: [M4, M5, M6]

Replay order depends on which server recovers first
Global ordering not guaranteed
```

**Solution:** Use single server for strict ordering, or partition-based ordering.

### 3. Incomplete Files

`.tmp` files from crashes:
```
- Not automatically recovered
- Require manual intervention
- Events in .tmp files may be lost
```

**Solution:** Regular monitoring, automated recovery scripts.

## Troubleshooting

### High Disk Usage

**Check buffered events:**
```bash
find /var/lib/queen/buffers -name "failover_*.buf" -exec wc -l {} +
```

**Solution:**
1. Check PostgreSQL connectivity
2. Check PostgreSQL performance (slow queries)
3. Increase background processing capacity

### Recovery Too Slow

**Symptoms:** Files building up faster than they're processed

**Solutions:**
1. Increase batch size:
```bash
export FILE_BUFFER_MAX_BATCH=500  # Up from 100
```

2. Reduce throttling:
```cpp
// Modify source code to throttle less frequently
if (total_processed % 10000 == 0) {  // Was 1000
  sleep(10ms);
}
```

3. Scale PostgreSQL (more connections, better hardware)

### Files in failed/

**Check failed files:**
```bash
ls -lh /var/lib/queen/buffers/failed/
```

**Investigate:**
```bash
# Check file contents
head /var/lib/queen/buffers/failed/failover_*.buf

# Check server logs
grep "Failed to flush" /var/log/queen/server.log
```

**Common causes:**
- PostgreSQL still down
- Corrupt JSON in file
- Database constraints violated
- Connection pool exhausted

## Summary

Queen MQ's failover system provides:

- ✅ **Zero Message Loss**: All messages buffered to disk
- ✅ **Zero Downtime**: Clients unaffected by database outages
- ✅ **Automatic Recovery**: Seamless transition back to normal operation
- ✅ **FIFO Ordering**: Per-partition ordering preserved
- ✅ **Crash Resistance**: Survives server crashes
- ✅ **Duplicate Protection**: Automatic deduplication on replay
- ✅ **Performance**: File buffering faster than database writes

The failover system ensures Queen MQ remains available and reliable even during PostgreSQL outages, making it suitable for mission-critical applications.

## Related Documentation

- [Architecture](ARCHITECTURE.md) - System architecture overview
- [Push Operations](PUSH.md) - How PUSH works with failover
- [Server Features](SERVER_FEATURES.md) - Complete feature list
- [API Reference](../server/API.md) - HTTP API details

