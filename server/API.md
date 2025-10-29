# QueenMQ API

### API Endpoint Summary

| Category | Endpoint | Method | Description |
|----------|----------|--------|-------------|
| **Health** | `/health` | GET | Health check |
| **Health** | `/metrics` | GET | Performance metrics |
| **Queue Config** | `/api/v1/configure` | POST | Configure/create queue |
| **Messages** | `/api/v1/push` | POST | Push messages |
| **Messages** | `/api/v1/pop/queue/:queue` | GET | Pop from any partition |
| **Messages** | `/api/v1/pop/queue/:queue/partition/:partition` | GET | Pop from specific partition |
| **Messages** | `/api/v1/pop` | GET | Pop with namespace/task filter |
| **Messages** | `/api/v1/ack` | POST | Acknowledge single message |
| **Messages** | `/api/v1/ack/batch` | POST | Acknowledge batch |
| **Advanced** | `/api/v1/transaction` | POST | Atomic operations |
| **Advanced** | `/api/v1/lease/:leaseId/extend` | POST | Extend message lease |
| **Resources** | `/api/v1/resources/overview` | GET | System overview |
| **Resources** | `/api/v1/resources/queues` | GET | List all queues |
| **Resources** | `/api/v1/resources/queues/:queue` | GET | Queue details |
| **Resources** | `/api/v1/resources/queues/:queue` | DELETE | Delete queue |
| **Resources** | `/api/v1/resources/namespaces` | GET | List namespaces |
| **Resources** | `/api/v1/resources/tasks` | GET | List tasks |
| **Resources** | `/api/v1/messages` | GET | List messages (filtered) |
| **Resources** | `/api/v1/messages/:transactionId` | GET | Message details |
| **Resources** | `/api/v1/dlq` | GET | Dead letter queue messages |
| **Status** | `/api/v1/status` | GET | Dashboard overview |
| **Status** | `/api/v1/status/queues` | GET | Queue statistics |
| **Status** | `/api/v1/status/queues/:queue` | GET | Queue detail stats |
| **Status** | `/api/v1/status/queues/:queue/messages` | GET | Queue messages |
| **Status** | `/api/v1/status/analytics` | GET | Analytics time-series |

---

### Health & Monitoring

#### `GET /health`
Health check endpoint.
```bash
curl http://localhost:6632/health
```
Response:
```json
{
  "status": "healthy",
  "database": "connected",
  "server": "C++ Queen Server (Acceptor/Worker)",
  "worker_id": 0,
  "version": "1.0.0"
}
```

#### `GET /metrics`
Get performance metrics.
```bash
curl http://localhost:6632/metrics
```

---

### Queue Configuration

#### `POST /api/v1/configure`
Configure or create a queue with options.
```bash
curl -X POST http://localhost:6632/api/v1/configure \
  -H "Content-Type: application/json" \
  -d '{
    "queue": "myqueue",
    "namespace": "billing",
    "task": "process-invoice",
    "options": {
      "leaseTime": 300,
      "maxSize": 10000,
      "retryLimit": 3,
      "priority": 5,
      "delayedProcessing": 2,
      "windowBuffer": 0,
      "retentionSeconds": 3600,
      "completedRetentionSeconds": 86400,
      "encryptionEnabled": false
    }
  }'
```

**Options:**
- `leaseTime` - Time in seconds before message lease expires (default: 300)
- `maxSize` - Maximum queue size (default: 10000)
- `retryLimit` - Max retry attempts (default: 3)
- `retryDelay` - Delay in milliseconds between retries (default: 1000)
- `priority` - Queue priority (default: 0)
- `delayedProcessing` - Delay in seconds before message is available (default: 0)
- `windowBuffer` - Time in seconds messages wait before being available (default: 0)
- `retentionSeconds` - Retention time for pending messages (default: 0)
- `completedRetentionSeconds` - Retention time for completed messages (default: 0)
- `encryptionEnabled` - Enable message encryption (default: false)
- `deadLetterQueue` - Enable dead letter queue functionality (default: false)
- `dlqAfterMaxRetries` - Automatically move messages to DLQ after max retries (default: false)
- `maxWaitTimeSeconds` - Maximum time a message can wait before being moved to DLQ (default: 0)

---

### Message Operations

#### `POST /api/v1/push`
Push messages to a queue.
```bash
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{
    "items": [
      {
        "queue": "myqueue",
        "partition": "Default",
        "payload": {"data": "test"},
        "transactionId": "optional-tx-id",
        "traceId": "optional-trace-id"
      }
    ]
  }'
```

**QoS 0 Buffering (Optional):**

Add `bufferMs` and `bufferMax` to enable server-side batching for 10-100x performance improvement:

```bash
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{
    "items": [
      { "queue": "events", "payload": {"type": "login"} }
    ],
    "bufferMs": 100,
    "bufferMax": 100
  }'
```

Response includes buffer status:
```json
{
  "pushed": true,
  "qos0": true,
  "dbHealthy": true,
  "messages": [...]
}
```

Server batches events and flushes to database after 100ms or 100 events (whichever comes first).

#### `GET /api/v1/pop/queue/:queue`
Pop messages from any partition of a queue.
```bash
# Queue mode (default)
curl "http://localhost:6632/api/v1/pop/queue/myqueue?batch=10&wait=true&timeout=30000"

# Bus mode (consumer group)
curl "http://localhost:6632/api/v1/pop/queue/myqueue?consumerGroup=workers&batch=10"
```

**Query Parameters:**
- `batch` - Number of messages to retrieve (default: 1)
- `wait` - Wait for messages if queue empty (default: false)
- `timeout` - Timeout in ms for long polling (default: 30000)
- `consumerGroup` - Consumer group name for bus mode (default: "__QUEUE_MODE__")
- `autoAck` - Auto-acknowledge messages on delivery (default: false)

**Auto-Ack Example:**

```bash
# Pop with auto-acknowledgment (no manual ack needed)
curl "http://localhost:6632/api/v1/pop/queue/events?autoAck=true&batch=10"
```

Messages are immediately marked as consumed upon delivery.

#### `GET /api/v1/pop/queue/:queue/partition/:partition`
Pop messages from a specific partition.
```bash
curl "http://localhost:6632/api/v1/pop/queue/myqueue/partition/customer-123?batch=5"
```

#### `GET /api/v1/pop`
Pop messages with namespace/task filtering.
```bash
# By namespace only
curl "http://localhost:6632/api/v1/pop?namespace=billing&batch=10"

# By namespace and task
curl "http://localhost:6632/api/v1/pop?namespace=billing&task=process-invoice&batch=10"

# With consumer group
curl "http://localhost:6632/api/v1/pop?namespace=billing&consumerGroup=workers"
```

#### `POST /api/v1/ack`
Acknowledge a single message.

**⚠️ IMPORTANT:** `partitionId` is **required** to ensure the correct message is acknowledged, especially when `transactionId` values may not be unique across partitions.

```bash
# Acknowledge as completed
curl -X POST http://localhost:6632/api/v1/ack \
  -H "Content-Type: application/json" \
  -d '{
    "transactionId": "msg-transaction-id",
    "partitionId": "partition-uuid",
    "status": "completed"
  }'

# Acknowledge as failed (will retry)
curl -X POST http://localhost:6632/api/v1/ack \
  -H "Content-Type: application/json" \
  -d '{
    "transactionId": "msg-transaction-id",
    "partitionId": "partition-uuid",
    "status": "failed",
    "error": "Processing error message"
  }'

# With consumer group
curl -X POST http://localhost:6632/api/v1/ack \
  -H "Content-Type: application/json" \
  -d '{
    "transactionId": "msg-transaction-id",
    "partitionId": "partition-uuid",
    "status": "completed",
    "consumerGroup": "workers"
  }'
```

#### `POST /api/v1/ack/batch`
Acknowledge multiple messages at once.

**⚠️ IMPORTANT:** `partitionId` is **required** for each acknowledgment to ensure the correct messages are acknowledged, especially when `transactionId` values may not be unique across partitions.

```bash
curl -X POST http://localhost:6632/api/v1/ack/batch \
  -H "Content-Type: application/json" \
  -d '{
    "consumerGroup": "workers",
    "acknowledgments": [
      {
        "transactionId": "tx-1",
        "partitionId": "partition-uuid-1",
        "status": "completed"
      },
      {
        "transactionId": "tx-2",
        "partitionId": "partition-uuid-2",
        "status": "failed",
        "error": "Validation error"
      }
    ]
  }'
```

---

### Advanced Features

#### `POST /api/v1/transaction`
Execute atomic operations (push + ack).

**⚠️ IMPORTANT:** `partitionId` is **required** for ack operations to ensure the correct message is acknowledged.

```bash
curl -X POST http://localhost:6632/api/v1/transaction \
  -H "Content-Type: application/json" \
  -d '{
    "operations": [
      {
        "type": "ack",
        "transactionId": "original-msg-id",
        "partitionId": "partition-uuid",
        "status": "completed"
      },
      {
        "type": "push",
        "items": [
          {
            "queue": "next-queue",
            "partition": "Default",
            "payload": {"processed": true}
          }
        ]
      }
    ]
  }'
```

#### `POST /api/v1/lease/:leaseId/extend`
Extend message lease for long-running operations.
```bash
curl -X POST http://localhost:6632/api/v1/lease/lease-uuid/extend \
  -H "Content-Type: application/json" \
  -d '{"seconds": 60}'
```

---

### Resources & Analytics

#### `GET /api/v1/resources/overview`
System overview with statistics.
```bash
curl http://localhost:6632/api/v1/resources/overview
```

#### `GET /api/v1/resources/queues`
List all queues.
```bash
curl http://localhost:6632/api/v1/resources/queues
```

#### `GET /api/v1/resources/queues/:queue`
Get details for a specific queue.
```bash
curl http://localhost:6632/api/v1/resources/queues/myqueue
```

#### `DELETE /api/v1/resources/queues/:queue`
Delete a queue.
```bash
curl -X DELETE http://localhost:6632/api/v1/resources/queues/myqueue
```

#### `GET /api/v1/resources/namespaces`
List all namespaces.
```bash
curl http://localhost:6632/api/v1/resources/namespaces
```

#### `GET /api/v1/resources/tasks`
List all tasks.
```bash
curl http://localhost:6632/api/v1/resources/tasks
```

#### `GET /api/v1/messages`
List messages with filters.
```bash
curl "http://localhost:6632/api/v1/messages?queue=myqueue&status=pending&limit=50"
```

**Query Parameters:**
- `queue` - Filter by queue name
- `partition` - Filter by partition
- `ns` - Filter by namespace
- `task` - Filter by task
- `status` - Filter by status (pending, completed, failed)
- `limit` - Number of results (default: 50)
- `offset` - Pagination offset (default: 0)

#### `GET /api/v1/messages/:transactionId`
Get details for a specific message.
```bash
curl http://localhost:6632/api/v1/messages/transaction-id-here
```

#### `GET /api/v1/dlq`
Get dead letter queue (DLQ) messages with optional filters.
```bash
# Get all DLQ messages for a queue
curl "http://localhost:6632/api/v1/dlq?queue=myqueue&limit=50"

# Get DLQ messages for a specific consumer group
curl "http://localhost:6632/api/v1/dlq?queue=myqueue&consumerGroup=workers"

# Get DLQ messages with date range
curl "http://localhost:6632/api/v1/dlq?queue=myqueue&from=2024-01-01&to=2024-01-31"

# Get DLQ messages for a specific partition
curl "http://localhost:6632/api/v1/dlq?queue=myqueue&partition=customer-123"
```

**Query Parameters:**
- `queue` - Filter by queue name (required)
- `consumerGroup` - Filter by consumer group (optional)
- `partition` - Filter by partition name (optional)
- `from` - Start date/time (ISO 8601 format) (optional)
- `to` - End date/time (ISO 8601 format) (optional)
- `limit` - Number of results (default: 100)
- `offset` - Pagination offset (default: 0)

**Response:**
```json
{
  "messages": [
    {
      "id": "dlq-entry-id",
      "messageId": "original-message-id",
      "partitionId": "partition-uuid",
      "transactionId": "transaction-id",
      "consumerGroup": "__QUEUE_MODE__",
      "errorMessage": "Processing failed: invalid data",
      "retryCount": 3,
      "originalCreatedAt": "2024-01-15T10:30:00.000Z",
      "movedToDlqAt": "2024-01-15T10:35:00.000Z",
      "messageCreatedAt": "2024-01-15T10:30:00.000Z",
      "queueName": "myqueue",
      "namespace": "billing",
      "task": "process-invoice",
      "partitionName": "Default",
      "data": { "orderId": "12345", "amount": 100 },
      "traceId": "trace-uuid"
    }
  ],
  "total": 42,
  "limit": 100,
  "offset": 0
}
```

---

### Dashboard & Status

#### `GET /api/v1/status`
Dashboard overview with throughput metrics.
```bash
curl "http://localhost:6632/api/v1/status?from=2024-01-01&to=2024-01-31"
```

#### `GET /api/v1/status/queues`
List queues with statistics.
```bash
curl "http://localhost:6632/api/v1/status/queues?limit=100"
```

#### `GET /api/v1/status/queues/:queue`
Detailed queue statistics.
```bash
curl http://localhost:6632/api/v1/status/queues/myqueue
```

#### `GET /api/v1/status/queues/:queue/messages`
Get messages for a specific queue.
```bash
curl "http://localhost:6632/api/v1/status/queues/myqueue/messages?limit=50"
```

#### `GET /api/v1/status/analytics`
Get analytics data with time-series metrics.
```bash
curl "http://localhost:6632/api/v1/status/analytics?interval=hour&from=2024-01-01"
```

**Query Parameters:**
- `interval` - Time interval: hour, day, week, month (default: hour)
- `from` - Start date
- `to` - End date
- `queue` - Filter by queue
- `namespace` - Filter by namespace
- `task` - Filter by task

#### `GET /api/v1/consumer-groups`
Get all consumer groups with their topics, members, and lag statistics. Includes both named consumer groups (bus/pub-sub mode) and `__QUEUE_MODE__` (default queue mode).
```bash
curl "http://localhost:6632/api/v1/consumer-groups"
```

**Response:**
Returns an array of consumer groups with:
- `name` - Consumer group name (including `__QUEUE_MODE__` for queue-mode consumers)
- `topics` - Array of queue/topic names subscribed
- `members` - Number of partition consumers
- `totalLag` - Total offset lag across all partitions
- `maxTimeLag` - Maximum time lag in seconds
- `state` - Group state: "Stable", "Lagging", or "Dead"
- `queues` - Detailed per-queue partition information

---

See [server/README.md](server/README.md) for C++ server setup and configuration.


