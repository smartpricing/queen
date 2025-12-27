# HTTP API Reference

Complete HTTP API documentation for Queen MQ. All operations are available through HTTP endpoints.

## Base URL

```
http://localhost:6632/api/v1
```

## Authentication

By default, Queen has no authentication. For production deployments, use the [authentication proxy](/proxy/overview) which provides JWT-based auth and role-based access control.

## API Endpoints Summary

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
| **Consumer Groups** | `/api/v1/consumer-groups` | GET | All consumer groups with lag |

---

## Health & Monitoring

### GET `/health`

Health check endpoint.

```bash
curl http://localhost:6632/health
```

**Response:**
```json
{
  "status": "healthy",
  "database": "connected",
  "server": "C++ Queen Server (Acceptor/Worker)",
  "worker_id": 0,
  "version": "1.0.0"
}
```

---

### GET `/metrics`

Get performance metrics and system statistics.

```bash
curl http://localhost:6632/metrics
```

**Response includes:**
- Request counts and rates
- Message throughput
- Queue depths
- Database connection pool status
- Worker thread status

---

## Queue Configuration

### POST `/api/v1/configure`

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
      "encryptionEnabled": false,
      "deadLetterQueue": true,
      "dlqAfterMaxRetries": true,
      "maxWaitTimeSeconds": 0
    }
  }'
```

**Options:**

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `leaseTime` | int | 300 | Time in seconds before message lease expires |
| `maxSize` | int | 10000 | Maximum queue size |
| `retryLimit` | int | 3 | Max retry attempts |
| `retryDelay` | int | 1000 | Delay in milliseconds between retries |
| `priority` | int | 0 | Queue priority (0-10) |
| `delayedProcessing` | int | 0 | Delay in seconds before message is available |
| `windowBuffer` | int | 0 | Time in seconds messages wait before being available |
| `retentionSeconds` | int | 0 | Retention time for pending messages (0 = forever) |
| `completedRetentionSeconds` | int | 0 | Retention time for completed messages (0 = forever) |
| `encryptionEnabled` | bool | false | Enable message encryption |
| `deadLetterQueue` | bool | false | Enable dead letter queue functionality |
| `dlqAfterMaxRetries` | bool | false | Automatically move messages to DLQ after max retries |
| `maxWaitTimeSeconds` | int | 0 | Maximum time a message can wait before being moved to DLQ |

**Response:**
```json
{
  "success": true,
  "queue": "myqueue"
}
```

---

## Message Operations

### POST `/api/v1/push`

Push messages to a queue.

**Basic Push:**
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

**With QoS 0 Buffering (10-100x Performance Improvement):**

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

Server batches events and flushes to database after 100ms or 100 events (whichever comes first).

**Request Body:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `items` | array | Yes | Array of messages to push |
| `items[].queue` | string | Yes | Queue name |
| `items[].partition` | string | No | Partition name (default: "Default") |
| `items[].payload` | object | Yes | Message data |
| `items[].transactionId` | string | No | Unique transaction ID (auto-generated if not provided) |
| `items[].traceId` | string | No | Trace ID for debugging workflows |
| `bufferMs` | int | No | QoS 0: Buffer time in milliseconds |
| `bufferMax` | int | No | QoS 0: Max messages before flush |

**Response:**
```json
{
  "pushed": true,
  "qos0": false,
  "dbHealthy": true,
  "failover": false,
  "messages": [
    {
      "id": "msg-uuid",
      "transactionId": "tx-123",
      "queue": "myqueue",
      "partition": "Default",
      "status": "pending"
    }
  ]
}
```

---

### GET `/api/v1/pop/queue/:queue`

Pop messages from any partition of a queue.

**Queue Mode (Default):**
```bash
curl "http://localhost:6632/api/v1/pop/queue/myqueue?batch=10&wait=true&timeout=30000"
```

**Bus Mode (Consumer Group):**
```bash
curl "http://localhost:6632/api/v1/pop/queue/myqueue?consumerGroup=workers&batch=10"
```

**With Auto-Ack:**
```bash
curl "http://localhost:6632/api/v1/pop/queue/events?autoAck=true&batch=10"
```

**Query Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `batch` | int | 1 | Number of messages to retrieve |
| `wait` | bool | false | Wait for messages if queue empty (long polling) |
| `timeout` | int | 30000 | Timeout in ms for long polling |
| `consumerGroup` | string | `__QUEUE_MODE__` | Consumer group name for bus mode |
| `autoAck` | bool | false | Auto-acknowledge messages on delivery |

**Response:**
```json
{
  "messages": [
    {
      "id": "msg-uuid",
      "transactionId": "tx-123",
      "partitionId": "partition-uuid",
      "partitionName": "Default",
      "queueName": "myqueue",
      "leaseId": "lease-uuid",
      "leaseExpiresAt": "2025-11-13T12:00:00Z",
      "data": {"orderId": 123},
      "createdAt": "2025-11-13T11:00:00Z",
      "sequence": 1,
      "retryCount": 0
    }
  ]
}
```

---

### GET `/api/v1/pop/queue/:queue/partition/:partition`

Pop messages from a specific partition.

```bash
curl "http://localhost:6632/api/v1/pop/queue/myqueue/partition/customer-123?batch=5"
```

Query parameters are the same as above.

---

### GET `/api/v1/pop`

Pop messages with namespace/task filtering.

**By Namespace:**
```bash
curl "http://localhost:6632/api/v1/pop?namespace=billing&batch=10"
```

**By Namespace and Task:**
```bash
curl "http://localhost:6632/api/v1/pop?namespace=billing&task=process-invoice&batch=10"
```

**With Consumer Group:**
```bash
curl "http://localhost:6632/api/v1/pop?namespace=billing&consumerGroup=workers"
```

**Query Parameters:**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `namespace` | string | No | Filter by namespace |
| `task` | string | No | Filter by task |
| `consumerGroup` | string | No | Consumer group name |
| `batch` | int | No | Number of messages |
| `wait` | bool | No | Long polling |
| `timeout` | int | No | Timeout in ms |

---

### POST `/api/v1/ack`

Acknowledge a single message.

::: warning IMPORTANT
`partitionId` is **required** to ensure the correct message is acknowledged, especially when `transactionId` values may not be unique across partitions.
:::

**Acknowledge as Completed:**
```bash
curl -X POST http://localhost:6632/api/v1/ack \
  -H "Content-Type: application/json" \
  -d '{
    "transactionId": "msg-transaction-id",
    "partitionId": "partition-uuid",
    "status": "completed"
  }'
```

**Acknowledge as Failed (Will Retry):**
```bash
curl -X POST http://localhost:6632/api/v1/ack \
  -H "Content-Type: application/json" \
  -d '{
    "transactionId": "msg-transaction-id",
    "partitionId": "partition-uuid",
    "status": "failed",
    "error": "Processing error message"
  }'
```

**With Consumer Group:**
```bash
curl -X POST http://localhost:6632/api/v1/ack \
  -H "Content-Type: application/json" \
  -d '{
    "transactionId": "msg-transaction-id",
    "partitionId": "partition-uuid",
    "status": "completed",
    "consumerGroup": "workers"
  }'
```

**Request Body:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `transactionId` | string | Yes | Message transaction ID |
| `partitionId` | string | **Yes** | Partition UUID (mandatory) |
| `status` | string | Yes | "completed" or "failed" |
| `consumerGroup` | string | No | Consumer group name (if used) |
| `error` | string | No | Error message (if failed) |

**Response:**
```json
{
  "acknowledged": true
}
```

---

### POST `/api/v1/ack/batch`

Acknowledge multiple messages at once.

::: warning IMPORTANT
`partitionId` is **required** for each acknowledgment.
:::

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

**Response:**
```json
{
  "acknowledged": 2,
  "failed": 0
}
```

---

## Advanced Features

### POST `/api/v1/transaction`

Execute atomic operations (push + ack).

::: warning IMPORTANT
`partitionId` is **required** for ack operations.
:::

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

**Response:**
```json
{
  "success": true,
  "results": [
    {"operation": "ack", "success": true},
    {"operation": "push", "success": true, "messages": [...]}
  ]
}
```

**Use Case:** Ensure exactly-once processing by atomically acknowledging input and pushing output.

---

### POST `/api/v1/lease/:leaseId/extend`

Extend message lease for long-running operations.

```bash
curl -X POST http://localhost:6632/api/v1/lease/lease-uuid/extend \
  -H "Content-Type: application/json" \
  -d '{"seconds": 60}'
```

**Response:**
```json
{
  "extended": true,
  "newExpiresAt": "2025-11-13T12:01:00Z"
}
```

---

## Resources & Analytics

### GET `/api/v1/resources/overview`

System overview with statistics.

```bash
curl http://localhost:6632/api/v1/resources/overview
```

**Response:**
```json
{
  "queues": 42,
  "totalMessages": 15234,
  "pendingMessages": 1523,
  "completedMessages": 13711,
  "consumerGroups": 8,
  "partitions": 156,
  "serverVersion": "1.0.0",
  "uptime": 86400
}
```

---

### GET `/api/v1/resources/queues`

List all queues.

```bash
curl http://localhost:6632/api/v1/resources/queues
```

**Response:**
```json
{
  "queues": [
    {
      "name": "orders",
      "namespace": "billing",
      "task": "process",
      "pendingMessages": 123,
      "completedMessages": 4567,
      "partitions": 10,
      "consumerGroups": 2,
      "config": {
        "leaseTime": 300,
        "retryLimit": 3,
        "priority": 5
      }
    }
  ]
}
```

---

### GET `/api/v1/resources/queues/:queue`

Get details for a specific queue.

```bash
curl http://localhost:6632/api/v1/resources/queues/myqueue
```

---

### DELETE `/api/v1/resources/queues/:queue`

Delete a queue.

```bash
curl -X DELETE http://localhost:6632/api/v1/resources/queues/myqueue
```

::: danger Warning
This deletes all messages, partitions, and consumer group state for this queue. Cannot be undone!
:::

---

### GET `/api/v1/resources/namespaces`

List all namespaces.

```bash
curl http://localhost:6632/api/v1/resources/namespaces
```

---

### GET `/api/v1/resources/tasks`

List all tasks.

```bash
curl http://localhost:6632/api/v1/resources/tasks
```

---

### GET `/api/v1/messages`

List messages with filters.

```bash
curl "http://localhost:6632/api/v1/messages?queue=myqueue&status=pending&limit=50"
```

**Query Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `queue` | string | Filter by queue name |
| `partition` | string | Filter by partition |
| `ns` | string | Filter by namespace |
| `task` | string | Filter by task |
| `status` | string | Filter by status (pending, completed, failed) |
| `limit` | int | Number of results (default: 50, max: 1000) |
| `offset` | int | Pagination offset (default: 0) |

---

### GET `/api/v1/messages/:transactionId`

Get details for a specific message.

```bash
curl http://localhost:6632/api/v1/messages/transaction-id-here
```

---

### GET `/api/v1/dlq`

Get dead letter queue (DLQ) messages with optional filters.

**All DLQ messages for a queue:**
```bash
curl "http://localhost:6632/api/v1/dlq?queue=myqueue&limit=50"
```

**For specific consumer group:**
```bash
curl "http://localhost:6632/api/v1/dlq?queue=myqueue&consumerGroup=workers"
```

**With date range:**
```bash
curl "http://localhost:6632/api/v1/dlq?queue=myqueue&from=2024-01-01&to=2024-01-31"
```

**For specific partition:**
```bash
curl "http://localhost:6632/api/v1/dlq?queue=myqueue&partition=customer-123"
```

**Query Parameters:**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `queue` | string | Yes | Queue name |
| `consumerGroup` | string | No | Filter by consumer group |
| `partition` | string | No | Filter by partition name |
| `from` | string | No | Start date/time (ISO 8601) |
| `to` | string | No | End date/time (ISO 8601) |
| `limit` | int | No | Number of results (default: 100) |
| `offset` | int | No | Pagination offset (default: 0) |

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
      "data": {"orderId": "12345", "amount": 100},
      "traceId": "trace-uuid"
    }
  ],
  "total": 42,
  "limit": 100,
  "offset": 0
}
```

---

## Dashboard & Status

### GET `/api/v1/status`

Dashboard overview with throughput metrics.

```bash
curl "http://localhost:6632/api/v1/status?from=2024-01-01&to=2024-01-31"
```

**Response includes:**
- Total messages
- Throughput (messages/second)
- Queue statistics
- Consumer group status
- System health

---

### GET `/api/v1/status/queues`

List queues with statistics.

```bash
curl "http://localhost:6632/api/v1/status/queues?limit=100"
```

---

### GET `/api/v1/status/queues/:queue`

Detailed queue statistics.

```bash
curl http://localhost:6632/api/v1/status/queues/myqueue
```

**Response:**
```json
{
  "queue": "myqueue",
  "pending": 123,
  "completed": 4567,
  "failed": 12,
  "dlq": 3,
  "partitions": [
    {
      "name": "Default",
      "pending": 50,
      "consumers": 2
    }
  ],
  "consumerGroups": [
    {
      "name": "workers",
      "lag": 45,
      "members": 5
    }
  ],
  "throughput": {
    "last_minute": 125,
    "last_hour": 7500,
    "last_day": 180000
  }
}
```

---

### GET `/api/v1/status/queues/:queue/messages`

Get messages for a specific queue.

```bash
curl "http://localhost:6632/api/v1/status/queues/myqueue/messages?limit=50"
```

---

### GET `/api/v1/status/analytics`

Get analytics data with time-series metrics.

```bash
curl "http://localhost:6632/api/v1/status/analytics?interval=hour&from=2024-01-01"
```

**Query Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `interval` | string | Time interval: hour, day, week, month (default: hour) |
| `from` | string | Start date (ISO 8601) |
| `to` | string | End date (ISO 8601) |
| `queue` | string | Filter by queue |
| `namespace` | string | Filter by namespace |
| `task` | string | Filter by task |

---

### GET `/api/v1/consumer-groups`

Get all consumer groups with their topics, members, and lag statistics.

```bash
curl "http://localhost:6632/api/v1/consumer-groups"
```

**Response:**
```json
{
  "consumerGroups": [
    {
      "name": "workers",
      "topics": ["orders", "events"],
      "members": 5,
      "totalLag": 245,
      "maxTimeLag": 120,
      "state": "Stable",
      "queues": [
        {
          "queue": "orders",
          "partitions": [
            {
              "partition": "customer-123",
              "lag": 45,
              "lastConsumedAt": "2025-11-13T12:00:00Z"
            }
          ]
        }
      ]
    },
    {
      "name": "__QUEUE_MODE__",
      "topics": ["tasks"],
      "members": 2,
      "totalLag": 0,
      "maxTimeLag": 0,
      "state": "Stable",
      "queues": [...]
    }
  ]
}
```

**Response Fields:**
- `name` - Consumer group name (including `__QUEUE_MODE__` for queue-mode consumers)
- `topics` - Array of queue/topic names subscribed
- `members` - Number of partition consumers
- `totalLag` - Total offset lag across all partitions
- `maxTimeLag` - Maximum time lag in seconds
- `state` - Group state: "Stable", "Lagging", or "Dead"
- `queues` - Detailed per-queue partition information

---

## Error Responses

All endpoints return consistent error responses:

```json
{
  "error": true,
  "message": "Error description",
  "code": "ERROR_CODE",
  "details": {}
}
```

**Common Error Codes:**

| Code | HTTP Status | Description |
|------|-------------|-------------|
| `QUEUE_NOT_FOUND` | 404 | Queue does not exist |
| `MESSAGE_NOT_FOUND` | 404 | Message not found |
| `INVALID_REQUEST` | 400 | Invalid request parameters |
| `DUPLICATE_TRANSACTION` | 409 | Transaction ID already exists |
| `LEASE_EXPIRED` | 410 | Message lease has expired |
| `DATABASE_ERROR` | 500 | Database operation failed |
| `INTERNAL_ERROR` | 500 | Internal server error |

---

## Rate Limiting

By default, Queen has no rate limiting. For production deployments, consider:

1. **Use the proxy server** with built-in rate limiting
2. **Configure nginx/API gateway** in front of Queen
3. **Monitor queue depths** and adjust client behavior

---

## Best Practices

### 1. Use Batch Operations

```bash
# Bad: One message at a time
curl -X POST .../push -d '{"items": [{"queue": "q", "payload": {}}]}'
curl -X POST .../push -d '{"items": [{"queue": "q", "payload": {}}]}'

# Good: Batch multiple messages
curl -X POST .../push -d '{
  "items": [
    {"queue": "q", "payload": {}},
    {"queue": "q", "payload": {}},
    {"queue": "q", "payload": {}}
  ]
}'
```

### 2. Always Include partitionId in ACK

```bash
# Always include partitionId
curl -X POST .../ack -d '{
  "transactionId": "tx-123",
  "partitionId": "part-uuid",  # ← Required!
  "status": "completed"
}'
```

### 3. Use Long Polling for Consumers

```bash
# Good: Wait for messages
curl ".../pop/queue/tasks?wait=true&timeout=30000"

# Bad: Busy polling
while true; do curl ".../pop/queue/tasks"; sleep 1; done
```

### 4. Enable QoS 0 for High Throughput

```bash
# For high-throughput scenarios
curl -X POST .../push -d '{
  "items": [...],
  "bufferMs": 100,
  "bufferMax": 100
}'
```

### 5. Use Transactions for Exactly-Once

```bash
# Atomically ack input and push output
curl -X POST .../transaction -d '{
  "operations": [
    {"type": "ack", "transactionId": "in", "partitionId": "p1"},
    {"type": "push", "items": [...]}
  ]
}'
```

---

## Client Libraries

Instead of using the raw HTTP API, consider using client libraries:

- [JavaScript Client](/clients/javascript) - Full-featured Node.js client
- [C++ Client](/clients/cpp) - High-performance C++ client

---

## See Also

- [Quick Start Guide](/guide/quickstart) - Get started quickly
- [Concepts](/guide/concepts) - Understand core concepts
