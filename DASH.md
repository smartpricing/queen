# Dashboard API Routes for Queen Message Queue

This document describes the API routes needed for the Queen dashboard web application, with complete implementation details, curl examples, and expected outputs.

---

## Database Schema Analysis

### Key Tables for Statistics:
- **`queen.messages`** - Immutable message data with `created_at` timestamp
- **`queen.messages_status`** - Message status per consumer group (pending, processing, completed, failed)
- **`queen.partition_cursors`** - Tracks consumption progress (`total_messages_consumed`, `last_consumed_at`)
- **`queen.partition_leases`** - Active leases on partitions
- **`queen.dead_letter_queue`** - Failed messages that exceeded retry limits
- **`queen.queues`** - Queue definitions with namespace/task grouping
- **`queen.partitions`** - Partition subdivisions of queues

### Critical Relationships:
```
queues (1) -> (N) partitions (1) -> (N) messages (1) -> (N) messages_status
                 |                                            |
                 +-> partition_cursors                        +-> dead_letter_queue
                 +-> partition_leases
```

---

## API Endpoints

### 1. Dashboard Status Endpoint

**Purpose:** Comprehensive system overview with throughput, queue stats, and message counts.

**Endpoint:** `GET /api/v1/status`

**Query Parameters:**
- `from` (optional): ISO 8601 timestamp, default: 1 hour ago
- `to` (optional): ISO 8601 timestamp, default: now
- `queue` (optional): Filter by specific queue name
- `namespace` (optional): Filter by namespace
- `task` (optional): Filter by task

**Implementation Query Strategy:**

```sql
-- 1. Throughput: Messages ingested per minute (from messages.created_at)
WITH time_series AS (
  SELECT generate_series(
    DATE_TRUNC('minute', $1::timestamp),
    DATE_TRUNC('minute', $2::timestamp),
    '1 minute'::interval
  ) AS minute
)
SELECT 
  ts.minute,
  COUNT(m.id) as messages_ingested,
  COUNT(CASE WHEN ms.status = 'completed' AND ms.completed_at >= ts.minute 
             AND ms.completed_at < ts.minute + INTERVAL '1 minute' THEN 1 END) as messages_processed
FROM time_series ts
LEFT JOIN queen.messages m ON DATE_TRUNC('minute', m.created_at) = ts.minute
LEFT JOIN queen.messages_status ms ON ms.message_id = m.id 
  AND ms.consumer_group IS NULL  -- Queue mode only
-- Add queue/namespace/task filters as needed
GROUP BY ts.minute
ORDER BY ts.minute DESC;

-- 2. Active queues in time range
SELECT 
  q.id, q.name, q.namespace, q.task,
  COUNT(DISTINCT p.id) as partition_count,
  SUM(pc.total_messages_consumed) as total_consumed
FROM queen.queues q
JOIN queen.partitions p ON p.queue_id = q.id
LEFT JOIN queen.partition_cursors pc ON pc.partition_id = p.id
LEFT JOIN queen.messages m ON m.partition_id = p.id
WHERE m.created_at >= $1 AND m.created_at <= $2
GROUP BY q.id, q.name, q.namespace, q.task;

-- 3. Message counts by status
SELECT 
  COUNT(DISTINCT m.id) as total_messages,
  COUNT(CASE WHEN ms.status = 'pending' THEN 1 END) as pending,
  COUNT(CASE WHEN ms.status = 'processing' THEN 1 END) as processing,
  COUNT(CASE WHEN ms.status = 'completed' THEN 1 END) as completed,
  COUNT(CASE WHEN ms.status = 'failed' THEN 1 END) as failed,
  COUNT(DISTINCT dlq.id) as dead_letter
FROM queen.messages m
LEFT JOIN queen.messages_status ms ON ms.message_id = m.id 
  AND ms.consumer_group IS NULL
LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
WHERE m.created_at >= $1 AND m.created_at <= $2;

-- 4. Active leases
SELECT 
  COUNT(*) as active_leases,
  COUNT(DISTINCT partition_id) as partitions_with_leases
FROM queen.partition_leases
WHERE lease_expires_at > NOW() AND released_at IS NULL;

-- 5. DLQ stats
SELECT 
  COUNT(*) as total_dlq_messages,
  COUNT(DISTINCT partition_id) as affected_partitions
FROM queen.dead_letter_queue
WHERE failed_at >= $1 AND failed_at <= $2;
```

**Curl Example:**

```bash
# Default (last hour)
curl -X GET "http://localhost:3000/api/v1/status" \
  -H "Content-Type: application/json"

# With time range
curl -X GET "http://localhost:3000/api/v1/status?from=2025-10-14T10:00:00Z&to=2025-10-14T11:00:00Z" \
  -H "Content-Type: application/json"

# Filter by namespace
curl -X GET "http://localhost:3000/api/v1/status?namespace=production" \
  -H "Content-Type: application/json"

# Filter by specific queue
curl -X GET "http://localhost:3000/api/v1/status?queue=user-events" \
  -H "Content-Type: application/json"
```

**Expected Response:**

```json
{
  "timeRange": {
    "from": "2025-10-14T10:00:00.000Z",
    "to": "2025-10-14T11:00:00.000Z"
  },
  "throughput": [
    {
      "timestamp": "2025-10-14T10:59:00.000Z",
      "ingested": 1250,
      "processed": 1180,
      "ingestedPerSecond": 20.8,
      "processedPerSecond": 19.7
    },
    {
      "timestamp": "2025-10-14T10:58:00.000Z",
      "ingested": 1320,
      "processed": 1290,
      "ingestedPerSecond": 22.0,
      "processedPerSecond": 21.5
    }
    // ... more minutes
  ],
  "queues": [
    {
      "id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
      "name": "user-events",
      "namespace": "production",
      "task": "analytics",
      "partitions": 8,
      "totalConsumed": 45230
    },
    {
      "id": "b2c3d4e5-f6a7-8901-bcde-f12345678901",
      "name": "order-processing",
      "namespace": "production",
      "task": null,
      "partitions": 4,
      "totalConsumed": 12450
    }
  ],
  "messages": {
    "total": 57680,
    "pending": 340,
    "processing": 28,
    "completed": 56890,
    "failed": 382,
    "deadLetter": 40
  },
  "leases": {
    "active": 12,
    "partitionsWithLeases": 12
  },
  "deadLetterQueue": {
    "totalMessages": 40,
    "affectedPartitions": 6,
    "topErrors": [
      {
        "error": "Timeout: Connection refused",
        "count": 15
      },
      {
        "error": "Invalid payload format",
        "count": 12
      }
    ]
  }
}
```

---

### 2. Queues List Endpoint

**Purpose:** List all queues with summary statistics.

**Endpoint:** `GET /api/v1/status/queues`

**Query Parameters:**
- `from` (optional): ISO 8601 timestamp, default: 1 hour ago
- `to` (optional): ISO 8601 timestamp, default: now
- `namespace` (optional): Filter by namespace
- `task` (optional): Filter by task
- `limit` (optional): Number of results, default: 100
- `offset` (optional): Pagination offset, default: 0

**Implementation Query:**

```sql
SELECT 
  q.id,
  q.name,
  q.namespace,
  q.task,
  q.priority,
  q.created_at,
  COUNT(DISTINCT p.id) as partition_count,
  COUNT(DISTINCT m.id) as total_messages,
  COUNT(CASE WHEN ms.status = 'pending' THEN 1 END) as pending,
  COUNT(CASE WHEN ms.status = 'processing' THEN 1 END) as processing,
  COUNT(CASE WHEN ms.status = 'completed' THEN 1 END) as completed,
  COUNT(CASE WHEN ms.status = 'failed' THEN 1 END) as failed,
  COUNT(DISTINCT dlq.id) as dead_letter,
  -- Lag calculation: oldest pending message age
  EXTRACT(EPOCH FROM (NOW() - MIN(CASE WHEN ms.status = 'pending' 
    THEN m.created_at END))) as lag_seconds,
  -- Average processing time for completed messages
  AVG(CASE WHEN ms.status = 'completed' AND ms.completed_at IS NOT NULL 
    THEN EXTRACT(EPOCH FROM (ms.completed_at - m.created_at)) END) as avg_processing_time_seconds
FROM queen.queues q
LEFT JOIN queen.partitions p ON p.queue_id = q.id
LEFT JOIN queen.messages m ON m.partition_id = p.id 
  AND m.created_at >= $1 AND m.created_at <= $2
LEFT JOIN queen.messages_status ms ON ms.message_id = m.id 
  AND ms.consumer_group IS NULL
LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
WHERE 1=1
  -- Add namespace/task filters
GROUP BY q.id, q.name, q.namespace, q.task, q.priority, q.created_at
ORDER BY q.priority DESC, q.name
LIMIT $3 OFFSET $4;
```

**Curl Example:**

```bash
# List all queues
curl -X GET "http://localhost:3000/api/v1/status/queues" \
  -H "Content-Type: application/json"

# Filter by namespace with pagination
curl -X GET "http://localhost:3000/api/v1/status/queues?namespace=production&limit=20&offset=0" \
  -H "Content-Type: application/json"

# Filter by task
curl -X GET "http://localhost:3000/api/v1/status/queues?task=analytics" \
  -H "Content-Type: application/json"
```

**Expected Response:**

```json
{
  "queues": [
    {
      "id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
      "name": "user-events",
      "namespace": "production",
      "task": "analytics",
      "priority": 10,
      "createdAt": "2025-10-01T08:30:00.000Z",
      "partitions": 8,
      "messages": {
        "total": 45230,
        "pending": 145,
        "processing": 12,
        "completed": 44890,
        "failed": 158,
        "deadLetter": 25
      },
      "lag": {
        "seconds": 34.5,
        "formatted": "34.5s"
      },
      "performance": {
        "avgProcessingTimeSeconds": 2.3,
        "avgProcessingTimeFormatted": "2.3s"
      }
    },
    {
      "id": "b2c3d4e5-f6a7-8901-bcde-f12345678901",
      "name": "order-processing",
      "namespace": "production",
      "task": null,
      "priority": 8,
      "createdAt": "2025-10-02T14:20:00.000Z",
      "partitions": 4,
      "messages": {
        "total": 12450,
        "pending": 195,
        "processing": 16,
        "completed": 12180,
        "failed": 44,
        "deadLetter": 15
      },
      "lag": {
        "seconds": 128.7,
        "formatted": "2m 8s"
      },
      "performance": {
        "avgProcessingTimeSeconds": 5.8,
        "avgProcessingTimeFormatted": "5.8s"
      }
    }
  ],
  "pagination": {
    "limit": 100,
    "offset": 0,
    "total": 2
  },
  "timeRange": {
    "from": "2025-10-14T10:00:00.000Z",
    "to": "2025-10-14T11:00:00.000Z"
  }
}
```

---

### 3. Queue Detail Endpoint

**Purpose:** Detailed view of a specific queue with partition-level breakdown.

**Endpoint:** `GET /api/v1/status/queues/:queueName`

**Query Parameters:**
- `from` (optional): ISO 8601 timestamp, default: 1 hour ago
- `to` (optional): ISO 8601 timestamp, default: now

**Implementation Query:**

```sql
-- Queue info with detailed partition stats
SELECT 
  q.id, q.name, q.namespace, q.task, q.priority,
  q.lease_time, q.retry_limit, q.ttl, q.max_queue_size,
  q.created_at,
  p.id as partition_id,
  p.name as partition_name,
  p.created_at as partition_created_at,
  p.last_activity as partition_last_activity,
  -- Partition message counts
  COUNT(DISTINCT m.id) as partition_total_messages,
  COUNT(CASE WHEN ms.status = 'pending' THEN 1 END) as partition_pending,
  COUNT(CASE WHEN ms.status = 'processing' THEN 1 END) as partition_processing,
  COUNT(CASE WHEN ms.status = 'completed' THEN 1 END) as partition_completed,
  COUNT(CASE WHEN ms.status = 'failed' THEN 1 END) as partition_failed,
  -- Cursor info
  pc.total_messages_consumed,
  pc.total_batches_consumed,
  pc.last_consumed_at,
  -- Lease info
  pl.lease_expires_at,
  pl.batch_size as lease_batch_size,
  pl.acked_count as lease_acked_count,
  -- Oldest/newest messages
  MIN(m.created_at) FILTER (WHERE ms.status = 'pending') as oldest_pending,
  MAX(m.created_at) as newest_message
FROM queen.queues q
LEFT JOIN queen.partitions p ON p.queue_id = q.id
LEFT JOIN queen.messages m ON m.partition_id = p.id 
  AND m.created_at >= $2 AND m.created_at <= $3
LEFT JOIN queen.messages_status ms ON ms.message_id = m.id 
  AND ms.consumer_group IS NULL
LEFT JOIN queen.partition_cursors pc ON pc.partition_id = p.id 
  AND pc.consumer_group = '__QUEUE_MODE__'
LEFT JOIN queen.partition_leases pl ON pl.partition_id = p.id 
  AND pl.consumer_group = '__QUEUE_MODE__' 
  AND pl.released_at IS NULL
WHERE q.name = $1
GROUP BY q.id, q.name, q.namespace, q.task, q.priority, q.lease_time, 
         q.retry_limit, q.ttl, q.max_queue_size, q.created_at,
         p.id, p.name, p.created_at, p.last_activity,
         pc.total_messages_consumed, pc.total_batches_consumed, pc.last_consumed_at,
         pl.lease_expires_at, pl.batch_size, pl.acked_count
ORDER BY p.name;
```

**Curl Example:**

```bash
# Get queue details
curl -X GET "http://localhost:3000/api/v1/status/queues/user-events" \
  -H "Content-Type: application/json"

# With custom time range
curl -X GET "http://localhost:3000/api/v1/status/queues/user-events?from=2025-10-14T09:00:00Z&to=2025-10-14T12:00:00Z" \
  -H "Content-Type: application/json"
```

**Expected Response:**

```json
{
  "queue": {
    "id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
    "name": "user-events",
    "namespace": "production",
    "task": "analytics",
    "priority": 10,
    "config": {
      "leaseTime": 300,
      "retryLimit": 3,
      "ttl": 3600,
      "maxQueueSize": 10000
    },
    "createdAt": "2025-10-01T08:30:00.000Z"
  },
  "totals": {
    "messages": {
      "total": 45230,
      "pending": 145,
      "processing": 12,
      "completed": 44890,
      "failed": 183
    },
    "partitions": 8,
    "consumed": 44890,
    "batches": 1876
  },
  "partitions": [
    {
      "id": "c1d2e3f4-a5b6-7890-cdef-012345678901",
      "name": "partition-0",
      "createdAt": "2025-10-01T08:30:05.000Z",
      "lastActivity": "2025-10-14T10:59:47.000Z",
      "messages": {
        "total": 5654,
        "pending": 18,
        "processing": 2,
        "completed": 5612,
        "failed": 22
      },
      "cursor": {
        "totalConsumed": 5612,
        "batchesConsumed": 235,
        "lastConsumedAt": "2025-10-14T10:59:45.000Z"
      },
      "lease": {
        "active": true,
        "expiresAt": "2025-10-14T11:04:45.000Z",
        "batchSize": 10,
        "ackedCount": 8
      },
      "lag": {
        "oldestPending": "2025-10-14T10:59:20.000Z",
        "lagSeconds": 27.2
      }
    }
    // ... more partitions
  ],
  "timeRange": {
    "from": "2025-10-14T10:00:00.000Z",
    "to": "2025-10-14T11:00:00.000Z"
  }
}
```

---

### 4. Queue Messages Endpoint

**Purpose:** List messages in a specific queue with filtering and pagination.

**Endpoint:** `GET /api/v1/status/queues/:queueName/messages`

**Query Parameters:**
- `status` (optional): Filter by status (pending, processing, completed, failed)
- `partition` (optional): Filter by partition name
- `from` (optional): ISO 8601 timestamp
- `to` (optional): ISO 8601 timestamp
- `limit` (optional): Number of results, default: 50
- `offset` (optional): Pagination offset, default: 0

**Implementation Query:**

```sql
SELECT 
  m.id,
  m.transaction_id,
  m.trace_id,
  m.created_at,
  m.is_encrypted,
  p.name as partition,
  ms.status,
  ms.worker_id,
  ms.locked_at,
  ms.completed_at,
  ms.failed_at,
  ms.error_message,
  ms.retry_count,
  ms.processing_at,
  -- Processing time for completed messages
  EXTRACT(EPOCH FROM (ms.completed_at - m.created_at)) as processing_time_seconds,
  -- Age for pending messages
  EXTRACT(EPOCH FROM (NOW() - m.created_at)) as age_seconds,
  -- Payload (if not encrypted or if decryption is handled separately)
  m.payload
FROM queen.messages m
JOIN queen.partitions p ON p.partition_id = m.partition_id
JOIN queen.queues q ON q.id = p.queue_id
LEFT JOIN queen.messages_status ms ON ms.message_id = m.id 
  AND ms.consumer_group IS NULL
WHERE q.name = $1
  AND m.created_at >= $2 AND m.created_at <= $3
  -- Add status/partition filters
ORDER BY m.created_at DESC
LIMIT $4 OFFSET $5;
```

**Curl Example:**

```bash
# List all messages in queue
curl -X GET "http://localhost:3000/api/v1/status/queues/user-events/messages" \
  -H "Content-Type: application/json"

# Filter by status
curl -X GET "http://localhost:3000/api/v1/status/queues/user-events/messages?status=pending" \
  -H "Content-Type: application/json"

# Filter by partition and status with pagination
curl -X GET "http://localhost:3000/api/v1/status/queues/user-events/messages?partition=partition-0&status=failed&limit=20&offset=0" \
  -H "Content-Type: application/json"

# Filter by time range
curl -X GET "http://localhost:3000/api/v1/status/queues/user-events/messages?from=2025-10-14T10:00:00Z&to=2025-10-14T11:00:00Z" \
  -H "Content-Type: application/json"
```

**Expected Response:**

```json
{
  "messages": [
    {
      "id": "d1e2f3a4-b5c6-7890-def0-123456789012",
      "transactionId": "txn_abc123def456",
      "traceId": "e2f3a4b5-c6d7-8901-ef01-234567890123",
      "partition": "partition-0",
      "createdAt": "2025-10-14T10:58:32.123Z",
      "status": "pending",
      "age": {
        "seconds": 85.4,
        "formatted": "1m 25s"
      },
      "retryCount": 0,
      "isEncrypted": false,
      "payload": {
        "userId": "user_12345",
        "event": "page_view",
        "url": "/dashboard",
        "timestamp": 1728901112123
      }
    },
    {
      "id": "f3a4b5c6-d7e8-9012-fa01-234567890124",
      "transactionId": "txn_def456ghi789",
      "traceId": "a4b5c6d7-e8f9-0123-a012-345678901235",
      "partition": "partition-2",
      "createdAt": "2025-10-14T10:57:18.456Z",
      "status": "completed",
      "completedAt": "2025-10-14T10:57:21.789Z",
      "processingTime": {
        "seconds": 3.333,
        "formatted": "3.3s"
      },
      "workerId": "worker-prod-01",
      "retryCount": 0,
      "isEncrypted": false,
      "payload": {
        "userId": "user_67890",
        "event": "purchase",
        "amount": 49.99
      }
    },
    {
      "id": "b5c6d7e8-f9a0-1234-b012-345678901236",
      "transactionId": "txn_ghi789jkl012",
      "traceId": "c6d7e8f9-a0b1-2345-c123-456789012347",
      "partition": "partition-1",
      "createdAt": "2025-10-14T10:56:45.678Z",
      "status": "failed",
      "failedAt": "2025-10-14T10:56:52.901Z",
      "processingTime": {
        "seconds": 7.223,
        "formatted": "7.2s"
      },
      "errorMessage": "Connection timeout to analytics service",
      "workerId": "worker-prod-03",
      "retryCount": 2,
      "isEncrypted": false,
      "payload": {
        "userId": "user_11111",
        "event": "signup",
        "source": "organic"
      }
    }
  ],
  "pagination": {
    "limit": 50,
    "offset": 0,
    "total": 145
  },
  "queue": "user-events",
  "filters": {
    "status": null,
    "partition": null
  },
  "timeRange": {
    "from": "2025-10-14T10:00:00.000Z",
    "to": "2025-10-14T11:00:00.000Z"
  }
}
```

---

### 5. Analytics Endpoint

**Purpose:** Advanced analytics with trends, percentiles, and time-series data.

**Endpoint:** `GET /api/v1/status/analytics`

**Query Parameters:**
- `from` (optional): ISO 8601 timestamp, default: 24 hours ago
- `to` (optional): ISO 8601 timestamp, default: now
- `queue` (optional): Filter by queue
- `namespace` (optional): Filter by namespace
- `task` (optional): Filter by task
- `interval` (optional): Time bucket size (minute, hour, day), default: hour

**Metrics Provided:**
1. **Throughput trends** - Messages/second over time
2. **Latency percentiles** - p50, p95, p99 processing times
3. **Error rates** - Failed messages over time
4. **Queue depths** - Pending message counts over time
5. **Consumer performance** - Messages per consumer group
6. **DLQ trends** - Dead letter messages over time

**Curl Example:**

```bash
# Get analytics for last 24 hours
curl -X GET "http://localhost:3000/api/v1/status/analytics" \
  -H "Content-Type: application/json"

# Get hourly analytics for specific namespace
curl -X GET "http://localhost:3000/api/v1/status/analytics?namespace=production&interval=hour" \
  -H "Content-Type: application/json"

# Get minute-level analytics for specific queue
curl -X GET "http://localhost:3000/api/v1/status/analytics?queue=user-events&interval=minute&from=2025-10-14T10:00:00Z&to=2025-10-14T11:00:00Z" \
  -H "Content-Type: application/json"
```

**Expected Response:**

```json
{
  "timeRange": {
    "from": "2025-10-13T11:00:00.000Z",
    "to": "2025-10-14T11:00:00.000Z",
    "interval": "hour"
  },
  "throughput": {
    "timeSeries": [
      {
        "timestamp": "2025-10-14T10:00:00.000Z",
        "ingested": 75430,
        "processed": 74120,
        "failed": 245,
        "ingestedPerSecond": 20.95,
        "processedPerSecond": 20.59
      },
      {
        "timestamp": "2025-10-14T09:00:00.000Z",
        "ingested": 82150,
        "processed": 81890,
        "failed": 180,
        "ingestedPerSecond": 22.82,
        "processedPerSecond": 22.75
      }
      // ... more hours
    ],
    "totals": {
      "ingested": 1847230,
      "processed": 1834560,
      "failed": 4890,
      "avgIngestedPerSecond": 21.38,
      "avgProcessedPerSecond": 21.23
    }
  },
  "latency": {
    "timeSeries": [
      {
        "timestamp": "2025-10-14T10:00:00.000Z",
        "p50": 2.1,
        "p95": 8.7,
        "p99": 15.3,
        "avg": 3.4,
        "min": 0.8,
        "max": 28.9
      }
      // ... more hours
    ],
    "overall": {
      "p50": 2.3,
      "p95": 9.2,
      "p99": 16.8,
      "avg": 3.6
    }
  },
  "errorRates": {
    "timeSeries": [
      {
        "timestamp": "2025-10-14T10:00:00.000Z",
        "failed": 245,
        "processed": 74120,
        "rate": 0.33,
        "ratePercent": "0.33%"
      }
      // ... more hours
    ],
    "overall": {
      "failed": 4890,
      "processed": 1834560,
      "rate": 0.27,
      "ratePercent": "0.27%"
    }
  },
  "queueDepths": {
    "timeSeries": [
      {
        "timestamp": "2025-10-14T10:00:00.000Z",
        "pending": 1245,
        "processing": 89
      }
      // ... more hours
    ],
    "current": {
      "pending": 1310,
      "processing": 73
    }
  },
  "topQueues": [
    {
      "name": "user-events",
      "namespace": "production",
      "messagesProcessed": 456230,
      "avgProcessingTime": 2.3,
      "errorRate": 0.18
    },
    {
      "name": "order-processing",
      "namespace": "production",
      "messagesProcessed": 234510,
      "avgProcessingTime": 5.6,
      "errorRate": 0.42
    }
  ],
  "deadLetterQueue": {
    "timeSeries": [
      {
        "timestamp": "2025-10-14T10:00:00.000Z",
        "messages": 15
      }
      // ... more hours
    ],
    "total": 342,
    "topErrors": [
      {
        "error": "Timeout: Connection refused",
        "count": 125,
        "percentage": 36.5
      },
      {
        "error": "Invalid payload format",
        "count": 89,
        "percentage": 26.0
      }
    ]
  }
}
```

---

## Implementation Notes

### Performance Considerations

1. **Indexes Required:**
   - Already exists: `idx_messages_created_at` on `queen.messages(created_at)`
   - Already exists: `idx_messages_partition_created` on `queen.messages(partition_id, created_at)`
   - Consider adding: `idx_messages_status_completed_at` on `queen.messages_status(completed_at)` for throughput queries

2. **Caching Strategy:**
   - Cache aggregated statistics for 30 seconds
   - Use Redis or in-memory cache with TTL
   - Invalidate on new message ingestion (for real-time updates)

3. **Query Optimization:**
   - Use CTEs (Common Table Expressions) for complex time-series queries
   - Limit result sets with proper pagination
   - Consider materialized views for heavy analytics queries

4. **Rate Limiting:**
   - Implement rate limiting on analytics endpoints (e.g., 60 requests/minute per client)
   - Use sliding window algorithm

### Error Handling

All endpoints should return consistent error responses:

```json
{
  "error": {
    "code": "QUEUE_NOT_FOUND",
    "message": "Queue 'user-events' does not exist",
    "statusCode": 404
  }
}
```

Common error codes:
- `400` - Bad request (invalid parameters)
- `404` - Resource not found
- `429` - Rate limit exceeded
- `500` - Internal server error

### WebSocket Support

For real-time dashboard updates, consider implementing WebSocket endpoints:

```javascript
// Connect to WebSocket
const ws = new WebSocket('ws://localhost:3000/ws/dashboard');

// Subscribe to queue updates
ws.send(JSON.stringify({
  action: 'subscribe',
  queues: ['user-events', 'order-processing']
}));

// Receive real-time updates
ws.onmessage = (event) => {
  const update = JSON.parse(event.data);
  // update.type: 'throughput' | 'depth' | 'message'
  // update.data: {...}
};
```

---

## Summary

This API design provides:

✅ **Comprehensive dashboard overview** - `/api/v1/status`  
✅ **Queue listing with stats** - `/api/v1/status/queues`  
✅ **Detailed queue inspection** - `/api/v1/status/queues/:queueName`  
✅ **Message browsing** - `/api/v1/status/queues/:queueName/messages`  
✅ **Advanced analytics** - `/api/v1/status/analytics`  

All endpoints support:
- Time range filtering
- Namespace/task/queue filtering
- Pagination
- Consistent JSON responses
- Performance-optimized SQL queries leveraging existing indexes

The implementation leverages the existing database schema effectively, using `partition_cursors` for consumption tracking, `messages_status` for state management, and time-based aggregations for throughput calculations.
