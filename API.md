# Queen V2 API Documentation

## Overview

Queen V2 is a high-performance message queue system with a two-tier architecture:
- **Queues**: Top-level organizational units
- **Partitions**: Subdivisions within queues where FIFO ordering is maintained

Base URL: `http://localhost:6632/api/v1`

## Core Concepts

### Message Flow
1. Messages are **pushed** to a queue (optionally specifying a partition)
2. Messages are **popped** from either a specific partition or any partition in a queue
3. Messages must be **acknowledged** after processing (completed or failed)

### Partitions
- Every queue automatically has a "Default" partition
- Additional partitions can be created by pushing messages to them
- FIFO ordering is maintained within each partition
- Messages without a specified partition go to "Default"

### Optional Grouping
- Queues can have optional `namespace` and `task` fields for logical grouping
- These don't affect the hierarchy but allow filtering operations

---

## API Endpoints

### 1. Push Messages
**Endpoint:** `POST /api/v1/push`

Adds one or more messages to queues.

**Request Body:**
```json
{
  "items": [
    {
      "queue": "email-queue",           // Required: queue name
      "partition": "urgent",             // Optional: defaults to "Default"
      "payload": {                      // Required: message data (JSON)
        "to": "user@example.com",
        "subject": "Hello"
      },
      "transactionId": "uuid-here"      // Optional: idempotency key
    }
  ]
}
```

**Response:**
```json
{
  "messages": [
    {
      "id": "018e63b7-6165-453f-88ae-56effa177605",
      "transactionId": "4dfb0478-655b-4c91-bcd9-b7acacf0400f",
      "status": "queued"  // or "duplicate" if transactionId exists
    }
  ]
}
```

**Notes:**
- Supports batch operations (up to 1000 messages)
- Automatic duplicate detection via transactionId
- Creates queue/partition if they don't exist

---

### 2. Pop Messages

#### 2.1 Pop from Specific Partition
**Endpoint:** `GET /api/v1/pop/queue/:queue/partition/:partition`

Retrieves messages from a specific partition.

**Query Parameters:**
- `batch` (integer): Number of messages to retrieve (default: 1)
- `wait` (boolean): Enable long polling (default: false)
- `timeout` (integer): Long polling timeout in ms (default: 30000, max: 60000)

**Example:**
```
GET /api/v1/pop/queue/emails/partition/urgent?batch=5&wait=true
```

**Response:**
```json
{
  "messages": [
    {
      "id": "message-uuid",
      "transactionId": "transaction-uuid",
      "queue": "emails",
      "partition": "urgent",
      "data": { "to": "user@example.com", "subject": "Alert" },
      "retryCount": 0,
      "priority": 0,
      "createdAt": "2025-10-08T05:12:33.893Z",
      "lockedAt": null,
      "options": { "leaseTime": 300, "retryLimit": 3 }
    }
  ]
}
```

#### 2.2 Pop from Any Partition in Queue
**Endpoint:** `GET /api/v1/pop/queue/:queue`

Retrieves messages from any available partition in the queue (oldest first).

**Query Parameters:** Same as above

**Example:**
```
GET /api/v1/pop/queue/emails?batch=10
```

#### 2.3 Pop with Filters
**Endpoint:** `GET /api/v1/pop`

Retrieves messages from queues matching namespace or task filters.

**Query Parameters:**
- `namespace` (string): Filter by namespace
- `task` (string): Filter by task
- `batch`, `wait`, `timeout`: Same as above

**Example:**
```
GET /api/v1/pop?namespace=production&batch=5
GET /api/v1/pop?task=billing&wait=true
```

**Response Format:** Same as above

**Status Codes:**
- `200`: Messages retrieved successfully
- `204`: No messages available (empty response)

---

### 3. Acknowledge Messages

#### 3.1 Single Acknowledgment
**Endpoint:** `POST /api/v1/ack`

Acknowledges a single message as completed or failed.

**Request Body:**
```json
{
  "transactionId": "4dfb0478-655b-4c91-bcd9-b7acacf0400f",
  "status": "completed",  // or "failed"
  "error": "Error message if failed"  // Optional
}
```

**Response:**
```json
{
  "transactionId": "4dfb0478-655b-4c91-bcd9-b7acacf0400f",
  "status": "completed",
  "acknowledgedAt": "2025-10-08T07:12:47.353Z"
}
```

#### 3.2 Batch Acknowledgment
**Endpoint:** `POST /api/v1/ack/batch`

Acknowledges multiple messages at once.

**Request Body:**
```json
{
  "acknowledgments": [
    {
      "transactionId": "uuid-1",
      "status": "completed"
    },
    {
      "transactionId": "uuid-2",
      "status": "failed",
      "error": "Processing error"
    }
  ]
}
```

**Response:**
```json
{
  "processed": 2,
  "results": [
    { "transactionId": "uuid-1", "status": "completed" },
    { "transactionId": "uuid-2", "status": "retry_scheduled", "retryCount": 1 }
  ]
}
```

**Notes:**
- Failed messages are automatically retried based on partition's `retryLimit`
- After max retries, messages can be moved to dead letter queue

---

### 4. Configure Queue
**Endpoint:** `POST /api/v1/configure`

Configures options for a queue. All configuration is now at the queue level - partitions are simple FIFO containers.

**Request Body:**
```json
{
  "queue": "notifications",
  "partition": "critical",  // DEPRECATED: Ignored but accepted for backward compatibility
  "options": {
    "leaseTime": 600,       // Seconds before message lease expires
    "retryLimit": 5,        // Max retry attempts
    "priority": 10,         // Queue priority (higher = processed first)
    "maxSize": 10000,       // Max messages in queue
    "ttl": 3600,           // Time to live in seconds
    "dlqAfterMaxRetries": true,  // Move to DLQ after max retries
    "delayedProcessing": 0,      // Delay before messages are available (seconds)
    "windowBuffer": 0,            // Buffer window for message processing (seconds)
    "retentionSeconds": 0,       // Auto-delete pending messages after X seconds
    "completedRetentionSeconds": 0, // Auto-delete completed messages after X seconds
    "retentionEnabled": false,   // Enable retention policies
    "encryptionEnabled": false,  // Enable message encryption
    "maxWaitTimeSeconds": 0      // Max wait time for long polling
  }
}
```

**Response:**
```json
{
  "queue": "notifications",
  "configured": true,
  "options": { /* all options with defaults filled */ }
}
```

**Note:** The `partition` parameter in the request is deprecated and ignored. All configuration now applies to the entire queue.

---

### 5. Analytics & Monitoring

#### 5.1 Queue Statistics
**Endpoint:** `GET /api/v1/analytics/queue/:queue`

Gets detailed statistics for a specific queue.

**Response:**
```json
{
  "queue": "emails",
  "namespace": null,
  "task": null,
  "totals": {
    "pending": 10,
    "processing": 5,
    "completed": 100,
    "failed": 2,
    "deadLetter": 1,
    "total": 118
  },
  "partitions": [
    {
      "name": "Default",
      "stats": {
        "pending": 8,
        "processing": 3,
        "completed": 80,
        "failed": 1,
        "deadLetter": 0,
        "total": 92
      }
    },
    {
      "name": "urgent",
      "stats": {
        "pending": 2,
        "processing": 2,
        "completed": 20,
        "failed": 1,
        "deadLetter": 1,
        "total": 26
      }
    }
  ]
}
```

#### 5.2 All Queues Overview
**Endpoint:** `GET /api/v1/analytics/queues`

Gets statistics for all queues in the system.

**Query Parameters:**
- `namespace` (string): Filter by namespace
- `task` (string): Filter by task

**Response:**
```json
{
  "queues": [
    {
      "queue": "emails",
      "namespace": "production",
      "task": "notifications",
      "partitions": [...],
      "totals": {...}
    }
  ]
}
```

#### 5.3 Queue Depths
**Endpoint:** `GET /api/v1/analytics/queue-depths`

Gets pending message counts for all queues.

**Response:**
```json
{
  "depths": [
    {
      "queue": "emails",
      "depth": 10,        // Total pending
      "processing": 5,    // Total processing
      "partitions": [
        { "name": "Default", "depth": 8, "processing": 3 },
        { "name": "urgent", "depth": 2, "processing": 2 }
      ]
    }
  ]
}
```

#### 5.4 Throughput Metrics
**Endpoint:** `GET /api/v1/analytics/throughput`

Gets throughput metrics over the last hour (minute-by-minute).

**Response:**
```json
{
  "throughput": [
    {
      "timestamp": "2025-10-08T07:00:00.000Z",
      "incoming": {
        "messagesPerMinute": 120,
        "messagesPerSecond": 2
      },
      "completed": {
        "messagesPerMinute": 115,
        "messagesPerSecond": 1
      },
      "processing": {
        "messagesPerMinute": 100,
        "messagesPerSecond": 1
      },
      "failed": {
        "messagesPerMinute": 5,
        "messagesPerSecond": 0
      },
      "lag": {
        "avgSeconds": 2.5,
        "avgMilliseconds": 2500,
        "sampleCount": 115
      }
    }
    // ... 59 more entries
  ]
}
```

#### 5.5 Queue Lag Analysis
**Endpoint:** `GET /api/v1/analytics/queue-lag`

Gets queue lag metrics based on processing times and current backlog.

**Query Parameters:**
- `queue` (string): Filter by queue name
- `namespace` (string): Filter by namespace
- `task` (string): Filter by task

**Response:**
```json
{
  "queues": [
    {
      "queue": "emails",
      "namespace": "production",
      "task": "notifications",
      "partitions": [
        {
          "name": "Default",
          "stats": {
            "pendingCount": 150,
            "processingCount": 25,
            "totalBacklog": 175,
            "completedMessages": 1250,
            "avgProcessingTimeSeconds": 2.5,
            "medianProcessingTimeSeconds": 2.1,
            "p95ProcessingTimeSeconds": 4.8,
            "estimatedLagSeconds": 437.5,
            "medianLagSeconds": 367.5,
            "p95LagSeconds": 840.0,
            "estimatedLag": "7m 17s",
            "medianLag": "6m 7s",
            "p95Lag": "14m 0s",
            "avgProcessingTime": "2.5s",
            "medianProcessingTime": "2.1s",
            "p95ProcessingTime": "4.8s"
          }
        }
      ],
      "totals": {
        "pendingCount": 150,
        "processingCount": 25,
        "totalBacklog": 175,
        "completedMessages": 1250,
        "avgProcessingTimeSeconds": 2.5,
        "medianProcessingTimeSeconds": 2.1,
        "p95ProcessingTimeSeconds": 4.8,
        "estimatedLagSeconds": 437.5,
        "medianLagSeconds": 367.5,
        "p95LagSeconds": 840.0,
        "estimatedLag": "7m 17s",
        "medianLag": "6m 7s",
        "p95Lag": "14m 0s",
        "avgProcessingTime": "2.5s",
        "medianProcessingTime": "2.1s",
        "p95ProcessingTime": "4.8s"
      }
    }
  ]
}
```

**Lag Calculation:**
- **Estimated Lag**: `(pending + processing) × average_processing_time`
- **Median Lag**: `(pending + processing) × median_processing_time`
- **95th Percentile Lag**: `(pending + processing) × p95_processing_time`

**Notes:**
- Only includes queues with at least 5 completed messages in the last 24 hours
- Processing times are calculated from `completed_at - created_at` for completed messages
- Lag represents the estimated time for all current backlog to be processed

#### 5.6 Queue-Specific Stats
**Endpoint:** `GET /api/v1/analytics/queue-stats`

Gets statistics with flexible filtering.

**Query Parameters:**
- `queue` (string): Filter by queue name
- `namespace` (string): Filter by namespace
- `task` (string): Filter by task

**Example:**
```
GET /api/v1/analytics/queue-stats?queue=emails
GET /api/v1/analytics/queue-stats?namespace=production
```

#### 5.6 Filtered Analytics
**Endpoint:** `GET /api/v1/analytics`

Gets analytics based on namespace or task filters.

**Query Parameters:**
- `namespace` (string): Get all queues in namespace
- `task` (string): Get all queues with task

---

### 6. Message Management

#### 6.1 List Messages
**Endpoint:** `GET /api/v1/messages`

Lists messages with filtering options.

**Query Parameters:**
- `queue` (string): Filter by queue name
- `partition` (string): Filter by partition name
- `namespace` (string): Filter by namespace
- `task` (string): Filter by task
- `status` (string): Filter by status (pending, processing, completed, failed, dead_letter)
- `limit` (integer): Max results (default: 100)
- `offset` (integer): Pagination offset (default: 0)

**Response:**
```json
{
  "messages": [
    {
      "id": "message-uuid",
      "transactionId": "transaction-uuid",
      "queuePath": "emails/urgent",
      "queue": "emails",
      "partition": "urgent",
      "namespace": null,
      "task": null,
      "payload": {...},
      "status": "pending",
      "workerId": null,
      "createdAt": "2025-10-08T05:12:33.889Z",
      "lockedAt": null,
      "completedAt": null,
      "failedAt": null,
      "errorMessage": null,
      "retryCount": 0,
      "leaseExpiresAt": null
    }
  ]
}
```

#### 6.2 Get Single Message
**Endpoint:** `GET /api/v1/messages/:transactionId`

Gets details of a specific message.

**Response:** Single message object with partition options included

#### 6.3 Delete Message
**Endpoint:** `DELETE /api/v1/messages/:transactionId`

Permanently deletes a message.

**Response:**
```json
{
  "deleted": true,
  "transactionId": "transaction-uuid"
}
```

#### 6.4 Retry Failed Message
**Endpoint:** `POST /api/v1/messages/:transactionId/retry`

Resets a failed message to pending for retry.

**Response:**
```json
{
  "retried": true,
  "transactionId": "transaction-uuid"
}
```

#### 6.5 Move to Dead Letter Queue
**Endpoint:** `POST /api/v1/messages/:transactionId/dlq`

Moves a failed message to the dead letter queue.

**Response:**
```json
{
  "movedToDLQ": true,
  "transactionId": "transaction-uuid"
}
```

#### 6.6 Get Related Messages
**Endpoint:** `GET /api/v1/messages/:transactionId/related`

Gets messages from the same partition within 1 hour of the specified message.

**Response:**
```json
{
  "messages": [
    {
      "transactionId": "related-uuid",
      "status": "completed",
      "createdAt": "2025-10-08T05:10:00.000Z",
      "payload": {...}
    }
  ]
}
```

#### 6.7 Clear Queue
**Endpoint:** `DELETE /api/v1/queues/:queue/clear`

Deletes all messages from a queue or specific partition.

**Query Parameters:**
- `partition` (string): Clear specific partition only

**Response:**
```json
{
  "cleared": true,
  "count": 25,
  "queue": "emails",
  "partition": "all"  // or specific partition name
}
```

---

### 7. System Health & Metrics

#### 7.1 Health Check
**Endpoint:** `GET /health`

Checks system health and basic statistics.

**Response:**
```json
{
  "status": "healthy",
  "uptime": "3600s",
  "connections": 5,
  "stats": {
    "requests": 1000,
    "messages": 5000,
    "requestsPerSecond": "0.28",
    "messagesPerSecond": "1.39",
    "pool": {
      "total": 20,
      "idle": 15,
      "waiting": 0
    }
  }
}
```

#### 7.2 Detailed Metrics
**Endpoint:** `GET /metrics`

Gets detailed performance metrics.

**Response:**
```json
{
  "uptime": 3600,
  "requests": {
    "total": 1000,
    "rate": 0.28
  },
  "messages": {
    "total": 5000,
    "rate": 1.39
  },
  "database": {
    "poolSize": 20,
    "idleConnections": 15,
    "waitingRequests": 0
  },
  "memory": {
    "rss": 104857600,
    "heapTotal": 73728000,
    "heapUsed": 45678900,
    "external": 2345678,
    "arrayBuffers": 123456
  },
  "cpu": {
    "user": 1234567,
    "system": 234567
  }
}
```

---

## Resource Management

### 8. Resources API

These endpoints provide information about queues, partitions, and system structure for frontend displays.

#### 8.1 List All Queues
**Endpoint:** `GET /api/v1/resources/queues`

Gets all queues with summary information.

**Query Parameters:**
- `namespace` (string): Filter by namespace
- `task` (string): Filter by task

**Response:**
```json
{
  "queues": [
    {
      "id": "queue-uuid",
      "name": "emails",
      "namespace": null,
      "task": null,
      "createdAt": "2025-10-08T05:13:37.528Z",
      "partitions": 3,
      "messages": {
        "total": 100,
        "pending": 20,
        "processing": 5
      }
    }
  ]
}
```

#### 8.2 Get Queue Details
**Endpoint:** `GET /api/v1/resources/queues/:queue`

Gets detailed information about a specific queue including all partitions.

**Response:**
```json
{
  "id": "queue-uuid",
  "name": "emails",
  "namespace": null,
  "task": null,
  "createdAt": "2025-10-08T05:13:37.528Z",
  "partitions": [
    {
      "id": "partition-uuid",
      "name": "urgent",
      "priority": 10,
      "options": {
        "leaseTime": 300,
        "retryLimit": 3
      },
      "createdAt": "2025-10-08T05:13:37.541Z",
      "stats": {
        "total": 50,
        "pending": 10,
        "processing": 2,
        "completed": 35,
        "failed": 3,
        "deadLetter": 0
      },
      "oldestMessage": "2025-10-08T05:00:00.000Z",
      "newestMessage": "2025-10-08T07:30:00.000Z"
    }
  ],
  "totals": {
    "total": 100,
    "pending": 20,
    "processing": 5,
    "completed": 70,
    "failed": 5,
    "deadLetter": 0
  }
}
```

#### 8.3 List All Partitions
**Endpoint:** `GET /api/v1/resources/partitions`

Gets all partitions across all queues.

**Query Parameters:**
- `queue` (string): Filter by queue name
- `minDepth` (integer): Only show partitions with at least this many pending messages

**Response:**
```json
{
  "partitions": [
    {
      "id": "partition-uuid",
      "name": "urgent",
      "queue": "emails",
      "namespace": null,
      "task": null,
      "priority": 10,
      "options": { /* partition options */ },
      "createdAt": "2025-10-08T05:13:37.541Z",
      "depth": 10,
      "processing": 2,
      "total": 50
    }
  ]
}
```

#### 8.4 List Namespaces
**Endpoint:** `GET /api/v1/resources/namespaces`

Gets all namespaces with aggregated statistics.

**Response:**
```json
{
  "namespaces": [
    {
      "namespace": "production",
      "queues": 5,
      "partitions": 15,
      "messages": {
        "total": 1000,
        "pending": 200
      }
    }
  ]
}
```

#### 8.5 List Tasks
**Endpoint:** `GET /api/v1/resources/tasks`

Gets all tasks with aggregated statistics.

**Response:**
```json
{
  "tasks": [
    {
      "task": "notifications",
      "queues": 3,
      "partitions": 9,
      "messages": {
        "total": 500,
        "pending": 100
      }
    }
  ]
}
```

#### 8.6 System Overview
**Endpoint:** `GET /api/v1/resources/overview`

Gets a complete system overview with all key metrics.

**Response:**
```json
{
  "queues": 8,
  "partitions": 15,
  "namespaces": 2,
  "tasks": 3,
  "messages": {
    "total": 1000,
    "pending": 200,
    "processing": 50,
    "completed": 700,
    "failed": 40,
    "deadLetter": 10
  },
  "timestamp": "2025-10-08T07:29:35.682Z"
}
```

---

## WebSocket Support

### Dashboard WebSocket
**Endpoint:** `ws://localhost:6632/ws/dashboard`

Real-time updates for dashboard monitoring with V2 structure.

#### Connection
```javascript
const ws = new WebSocket('ws://localhost:6632/ws/dashboard');

ws.onopen = () => {
  // Send ping to keep alive
  setInterval(() => ws.send('ping'), 30000);
  
  // Optional: Subscribe to specific queues
  ws.send(JSON.stringify({
    type: 'subscribe',
    queues: ['emails', 'payments']
  }));
};
```

#### Events (Server → Client)

**Connection Events:**
```json
{
  "event": "connected",
  "data": {
    "connectionId": "uuid",
    "version": "v2"
  },
  "timestamp": "2025-10-08T07:30:00.000Z"
}
```

**Message Events:**
```json
// Message pushed
{
  "event": "message.pushed",
  "data": {
    "queue": "emails",
    "partition": "urgent",
    "transactionId": "uuid"
  },
  "timestamp": "2025-10-08T07:30:00.000Z"
}

// Message processing
{
  "event": "message.processing",
  "data": {
    "queue": "emails",
    "partition": "urgent",
    "transactionId": "uuid",
    "workerId": "worker-123"
  },
  "timestamp": "2025-10-08T07:30:00.000Z"
}

// Message completed
{
  "event": "message.completed",
  "data": {
    "transactionId": "uuid"
  },
  "timestamp": "2025-10-08T07:30:00.000Z"
}

// Message failed
{
  "event": "message.failed",
  "data": {
    "transactionId": "uuid",
    "error": "Processing error"
  },
  "timestamp": "2025-10-08T07:30:00.000Z"
}
```

**Queue Events:**
```json
// Queue created
{
  "event": "queue.created",
  "data": {
    "queue": "new-queue",
    "partition": "Default"
  },
  "timestamp": "2025-10-08T07:30:00.000Z"
}

// Queue depth update (every 5 seconds)
{
  "event": "queue.depth",
  "data": {
    "queue": "emails",
    "namespace": null,
    "task": null,
    "totalDepth": 25,
    "totalProcessing": 5,
    "partitions": {
      "Default": {
        "depth": 10,
        "processing": 2,
        "completed": 100,
        "failed": 5
      },
      "urgent": {
        "depth": 15,
        "processing": 3,
        "completed": 50,
        "failed": 2
      }
    }
  },
  "timestamp": "2025-10-08T07:30:00.000Z"
}

// Partition depth update (every 5 seconds)
{
  "event": "partition.depth",
  "data": {
    "queue": "emails",
    "partition": "urgent",
    "depth": 15,
    "processing": 3,
    "completed": 50,
    "failed": 2,
    "total": 70
  },
  "timestamp": "2025-10-08T07:30:00.000Z"
}
```

**System Events:**
```json
// System statistics (every 10 seconds)
{
  "event": "system.stats",
  "data": {
    "pending": 200,
    "processing": 50,
    "recentCreated": 120,  // Last minute
    "recentCompleted": 115, // Last minute
    "connections": 5        // WebSocket connections
  },
  "timestamp": "2025-10-08T07:30:00.000Z"
}

// Client connected
{
  "event": "client.connected",
  "data": {
    "clientId": "uuid"
  },
  "timestamp": "2025-10-08T07:30:00.000Z"
}

// Client disconnected
{
  "event": "client.disconnected",
  "data": {
    "clientId": "uuid"
  },
  "timestamp": "2025-10-08T07:30:00.000Z"
}
```

#### Client → Server Messages

**Keep Alive:**
```
ping
```
Server responds with: `pong`

**Subscribe to Queues:**
```json
{
  "type": "subscribe",
  "queues": ["emails", "payments", "notifications"]
}
```

**Note:** Subscription is optional and currently for future filtering implementation

---

## Error Responses

All endpoints may return error responses:

```json
{
  "error": "Error message description"
}
```

**Common Status Codes:**
- `200`: Success
- `201`: Created
- `204`: No Content (empty response)
- `400`: Bad Request (invalid parameters)
- `404`: Not Found
- `500`: Internal Server Error
- `503`: Service Unavailable (database connection issues)

---

## Performance Considerations

1. **Batch Operations**: Use batch push/pop for better throughput
2. **Long Polling**: Use `wait=true` to reduce polling overhead
3. **Partition Strategy**: Use multiple partitions for parallel processing
4. **Lease Time**: Set appropriate lease times based on processing duration
5. **Connection Pooling**: System supports up to 10,000+ messages/second with proper configuration

---

## Example Usage Flow

```javascript
// 1. Push a message
POST /api/v1/push
{
  "items": [{
    "queue": "orders",
    "partition": "high-priority",
    "payload": { "orderId": "12345", "amount": 99.99 }
  }]
}

// 2. Pop the message
GET /api/v1/pop/queue/orders/partition/high-priority

// 3. Process the message...

// 4. Acknowledge completion
POST /api/v1/ack
{
  "transactionId": "returned-transaction-id",
  "status": "completed"
}
```

---

## CORS Support

All endpoints include CORS headers:
- `Access-Control-Allow-Origin: *`
- `Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS`
- `Access-Control-Allow-Headers: Content-Type, Authorization`
- `Access-Control-Max-Age: 86400`
