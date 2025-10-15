# Queen Message Queue - API Documentation

**Base URL:** `http://localhost:6632`

**API Version:** v1

**Date:** October 15, 2025

---

## Table of Contents

1. [Authentication](#authentication)
2. [Health & Monitoring](#health--monitoring)
3. [Queue Management](#queue-management)
4. [Message Operations](#message-operations)
5. [Resource Queries](#resource-queries)
6. [Status & Analytics](#status--analytics)
7. [Error Responses](#error-responses)

---

## Authentication

Currently, the Queen API does not require authentication. All endpoints are publicly accessible. CORS is enabled with the following headers:

- `Access-Control-Allow-Origin`: `*`
- `Access-Control-Allow-Methods`: `GET, POST, PUT, DELETE, OPTIONS`
- `Access-Control-Allow-Headers`: `Content-Type, Authorization`

---

## Health & Monitoring

### GET /health

**Purpose:** Check server health and get basic performance statistics.

**Authentication:** None

**Query Parameters:** None

**Response:**
```json
{
  "status": "healthy",
  "uptime": "30s",
  "connections": 0,
  "stats": {
    "requests": 0,
    "messages": 0,
    "requestsPerSecond": "0.00",
    "messagesPerSecond": "0.00",
    "pool": {
      "total": 3,
      "idle": 3,
      "waiting": 0
    }
  }
}
```

**Status Codes:**
- `200`: Server is healthy
- `503`: Server is unhealthy

---

### GET /metrics

**Purpose:** Get detailed performance metrics for monitoring and observability.

**Authentication:** None

**Query Parameters:** None

**Response:**
```json
{
  "uptime": 44.691,
  "requests": {
    "total": 0,
    "rate": 0
  },
  "messages": {
    "total": 0,
    "rate": 0
  },
  "database": {
    "poolSize": 3,
    "idleConnections": 3,
    "waitingRequests": 0
  },
  "memory": {
    "rss": 51740672,
    "heapTotal": 9224192,
    "heapUsed": 7700024,
    "external": 2189469,
    "arrayBuffers": 103809
  },
  "cpu": {
    "user": 179881,
    "system": 47586
  }
}
```

**Status Codes:**
- `200`: Success

---

## Queue Management

### POST /api/v1/configure

**Purpose:** Create or configure a queue with specific settings and partitions.

**Authentication:** None

**Request Body:**
```json
{
  "queue": "test-queue",
  "partition": "Default",
  "ttl": 300,
  "priority": 1,
  "maxQueueSize": 1000
}
```

**Parameters:**
- `queue` (string, required): Queue name
- `partition` (string, optional): Partition name (defaults to "Default")
- `ttl` (number, optional): Time-to-live in seconds
- `priority` (number, optional): Queue priority (0-100)
- `maxQueueSize` (number, optional): Maximum queue size
- `leaseTime` (number, optional): Lease time for messages in seconds
- `retryLimit` (number, optional): Maximum retry attempts
- `retryDelay` (number, optional): Delay between retries in milliseconds

**Response:**
```json
{
  "queue": "test-queue",
  "namespace": null,
  "task": null,
  "configured": true,
  "options": {
    "leaseTime": 300,
    "maxSize": 10000,
    "ttl": 3600,
    "retryLimit": 3,
    "retryDelay": 1000,
    "deadLetterQueue": false,
    "dlqAfterMaxRetries": false,
    "priority": 0,
    "delayedProcessing": 0,
    "windowBuffer": 0,
    "retentionSeconds": 0,
    "completedRetentionSeconds": 0,
    "retentionEnabled": false
  },
  "partition": "Default",
  "_deprecation_notice": "Partition-level configuration is deprecated. All configuration is now at queue level."
}
```

**Status Codes:**
- `201`: Queue configured successfully
- `400`: Invalid request body
- `500`: Internal server error

---

## Message Operations

### POST /api/v1/push

**Purpose:** Push one or more messages to a queue.

**Authentication:** None

**Request Body:**
```json
{
  "items": [
    {
      "queue": "test-queue",
      "partition": "Default",
      "payload": {
        "message": "Hello World"
      },
      "ttl": 300,
      "priority": 1,
      "traceId": "optional-trace-id"
    }
  ]
}
```

**Parameters:**
- `items` (array, required): Array of messages to push
  - `queue` (string, required): Queue name
  - `partition` (string, optional): Partition name (defaults to "Default")
  - `payload` (object, required): Message payload (any JSON object)
  - `ttl` (number, optional): Message time-to-live in seconds
  - `priority` (number, optional): Message priority
  - `traceId` (string, optional): Trace ID for distributed tracing

**Response:**
```json
{
  "messages": [
    {
      "id": "0199e688-1857-7462-81ea-b87975de7e95",
      "transactionId": "0199e688-1857-7462-81ea-b4fe7532bbde",
      "traceId": null,
      "status": "queued"
    }
  ]
}
```

**Status Codes:**
- `201`: Messages pushed successfully
- `400`: Invalid request body
- `500`: Internal server error

---

### GET /api/v1/pop/queue/:queue/partition/:partition

**Purpose:** Pop messages from a specific queue and partition.

**Authentication:** None

**Path Parameters:**
- `queue` (string): Queue name
- `partition` (string): Partition name

**Query Parameters:**
- `wait` (boolean, optional): Wait for messages if queue is empty (default: false)
- `timeout` (number, optional): Wait timeout in milliseconds (default: 30000)
- `batch` (number, optional): Number of messages to pop (default: 1)
- `consumerGroup` (string, optional): Consumer group name for subscription mode
- `subscriptionMode` (string, optional): Subscription mode: "earliest", "latest", "timestamp"
- `subscriptionFrom` (string, optional): Starting point for subscription

**Response:**
```json
{
  "messages": [
    {
      "id": "0199e688-1857-7462-81ea-b87975de7e95",
      "transactionId": "0199e688-1857-7462-81ea-b4fe7532bbde",
      "traceId": null,
      "queue": "test-queue",
      "partition": "Default",
      "data": {
        "message": "Hello World"
      },
      "payload": {
        "message": "Hello World"
      },
      "retryCount": 0,
      "priority": "0",
      "createdAt": "2025-10-15T06:21:42.865Z",
      "consumerGroup": null
    }
  ]
}
```

**Status Codes:**
- `200`: Messages retrieved successfully
- `204`: No messages available
- `500`: Internal server error

---

### GET /api/v1/pop/queue/:queue

**Purpose:** Pop messages from a queue (any partition).

**Authentication:** None

**Path Parameters:**
- `queue` (string): Queue name

**Query Parameters:** Same as `/api/v1/pop/queue/:queue/partition/:partition`

**Response:** Same as `/api/v1/pop/queue/:queue/partition/:partition`

**Status Codes:**
- `200`: Messages retrieved successfully
- `204`: No messages available
- `500`: Internal server error

---

### GET /api/v1/pop

**Purpose:** Pop messages from queues filtered by namespace or task.

**Authentication:** None

**Query Parameters:**
- `namespace` (string, optional): Filter by namespace
- `task` (string, optional): Filter by task
- `wait` (boolean, optional): Wait for messages if queue is empty
- `timeout` (number, optional): Wait timeout in milliseconds
- `batch` (number, optional): Number of messages to pop
- `consumerGroup` (string, optional): Consumer group name

**Response:** Same as `/api/v1/pop/queue/:queue/partition/:partition`

**Status Codes:**
- `200`: Messages retrieved successfully
- `204`: No messages available
- `500`: Internal server error

---

### POST /api/v1/ack

**Purpose:** Acknowledge a single message as completed or failed.

**Authentication:** None

**Request Body:**
```json
{
  "transactionId": "0199e688-4d29-7019-bf32-5d4f21306b35",
  "status": "completed",
  "error": "optional error message if failed"
}
```

**Parameters:**
- `transactionId` (string, required): Transaction ID of the message
- `status` (string, required): "completed" or "failed"
- `error` (string, optional): Error message if status is "failed"
- `consumerGroup` (string, optional): Consumer group name

**Response:**
```json
{
  "status": "completed",
  "consumerGroup": null,
  "acknowledgedAt": "2025-10-15T06:22:02.382Z"
}
```

**Status Codes:**
- `200`: Acknowledgment successful
- `400`: Invalid request body
- `500`: Internal server error

---

### POST /api/v1/ack/batch

**Purpose:** Acknowledge multiple messages in a single request.

**Authentication:** None

**Request Body:**
```json
{
  "acknowledgments": [
    {
      "transactionId": "0199e688-6c45-769d-921b-527ee7c3d57c",
      "status": "completed"
    },
    {
      "transactionId": "0199e688-6c45-769d-921b-55f611d0cd5a",
      "status": "failed",
      "error": "Test error"
    }
  ],
  "consumerGroup": "optional-consumer-group"
}
```

**Parameters:**
- `acknowledgments` (array, required): Array of acknowledgments
  - `transactionId` (string, required): Transaction ID
  - `status` (string, required): "completed" or "failed"
  - `error` (string, optional): Error message if failed
- `consumerGroup` (string, optional): Consumer group name

**Response:**
```json
{
  "processed": 2,
  "results": [
    {
      "transactionId": "0199e688-6c45-769d-921b-527ee7c3d57c",
      "status": "completed"
    },
    {
      "transactionId": "0199e688-6c45-769d-921b-55f611d0cd5a",
      "status": "failed_dlq"
    }
  ]
}
```

**Status Codes:**
- `200`: Batch acknowledgment successful
- `400`: Invalid request body
- `500`: Internal server error

---

### GET /api/v1/messages

**Purpose:** List messages with optional filters.

**Authentication:** None

**Query Parameters:**
- `queue` (string, optional): Filter by queue name
- `ns` (string, optional): Filter by namespace
- `task` (string, optional): Filter by task
- `status` (string, optional): Filter by status
- `limit` (number, optional): Number of messages to return (default: 100)
- `offset` (number, optional): Offset for pagination (default: 0)

**Response:**
```json
{
  "messages": []
}
```

**Note:** This endpoint currently has a database schema issue (`column m.status does not exist`).

**Status Codes:**
- `200`: Success
- `500`: Internal server error

---

### GET /api/v1/messages/:transactionId

**Purpose:** Get details of a specific message by transaction ID.

**Authentication:** None

**Path Parameters:**
- `transactionId` (string): Message transaction ID

**Response:**
```json
{
  "id": "0199e688-1857-7462-81ea-b87975de7e95",
  "transactionId": "0199e688-1857-7462-81ea-b4fe7532bbde",
  "queuePath": "test-queue/Default",
  "queue": "test-queue",
  "partition": "Default",
  "namespace": null,
  "task": null,
  "payload": {
    "message": "Hello World"
  },
  "createdAt": "2025-10-15T06:21:42.865Z",
  "queueConfig": {
    "leaseTime": 300,
    "retryLimit": 3,
    "retryDelay": 1000,
    "ttl": 3600,
    "priority": 0
  }
}
```

**Status Codes:**
- `200`: Message found
- `404`: Message not found
- `500`: Internal server error

---

### DELETE /api/v1/messages/:transactionId

**Purpose:** Delete a specific message by transaction ID.

**Authentication:** None

**Path Parameters:**
- `transactionId` (string): Message transaction ID

**Response:**
```json
{
  "deleted": true,
  "transactionId": "0199e688-1857-7462-81ea-b4fe7532bbde"
}
```

**Status Codes:**
- `200`: Message deleted
- `404`: Message not found
- `500`: Internal server error

---

### POST /api/v1/messages/:transactionId/retry

**Purpose:** Retry a failed message.

**Authentication:** None

**Path Parameters:**
- `transactionId` (string): Message transaction ID

**Response:**
```json
{
  "retried": true,
  "transactionId": "0199e688-a908-726e-9ca2-a2a9b312684d"
}
```

**Status Codes:**
- `200`: Message retried
- `500`: Internal server error

---

### POST /api/v1/messages/:transactionId/dlq

**Purpose:** Move a message to the dead letter queue.

**Authentication:** None

**Path Parameters:**
- `transactionId` (string): Message transaction ID

**Response:**
```json
{
  "movedToDLQ": true,
  "transactionId": "0199e688-a908-726e-9ca2-a2a9b312684d"
}
```

**Status Codes:**
- `200`: Message moved to DLQ
- `500`: Internal server error

---

### GET /api/v1/messages/:transactionId/related

**Purpose:** Get messages related to a specific message (e.g., by trace ID).

**Authentication:** None

**Path Parameters:**
- `transactionId` (string): Message transaction ID

**Response:**
```json
{
  "messages": []
}
```

**Note:** This endpoint currently has a database schema issue.

**Status Codes:**
- `200`: Success
- `500`: Internal server error

---

### DELETE /api/v1/queues/:queue/clear

**Purpose:** Clear all messages from a queue or specific partition.

**Authentication:** None

**Path Parameters:**
- `queue` (string): Queue name

**Query Parameters:**
- `partition` (string, optional): Partition name to clear (if omitted, clears all partitions)

**Response:**
```json
{
  "cleared": true,
  "count": 6,
  "queue": "test-queue",
  "partition": "Default"
}
```

**Status Codes:**
- `200`: Queue cleared
- `500`: Internal server error

---

## Resource Queries

### GET /api/v1/resources/queues

**Purpose:** Get a list of all queues with their statistics.

**Authentication:** None

**Query Parameters:**
- `namespace` (string, optional): Filter by namespace
- `task` (string, optional): Filter by task

**Response:**
```json
{
  "queues": [
    {
      "id": "03093457-e6f6-4e5f-869b-045a6916fdff",
      "name": "test-queue",
      "namespace": null,
      "task": null,
      "createdAt": "2025-10-15T06:21:42.034Z",
      "partitions": 1,
      "messages": {
        "total": 0,
        "pending": 2,
        "processing": 0
      }
    }
  ]
}
```

**Status Codes:**
- `200`: Success
- `500`: Internal server error

---

### GET /api/v1/resources/queues/:queue

**Purpose:** Get detailed information about a specific queue.

**Authentication:** None

**Path Parameters:**
- `queue` (string): Queue name

**Response:**
```json
{
  "id": "03093457-e6f6-4e5f-869b-045a6916fdff",
  "name": "test-queue",
  "namespace": null,
  "task": null,
  "createdAt": "2025-10-15T06:21:42.034Z",
  "partitions": [
    {
      "id": "7a30d768-7eb7-40dd-ade9-8afb20c18591",
      "name": "Default",
      "createdAt": "2025-10-15T06:21:42.865Z",
      "stats": {
        "total": 0,
        "pending": 2,
        "processing": 0,
        "completed": 3,
        "failed": 0,
        "deadLetter": 0
      },
      "oldestMessage": null,
      "newestMessage": null
    }
  ],
  "totals": {
    "total": 0,
    "pending": 2,
    "processing": 0,
    "completed": 3,
    "failed": 0,
    "deadLetter": 0
  }
}
```

**Status Codes:**
- `200`: Success
- `404`: Queue not found
- `500`: Internal server error

---

### DELETE /api/v1/resources/queues/:queue

**Purpose:** Delete a queue and all its messages.

**Authentication:** None

**Path Parameters:**
- `queue` (string): Queue name

**Response:**
```json
{
  "deleted": true,
  "queue": "test-queue"
}
```

**Status Codes:**
- `200`: Queue deleted
- `500`: Internal server error

---

### GET /api/v1/resources/partitions

**Purpose:** Get a list of partitions across all queues.

**Authentication:** None

**Query Parameters:**
- `queue` (string, optional): Filter by queue name
- `minDepth` (number, optional): Filter partitions with at least this many messages

**Response:**
```json
{
  "partitions": [
    {
      "id": "7a30d768-7eb7-40dd-ade9-8afb20c18591",
      "name": "Default",
      "queue": "test-queue",
      "namespace": null,
      "task": null,
      "queuePriority": 0,
      "createdAt": "2025-10-15T06:21:42.865Z",
      "depth": 2,
      "processing": 0,
      "total": 0
    }
  ]
}
```

**Status Codes:**
- `200`: Success
- `500`: Internal server error

---

### GET /api/v1/resources/namespaces

**Purpose:** Get a list of all namespaces with statistics.

**Authentication:** None

**Query Parameters:** None

**Response:**
```json
{
  "namespaces": [
    {
      "namespace": "benchmark",
      "queues": 51,
      "partitions": 510,
      "messages": {
        "total": 200000,
        "pending": 0
      }
    }
  ]
}
```

**Status Codes:**
- `200`: Success
- `500`: Internal server error

---

### GET /api/v1/resources/tasks

**Purpose:** Get a list of all tasks with statistics.

**Authentication:** None

**Query Parameters:** None

**Response:**
```json
{
  "tasks": []
}
```

**Status Codes:**
- `200`: Success
- `500`: Internal server error

---

### GET /api/v1/resources/overview

**Purpose:** Get a comprehensive system overview with all statistics.

**Authentication:** None

**Query Parameters:** None

**Response:**
```json
{
  "queues": 53,
  "partitions": 512,
  "namespaces": 1,
  "tasks": 0,
  "messages": {
    "total": 200000,
    "pending": 2,
    "processing": 0,
    "completed": 16493,
    "failed": 0,
    "deadLetter": 0
  },
  "timestamp": "2025-10-15T06:22:38.014Z"
}
```

**Status Codes:**
- `200`: Success
- `500`: Internal server error

---

## Status & Analytics

### GET /api/v1/status

**Purpose:** Get comprehensive dashboard status including throughput metrics and message statistics.

**Authentication:** None

**Query Parameters:**
- `from` (string, optional): Start date/time (ISO 8601)
- `to` (string, optional): End date/time (ISO 8601)
- `queue` (string, optional): Filter by queue name
- `namespace` (string, optional): Filter by namespace
- `task` (string, optional): Filter by task

**Response:**
```json
{
  "timeRange": {
    "from": "2025-10-15T05:22:39.466Z",
    "to": "2025-10-15T06:22:39.466Z"
  },
  "throughput": [
    {
      "timestamp": "2025-10-15T06:22:00.000Z",
      "ingested": 0,
      "processed": 0,
      "ingestedPerSecond": 0,
      "processedPerSecond": 0
    }
  ],
  "queues": [],
  "messages": {
    "total": 0,
    "pending": 2,
    "processing": 0,
    "completed": 16493,
    "failed": 0,
    "deadLetter": 0
  },
  "leases": {
    "active": 0,
    "partitionsWithLeases": 0,
    "totalBatchSize": 0,
    "totalAcked": 0
  },
  "deadLetterQueue": {
    "totalMessages": 0,
    "affectedPartitions": 0,
    "topErrors": []
  }
}
```

**Status Codes:**
- `200`: Success
- `500`: Internal server error

---

### GET /api/v1/status/queues

**Purpose:** Get a list of queues with detailed status information.

**Authentication:** None

**Query Parameters:**
- `from` (string, optional): Start date/time
- `to` (string, optional): End date/time
- `namespace` (string, optional): Filter by namespace
- `task` (string, optional): Filter by task
- `limit` (number, optional): Number of results
- `offset` (number, optional): Pagination offset

**Response:**
```json
{
  "queues": [
    {
      "id": "a6447f65-6c44-423e-a9b7-440fd7136d35",
      "name": "__system_events__",
      "namespace": null,
      "task": null,
      "priority": 100,
      "createdAt": "2025-10-14T12:45:32.356Z",
      "partitions": 1,
      "messages": {
        "total": 0,
        "pending": 0,
        "processing": 0,
        "completed": 0,
        "failed": 0,
        "deadLetter": 0
      },
      "lag": null,
      "performance": null
    }
  ]
}
```

**Status Codes:**
- `200`: Success
- `500`: Internal server error

---

### GET /api/v1/status/queues/:queue

**Purpose:** Get detailed status information for a specific queue.

**Authentication:** None

**Path Parameters:**
- `queue` (string): Queue name

**Query Parameters:**
- `from` (string, optional): Start date/time
- `to` (string, optional): End date/time

**Response:**
```json
{
  "queue": {
    "id": "03093457-e6f6-4e5f-869b-045a6916fdff",
    "name": "test-queue",
    "namespace": null,
    "task": null,
    "priority": 0,
    "config": {
      "leaseTime": 300,
      "retryLimit": 3,
      "ttl": 3600,
      "maxQueueSize": 0
    },
    "createdAt": "2025-10-15T06:21:42.034Z"
  },
  "totals": {
    "messages": {
      "total": 0,
      "pending": 0,
      "processing": 0,
      "completed": 3,
      "failed": 0
    },
    "partitions": 1,
    "consumed": 3,
    "batches": 2
  },
  "partitions": [
    {
      "id": "7a30d768-7eb7-40dd-ade9-8afb20c18591",
      "name": "Default",
      "createdAt": "2025-10-15T06:21:42.865Z",
      "lastActivity": "2025-10-15T06:22:21.725Z",
      "messages": {
        "total": 0,
        "pending": 0,
        "processing": 0,
        "completed": 3,
        "failed": 0
      },
      "cursor": {
        "totalConsumed": 3,
        "batchesConsumed": 2,
        "lastConsumedAt": "2025-10-15T06:22:10.537Z"
      }
    }
  ],
  "timeRange": {
    "from": "2025-10-15T05:22:47.435Z",
    "to": "2025-10-15T06:22:47.435Z"
  }
}
```

**Status Codes:**
- `200`: Success
- `404`: Queue not found
- `500`: Internal server error

---

### GET /api/v1/status/queues/:queue/messages

**Purpose:** Get messages from a specific queue with filtering and pagination.

**Authentication:** None

**Path Parameters:**
- `queue` (string): Queue name

**Query Parameters:**
- `status` (string, optional): Filter by message status
- `partition` (string, optional): Filter by partition
- `from` (string, optional): Start date/time
- `to` (string, optional): End date/time
- `limit` (number, optional): Number of results
- `offset` (number, optional): Pagination offset

**Response:**
```json
{
  "messages": [],
  "pagination": {
    "limit": 2,
    "offset": null,
    "total": 0
  },
  "queue": "test-queue",
  "filters": {
    "status": null,
    "partition": null
  },
  "timeRange": {
    "from": "2025-10-15T05:22:48.221Z",
    "to": "2025-10-15T06:22:48.221Z"
  }
}
```

**Status Codes:**
- `200`: Success
- `500`: Internal server error

---

### GET /api/v1/status/analytics

**Purpose:** Get analytics data including time series metrics.

**Authentication:** None

**Query Parameters:**
- `from` (string, optional): Start date/time
- `to` (string, optional): End date/time
- `queue` (string, optional): Filter by queue
- `namespace` (string, optional): Filter by namespace
- `task` (string, optional): Filter by task
- `interval` (string, optional): Time interval: "minute", "hour", "day" (default: "hour")

**Response:**
```json
{
  "timeRange": {
    "from": "2025-10-15T05:22:50.000Z",
    "to": "2025-10-15T06:22:50.000Z"
  },
  "interval": "hour",
  "timeSeries": [],
  "summary": null
}
```

**Status Codes:**
- `200`: Success
- `500`: Internal server error

---

## Error Responses

All endpoints may return error responses in the following format:

```json
{
  "error": "Error message description"
}
```

### Common Status Codes:

- `200 OK`: Request successful
- `201 Created`: Resource created successfully
- `204 No Content`: Request successful but no content to return
- `400 Bad Request`: Invalid request parameters or body
- `404 Not Found`: Resource not found
- `500 Internal Server Error`: Server error
- `503 Service Unavailable`: Server is unhealthy or unavailable

---

## WebSocket API

Queen also provides a WebSocket connection for real-time updates:

**WebSocket URL:** `ws://localhost:6632/ws/dashboard`

**Purpose:** Real-time updates for:
- Queue depth changes
- Message events (pushed, processing, completed, failed)
- System statistics

---

## Notes

1. **CORS**: All endpoints support CORS with permissive settings. In production, configure appropriate CORS settings.

2. **Pagination**: Most list endpoints support pagination via `limit` and `offset` query parameters.

3. **Time Ranges**: Status and analytics endpoints default to the last 1 hour if no time range is specified.

4. **Consumer Groups**: Queen supports consumer groups for subscription-based message consumption, enabling multiple consumers to process messages in parallel without duplication.

5. **Partitions**: Messages can be organized into partitions for better parallelism and ordering guarantees within partitions.

6. **Namespaces and Tasks**: Optional organizational features for grouping queues logically.

7. **Dead Letter Queue (DLQ)**: Failed messages can be moved to a DLQ for later analysis and reprocessing.

8. **Encryption**: Encryption can be enabled by setting the `QUEEN_ENCRYPTION_KEY` environment variable.

---

## Examples

### Creating a Queue and Sending Messages

```bash
# 1. Create a queue
curl -X POST http://localhost:6632/api/v1/configure \
  -H "Content-Type: application/json" \
  -d '{
    "queue": "my-queue",
    "partition": "Default",
    "ttl": 3600,
    "priority": 1
  }'

# 2. Push messages
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{
    "items": [
      {
        "queue": "my-queue",
        "partition": "Default",
        "payload": {"task": "process-order", "orderId": 123}
      }
    ]
  }'

# 3. Pop messages
curl "http://localhost:6632/api/v1/pop/queue/my-queue?batch=10"

# 4. Acknowledge message
curl -X POST http://localhost:6632/api/v1/ack \
  -H "Content-Type: application/json" \
  -d '{
    "transactionId": "<transaction-id-from-pop>",
    "status": "completed"
  }'
```

### Long Polling

```bash
# Wait up to 30 seconds for messages
curl "http://localhost:6632/api/v1/pop/queue/my-queue?wait=true&timeout=30000&batch=10"
```

### Subscription Mode (Consumer Groups)

```bash
# Subscribe from earliest message
curl "http://localhost:6632/api/v1/pop/queue/my-queue?consumerGroup=worker-group-1&subscriptionMode=earliest&batch=10"

# Acknowledge for consumer group
curl -X POST http://localhost:6632/api/v1/ack \
  -H "Content-Type: application/json" \
  -d '{
    "transactionId": "<transaction-id>",
    "status": "completed",
    "consumerGroup": "worker-group-1"
  }'
```

---

**Last Updated:** October 15, 2025  
**API Version:** v1  
**Server Version:** Queen Message Queue

