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
- `priority` - Queue priority (default: 0)
- `delayedProcessing` - Delay in seconds before message is available (default: 0)
- `windowBuffer` - Time in seconds messages wait before being available (default: 0)
- `retentionSeconds` - Retention time for pending messages (default: 0)
- `completedRetentionSeconds` - Retention time for completed messages (default: 0)
- `encryptionEnabled` - Enable message encryption (default: false)

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
```bash
# Acknowledge as completed
curl -X POST http://localhost:6632/api/v1/ack \
  -H "Content-Type: application/json" \
  -d '{
    "transactionId": "msg-transaction-id",
    "status": "completed"
  }'

# Acknowledge as failed (will retry)
curl -X POST http://localhost:6632/api/v1/ack \
  -H "Content-Type: application/json" \
  -d '{
    "transactionId": "msg-transaction-id",
    "status": "failed",
    "error": "Processing error message"
  }'

# With consumer group
curl -X POST http://localhost:6632/api/v1/ack \
  -H "Content-Type: application/json" \
  -d '{
    "transactionId": "msg-transaction-id",
    "status": "completed",
    "consumerGroup": "workers"
  }'
```

#### `POST /api/v1/ack/batch`
Acknowledge multiple messages at once.
```bash
curl -X POST http://localhost:6632/api/v1/ack/batch \
  -H "Content-Type: application/json" \
  -d '{
    "consumerGroup": "workers",
    "acknowledgments": [
      {
        "transactionId": "tx-1",
        "status": "completed"
      },
      {
        "transactionId": "tx-2",
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
```bash
curl -X POST http://localhost:6632/api/v1/transaction \
  -H "Content-Type: application/json" \
  -d '{
    "operations": [
      {
        "type": "ack",
        "transactionId": "original-msg-id",
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

---

See [server/README.md](server/README.md) for C++ server setup and configuration.


