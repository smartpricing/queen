# Monitoring

Monitor Queen MQ health and performance.

## Health Check

```bash
curl http://localhost:6632/health
```

## Metrics

```bash
curl http://localhost:6632/metrics
```

**Response:**
```json
{
  "throughput": {
    "messages_per_second": 5234,
    "requests_per_second": 1523
  },
  "queues": {
    "total": 15,
    "active": 12
  },
  "connections": {
    "active": 45,
    "idle": 97
  }
}
```

## Dashboard

Visit `http://localhost:6632` for real-time monitoring.

## Alerts

```javascript
// Monitor queue depth
const depth = await queen.getQueueDepth('tasks')
if (depth > 10000) {
  alert('High queue depth!')
}

// Monitor DLQ
const dlqCount = await queen.getDLQCount('tasks')
if (dlqCount > 100) {
  alert('Many failed messages!')
}
```

[Web Dashboard](/webapp/overview)
