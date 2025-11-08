# HTTP API

Complete REST API reference for Queen MQ.

## Base URL

```
http://localhost:6632/api/v1
```

## Authentication

No authentication required by default. Use [proxy](/proxy/overview) for auth.

## Queue Configuration

### POST `/configure`

Create or configure a queue.

```bash
curl -X POST http://localhost:6632/api/v1/configure \
  -H "Content-Type: application/json" \
  -d '{
    "queue": "orders",
    "options": {
      "leaseTime": 60,
      "retryLimit": 3,
      "priority": 5
    }
  }'
```

## Messages

### POST `/push`

Push messages to a queue.

```bash
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{
    "items": [{
      "queue": "orders",
      "partition": "customer-123",
      "payload": {"orderId": 123}
    }]
  }'
```

### GET `/pop/queue/:queue`

Pop messages from a queue.

```bash
curl "http://localhost:6632/api/v1/pop/queue/orders?batch=10&wait=true&timeout=30000"
```

### POST `/ack`

Acknowledge a message.

```bash
curl -X POST http://localhost:6632/api/v1/ack \
  -H "Content-Type: application/json" \
  -d '{
    "transactionId": "msg-123",
    "partitionId": "part-uuid",
    "status": "completed"
  }'
```

## Transactions

### POST `/transaction`

Execute atomic transaction.

```bash
curl -X POST http://localhost:6632/api/v1/transaction \
  -H "Content-Type: application/json" \
  -d '{
    "operations": [
      {"type": "ack", "transactionId": "msg-1", "partitionId": "part-1"},
      {"type": "push", "items": [{"queue": "next", "payload": {...}}]}
    ]
  }'
```

## Resources

### GET `/resources/queues`

List all queues.

```bash
curl http://localhost:6632/api/v1/resources/queues
```

### GET `/resources/queues/:queue`

Get queue details.

```bash
curl http://localhost:6632/api/v1/resources/queues/orders
```

## Health

### GET `/health`

Health check.

```bash
curl http://localhost:6632/health
```

### GET `/metrics`

Performance metrics.

```bash
curl http://localhost:6632/metrics
```

[Complete API docs](https://github.com/smartpricing/queen/blob/master/server/API.md)
