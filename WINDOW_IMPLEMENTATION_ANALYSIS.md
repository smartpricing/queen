# Window Implementation Analysis & Queries

**Date**: 2025-11-02  
**Attempt**: 3rd  
**Status**: TESTING QUERIES BEFORE IMPLEMENTATION

## Problems Identified

### 1. Missing Implementation
- ❌ No `window()` method in `Stream.js`
- ❌ No `stream_windows` database table
- ❌ No window processing logic in `stream_manager.cpp`
- ❌ No window operation builder

### 2. Design Issues from STREAMING_V2.md
- ❌ Plan requires `groupBy` + `aggregate` for windows (too complex)
- ❌ Plan uses processing time instead of ingestion time
- ❌ Plan has complex WindowIntentionRegistry (over-engineered)

## Correct Design Principles

### 1. **Ingestion Time Windows**
- Use `m.created_at` (when message was queued) NOT processing time
- Epoch-aligned: 5-second windows start at 00, 05, 10, 15, 20, etc.
- Example: If message arrives at 15:00:07, it goes in window [15:00:05, 15:00:10)

### 2. **GroupBy & Aggregation are OPTIONAL**
- Simple case: `stream.window({ duration: 5 })` returns array of messages
- With groupBy: `stream.window({ duration: 5 }).groupBy('userId')` returns grouped messages
- With aggregate: `stream.window({ duration: 5 }).groupBy('userId').count()` returns aggregated results

### 3. **Window State Tracking**
- Each consumer group tracks which windows have been consumed
- Windows are identified by: `{queue}@{consumer}:tumbling:{start}-{end}`
- Once consumed, never returned again (exactly-once semantics)

---

## Database Schema

### Table: `queen.stream_windows`

```sql
CREATE TABLE IF NOT EXISTS queen.stream_windows (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    
    -- Window identification
    window_id VARCHAR NOT NULL,  -- "events@processor:tumbling:2025-11-02T15:00:00-2025-11-02T15:00:05"
    consumer_group VARCHAR NOT NULL,
    queue_name VARCHAR NOT NULL,
    partition_name VARCHAR,
    
    -- Window configuration
    window_type VARCHAR NOT NULL,       -- 'tumbling', 'sliding', 'session'
    window_duration_seconds INT,        -- For time-based windows
    window_count INT,                   -- For count-based windows
    grace_period_seconds INT DEFAULT 0,
    
    -- Window boundaries (time-based)
    window_start TIMESTAMPTZ,
    window_end TIMESTAMPTZ,
    
    -- Window boundaries (count-based)
    window_start_offset BIGINT,
    window_end_offset BIGINT,
    
    -- Status tracking
    status VARCHAR DEFAULT 'open',      -- 'open', 'ready', 'consumed'
    message_count INT DEFAULT 0,
    
    -- Timestamps
    created_at TIMESTAMPTZ DEFAULT NOW(),
    closed_at TIMESTAMPTZ,              -- When window became ready
    consumed_at TIMESTAMPTZ,            -- When client consumed
    
    -- Unique constraint
    UNIQUE(consumer_group, queue_name, COALESCE(partition_name, ''), window_id)
);

-- Indexes
CREATE INDEX idx_stream_windows_consumer ON queen.stream_windows(consumer_group, queue_name, status);
CREATE INDEX idx_stream_windows_ready ON queen.stream_windows(consumer_group, queue_name, window_end)
    WHERE status = 'ready' AND consumed_at IS NULL;
CREATE INDEX idx_stream_windows_time ON queen.stream_windows(window_start, window_end);
```

---

## Query Design

### Query 1: Calculate Epoch-Aligned Window Boundaries

**Purpose**: Get current and next window boundaries based on ingestion time

```sql
-- For 5-second tumbling window
WITH current_time AS (
    SELECT NOW() as now
),
window_params AS (
    SELECT 
        5 as duration_seconds,
        1 as grace_seconds
),
window_bounds AS (
    SELECT 
        -- Current epoch timestamp
        EXTRACT(EPOCH FROM ct.now)::BIGINT as current_epoch,
        
        -- Last completed window start (floor to window duration)
        TO_TIMESTAMP(
            (EXTRACT(EPOCH FROM ct.now)::BIGINT / wp.duration_seconds) * wp.duration_seconds
        ) as current_window_start,
        
        -- Last completed window end
        TO_TIMESTAMP(
            ((EXTRACT(EPOCH FROM ct.now)::BIGINT / wp.duration_seconds) * wp.duration_seconds) + wp.duration_seconds
        ) as current_window_end,
        
        -- Check if grace period has passed
        (ct.now >= TO_TIMESTAMP(
            ((EXTRACT(EPOCH FROM ct.now)::BIGINT / wp.duration_seconds) * wp.duration_seconds) + wp.duration_seconds + wp.grace_seconds
        )) as is_ready
    FROM current_time ct, window_params wp
)
SELECT 
    current_window_start::TEXT as window_start,
    current_window_end::TEXT as window_end,
    is_ready,
    current_epoch
FROM window_bounds;
```

**Test this query**:
```bash
PGPASSWORD=postgres psql -U postgres -h 0.0.0.0 -c "
WITH current_time AS (
    SELECT NOW() as now
),
window_params AS (
    SELECT 
        5 as duration_seconds,
        1 as grace_seconds
),
window_bounds AS (
    SELECT 
        EXTRACT(EPOCH FROM ct.now)::BIGINT as current_epoch,
        TO_TIMESTAMP(
            (EXTRACT(EPOCH FROM ct.now)::BIGINT / wp.duration_seconds) * wp.duration_seconds
        ) as current_window_start,
        TO_TIMESTAMP(
            ((EXTRACT(EPOCH FROM ct.now)::BIGINT / wp.duration_seconds) * wp.duration_seconds) + wp.duration_seconds
        ) as current_window_end,
        (ct.now >= TO_TIMESTAMP(
            ((EXTRACT(EPOCH FROM ct.now)::BIGINT / wp.duration_seconds) * wp.duration_seconds) + wp.duration_seconds + wp.grace_seconds
        )) as is_ready
    FROM current_time ct, window_params wp
)
SELECT 
    current_window_start::TEXT as window_start,
    current_window_end::TEXT as window_end,
    is_ready,
    current_epoch
FROM window_bounds;
"
```

### Query 2: Get Next Window to Process

**Purpose**: Find the next unconsumed window for a consumer group

```sql
-- Get last consumed window for this consumer group
WITH last_consumed AS (
    SELECT 
        COALESCE(MAX(window_end), TO_TIMESTAMP(0)) as last_window_end
    FROM queen.stream_windows
    WHERE consumer_group = $1  -- 'window_test'
    AND queue_name = $2         -- 'smartchat-agent'
    AND status = 'consumed'
),
window_params AS (
    SELECT 
        $3::INT as duration_seconds,  -- 5
        $4::INT as grace_seconds      -- 1
),
next_window AS (
    SELECT 
        -- Next window starts where last ended (or at epoch if first)
        CASE 
            WHEN lc.last_window_end = TO_TIMESTAMP(0) THEN
                -- First window: align to epoch
                TO_TIMESTAMP(
                    (EXTRACT(EPOCH FROM NOW())::BIGINT / wp.duration_seconds) * wp.duration_seconds
                )
            ELSE
                lc.last_window_end
        END as window_start,
        
        -- Next window ends duration after start
        CASE 
            WHEN lc.last_window_end = TO_TIMESTAMP(0) THEN
                TO_TIMESTAMP(
                    ((EXTRACT(EPOCH FROM NOW())::BIGINT / wp.duration_seconds) * wp.duration_seconds) + wp.duration_seconds
                )
            ELSE
                lc.last_window_end + (wp.duration_seconds * INTERVAL '1 second')
        END as window_end,
        
        wp.duration_seconds,
        wp.grace_seconds
    FROM last_consumed lc, window_params wp
)
SELECT 
    window_start::TEXT,
    window_end::TEXT,
    -- Window is ready if: current_time >= window_end + grace
    (NOW() >= window_end + (grace_seconds * INTERVAL '1 second')) as is_ready,
    -- Window is in future
    (window_end > NOW()) as is_future
FROM next_window;
```

**Test this query**:
```bash
PGPASSWORD=postgres psql -U postgres -h 0.0.0.0 -c "
WITH last_consumed AS (
    SELECT 
        COALESCE(MAX(window_end), TO_TIMESTAMP(0)) as last_window_end
    FROM queen.stream_windows
    WHERE consumer_group = 'window_test'
    AND queue_name = 'smartchat-agent'
    AND status = 'consumed'
),
window_params AS (
    SELECT 
        5::INT as duration_seconds,
        1::INT as grace_seconds
),
next_window AS (
    SELECT 
        CASE 
            WHEN lc.last_window_end = TO_TIMESTAMP(0) THEN
                TO_TIMESTAMP(
                    (EXTRACT(EPOCH FROM NOW())::BIGINT / wp.duration_seconds) * wp.duration_seconds
                )
            ELSE
                lc.last_window_end
        END as window_start,
        
        CASE 
            WHEN lc.last_window_end = TO_TIMESTAMP(0) THEN
                TO_TIMESTAMP(
                    ((EXTRACT(EPOCH FROM NOW())::BIGINT / wp.duration_seconds) * wp.duration_seconds) + wp.duration_seconds
                )
            ELSE
                lc.last_window_end + (wp.duration_seconds * INTERVAL '1 second')
        END as window_end,
        
        wp.duration_seconds,
        wp.grace_seconds
    FROM last_consumed lc, window_params wp
)
SELECT 
    window_start::TEXT,
    window_end::TEXT,
    (NOW() >= window_end + (grace_seconds * INTERVAL '1 second')) as is_ready,
    (window_end > NOW()) as is_future
FROM next_window;
"
```

### Query 3: Get Messages in Window

**Purpose**: Retrieve all messages that fall within a window's time range

```sql
-- Get all messages in a specific window
SELECT 
    m.id,
    m.payload,
    m.created_at,
    p.name as partition_name,
    q.name as queue_name
FROM queen.messages m
JOIN queen.partitions p ON p.id = m.partition_id
JOIN queen.queues q ON q.id = p.queue_id
WHERE q.name = $1                     -- 'smartchat-agent'
AND m.created_at >= $2::TIMESTAMPTZ   -- window_start
AND m.created_at < $3::TIMESTAMPTZ    -- window_end (exclusive)
ORDER BY m.created_at, m.id;
```

**Test this query** (using sample window):
```bash
PGPASSWORD=postgres psql -U postgres -h 0.0.0.0 -c "
SELECT 
    m.id,
    m.payload,
    m.created_at,
    p.name as partition_name,
    q.name as queue_name
FROM queen.messages m
JOIN queen.partitions p ON p.id = m.partition_id
JOIN queen.queues q ON q.id = p.queue_id
WHERE q.name = 'smartchat-agent'
AND m.created_at >= NOW() - INTERVAL '10 seconds'
AND m.created_at < NOW() - INTERVAL '5 seconds'
ORDER BY m.created_at, m.id
LIMIT 10;
"
```

### Query 4: Get Messages with GroupBy (Optional)

**Purpose**: Group and aggregate messages within a window

```sql
-- With groupBy and aggregation
SELECT 
    (m.payload->>'tenantId') as tenantId,
    COUNT(*) as count,
    AVG((m.payload->>'aiScore')::numeric) as avgScore
FROM queen.messages m
JOIN queen.partitions p ON p.id = m.partition_id
JOIN queen.queues q ON q.id = p.queue_id
WHERE q.name = $1                     -- 'smartchat-agent'
AND m.created_at >= $2::TIMESTAMPTZ   -- window_start
AND m.created_at < $3::TIMESTAMPTZ    -- window_end
GROUP BY (m.payload->>'tenantId')
ORDER BY count DESC;
```

**Test this query**:
```bash
PGPASSWORD=postgres psql -U postgres -h 0.0.0.0 -c "
SELECT 
    (m.payload->>'tenantId') as tenantId,
    COUNT(*) as count,
    AVG((m.payload->>'aiScore')::numeric) as avgScore
FROM queen.messages m
JOIN queen.partitions p ON p.id = m.partition_id
JOIN queen.queues q ON q.id = p.queue_id
WHERE q.name = 'smartchat-agent'
AND m.created_at >= NOW() - INTERVAL '60 seconds'
AND m.created_at < NOW()
GROUP BY (m.payload->>'tenantId')
ORDER BY count DESC;
"
```

### Query 5: Mark Window as Consumed

**Purpose**: Record that a window has been consumed by a consumer group

```sql
INSERT INTO queen.stream_windows (
    window_id,
    consumer_group,
    queue_name,
    partition_name,
    window_type,
    window_duration_seconds,
    grace_period_seconds,
    window_start,
    window_end,
    status,
    message_count,
    consumed_at
) VALUES (
    $1,  -- window_id: 'smartchat-agent@window_test:tumbling:2025-11-02T15:00:00-2025-11-02T15:00:05'
    $2,  -- consumer_group: 'window_test'
    $3,  -- queue_name: 'smartchat-agent'
    $4,  -- partition_name: NULL
    $5,  -- window_type: 'tumbling'
    $6,  -- window_duration_seconds: 5
    $7,  -- grace_period_seconds: 1
    $8,  -- window_start: '2025-11-02 15:00:00'
    $9,  -- window_end: '2025-11-02 15:00:05'
    'consumed',
    $10, -- message_count: 42
    NOW()
)
ON CONFLICT (consumer_group, queue_name, COALESCE(partition_name, ''), window_id) 
DO UPDATE SET
    consumed_at = NOW(),
    status = 'consumed',
    message_count = EXCLUDED.message_count;
```

---

## Complete Window Processing Flow

### 1. Client Request
```javascript
for await (const window of queen
  .stream('smartchat-agent@window_test')
  .window({ type: 'tumbling', duration: 5, grace: 1 })
) {
  console.log(window)  // Array of messages in this window
}
```

### 2. Server Processing Steps

```
┌─────────────────────────────────────────────────────────────┐
│ 1. Get Last Consumed Window                                 │
│    Query: SELECT MAX(window_end) FROM stream_windows        │
│    Result: '2025-11-02 15:00:05' (or NULL if first)        │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. Calculate Next Window Boundaries                         │
│    If first: Align to epoch (floor(now / 5) * 5)           │
│    If not first: last_window_end to last_window_end + 5     │
│    Result: window_start = '15:00:05'                       │
│            window_end   = '15:00:10'                        │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. Check if Window is Ready                                 │
│    Ready if: NOW() >= window_end + grace                   │
│    Current time: 15:00:12                                  │
│    Window end + grace: 15:00:10 + 1s = 15:00:11           │
│    Result: READY ✓                                         │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. Get Messages in Window                                   │
│    Query: SELECT * FROM messages                            │
│           WHERE created_at >= '15:00:05'                   │
│           AND created_at < '15:00:10'                      │
│    Result: [msg1, msg2, msg3, ...]                         │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│ 5. Mark Window as Consumed                                  │
│    INSERT INTO stream_windows (window_id, consumed_at, ...) │
│    ON CONFLICT UPDATE consumed_at = NOW()                   │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│ 6. Return Window to Client                                  │
│    {                                                        │
│      window: [msg1, msg2, msg3, ...],                      │
│      _windowStart: '2025-11-02T15:00:05Z',                 │
│      _windowEnd: '2025-11-02T15:00:10Z',                   │
│      _messageCount: 42                                      │
│    }                                                        │
└─────────────────────────────────────────────────────────────┘
```

### 3. With GroupBy and Aggregation

```javascript
for await (const window of queen
  .stream('smartchat-agent@window_test')
  .window({ type: 'tumbling', duration: 5, grace: 1 })
  .groupBy('payload.tenantId')
  .aggregate({ count: { $count: '*' }, avgScore: { $avg: 'payload.aiScore' } })
) {
  console.log(window)
  // [
  //   { tenantId: '0', count: 5, avgScore: 2.4 },
  //   { tenantId: '1', count: 3, avgScore: 1.7 },
  //   ...
  // ]
}
```

**Query becomes**:
```sql
SELECT 
    (m.payload->>'tenantId') as tenantId,
    COUNT(*) as count,
    AVG((m.payload->>'aiScore')::numeric) as avgScore
FROM queen.messages m
JOIN queen.partitions p ON p.id = m.partition_id
JOIN queen.queues q ON q.id = p.queue_id
WHERE q.name = 'smartchat-agent'
AND m.created_at >= '2025-11-02 15:00:05'
AND m.created_at < '2025-11-02 15:00:10'
GROUP BY (m.payload->>'tenantId')
ORDER BY count DESC;
```

---

## Testing Checklist

### Test 1: Create Table
```bash
PGPASSWORD=postgres psql -U postgres -h 0.0.0.0 <<EOF
-- Run the CREATE TABLE statement above
EOF
```

### Test 2: Calculate Window Boundaries
```bash
# Run Query 1 multiple times over 10 seconds
# Verify window boundaries advance every 5 seconds
```

### Test 3: Get Next Window
```bash
# Run Query 2 
# Verify it returns next unconsumed window
```

### Test 4: Get Messages
```bash
# Run Query 3 with real window boundaries
# Verify messages are within time range
```

### Test 5: Mark as Consumed
```bash
# Run Query 5
# Verify window is recorded
# Run Query 2 again - should return NEXT window
```

### Test 6: Multiple Consumers
```bash
# Run with different consumer_group names
# Verify each group tracks windows independently
```

---

## Implementation Files

### 1. Client-Side
- [ ] Add `window()` method to `Stream.js`
- [ ] Create `WindowedStream.js` class
- [ ] Add `window()` to `OperationBuilder.js`
- [ ] Update `GroupedStream.js` to handle windows

### 2. Server-Side
- [ ] Add window operation to `stream_manager.cpp`
- [ ] Implement window boundary calculation
- [ ] Implement window state tracking
- [ ] Add `/api/v1/stream/window/next` endpoint

### 3. Database
- [ ] Create `stream_windows` table
- [ ] Add indexes

---

## Next Steps

1. **RUN THESE QUERIES** in the database to verify they work
2. Fix any issues with the queries
3. Implement client-side `window()` method
4. Implement server-side window processing
5. Test end-to-end

**CRITICAL**: Test all queries FIRST before writing any code!

