# Dashboard API Quick Start Guide

## Overview

The Queen Dashboard API provides 5 comprehensive endpoints for monitoring and managing your message queue system.

## Endpoints Summary

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/v1/status` | GET | Dashboard overview with throughput, queues, and message counts |
| `/api/v1/status/queues` | GET | List all queues with statistics and filtering |
| `/api/v1/status/queues/:queue` | GET | Detailed queue information with partition breakdown |
| `/api/v1/status/queues/:queue/messages` | GET | Browse messages in a queue with filtering |
| `/api/v1/status/analytics` | GET | Advanced analytics with time-series data |

## Quick Examples

### 1. Get Dashboard Status

```bash
# Default (last hour)
curl "http://localhost:3000/api/v1/status"

# Filter by namespace
curl "http://localhost:3000/api/v1/status?namespace=production"

# Custom time range
curl "http://localhost:3000/api/v1/status?from=2025-10-14T10:00:00Z&to=2025-10-14T11:00:00Z"
```

**Response includes:**
- Throughput per minute (ingested/processed)
- Active queues list
- Message counts (total, pending, processing, completed, failed, DLQ)
- Active lease information
- Top DLQ errors

### 2. List Queues

```bash
# List all queues
curl "http://localhost:3000/api/v1/status/queues"

# Filter by namespace with pagination
curl "http://localhost:3000/api/v1/status/queues?namespace=production&limit=20&offset=0"

# Filter by task
curl "http://localhost:3000/api/v1/status/queues?task=analytics"
```

**Response includes:**
- Queue metadata (name, namespace, task, priority)
- Message statistics per queue
- Lag information
- Average processing times
- Pagination info

### 3. Get Queue Details

```bash
# Get detailed queue information
curl "http://localhost:3000/api/v1/status/queues/user-events"

# With time range
curl "http://localhost:3000/api/v1/status/queues/user-events?from=2025-10-14T09:00:00Z"
```

**Response includes:**
- Queue configuration
- Partition-level breakdown
- Per-partition message counts
- Cursor positions (consumption progress)
- Active leases per partition
- Lag per partition

### 4. Browse Queue Messages

```bash
# List messages in queue
curl "http://localhost:3000/api/v1/status/queues/user-events/messages"

# Filter by status
curl "http://localhost:3000/api/v1/status/queues/user-events/messages?status=pending"

# Filter by partition
curl "http://localhost:3000/api/v1/status/queues/user-events/messages?partition=partition-0"

# Multiple filters with pagination
curl "http://localhost:3000/api/v1/status/queues/user-events/messages?status=failed&limit=20&offset=0"
```

**Response includes:**
- Message details (transaction ID, trace ID, payload)
- Message status and timestamps
- Processing times
- Worker IDs
- Error messages (for failed messages)
- Age/lag information

### 5. Get Analytics

```bash
# Analytics for last 24 hours (hourly)
curl "http://localhost:3000/api/v1/status/analytics"

# Minute-level analytics for last hour
curl "http://localhost:3000/api/v1/status/analytics?interval=minute"

# Filter by namespace
curl "http://localhost:3000/api/v1/status/analytics?namespace=production"

# Custom time range with day interval
curl "http://localhost:3000/api/v1/status/analytics?from=2025-10-01T00:00:00Z&to=2025-10-14T23:59:59Z&interval=day"
```

**Response includes:**
- Throughput time series (ingested, processed, failed)
- Latency percentiles (p50, p95, p99) over time
- Error rates over time
- Current queue depths
- Top queues by volume
- DLQ trends and top errors

## Common Query Parameters

### Time Filtering
- `from` - ISO 8601 timestamp (default: 1 hour ago for most, 24 hours for analytics)
- `to` - ISO 8601 timestamp (default: now)

### Resource Filtering
- `queue` - Filter by specific queue name
- `namespace` - Filter by namespace
- `task` - Filter by task

### Pagination
- `limit` - Number of results (default: 50 for messages, 100 for queues)
- `offset` - Pagination offset (default: 0)

### Message Filtering
- `status` - Filter by status: `pending`, `processing`, `completed`, `failed`
- `partition` - Filter by partition name

### Analytics Options
- `interval` - Time bucket size: `minute`, `hour`, `day` (default: `hour`)

## Testing

Run the test script to verify all endpoints:

```bash
# Start the Queen server first
cd /Users/alice/Work/queen
nvm use 22 && node src/server.js

# In another terminal, run the test script
nvm use 22 && node examples/test-dashboard-api.js
```

## Integration Examples

### JavaScript/Node.js

```javascript
// Using fetch
async function getDashboardStatus() {
  const response = await fetch('http://localhost:3000/api/v1/status', {
    headers: { 'Content-Type': 'application/json' }
  });
  return await response.json();
}

// With filters
async function getQueuesByNamespace(namespace) {
  const url = new URL('http://localhost:3000/api/v1/status/queues');
  url.searchParams.append('namespace', namespace);
  
  const response = await fetch(url);
  return await response.json();
}

// Analytics with time range
async function getAnalytics(from, to) {
  const url = new URL('http://localhost:3000/api/v1/status/analytics');
  url.searchParams.append('from', from.toISOString());
  url.searchParams.append('to', to.toISOString());
  url.searchParams.append('interval', 'minute');
  
  const response = await fetch(url);
  return await response.json();
}
```

### React/Vue Dashboard Example

```javascript
import { useState, useEffect } from 'react';

function Dashboard() {
  const [status, setStatus] = useState(null);
  const [loading, setLoading] = useState(true);
  
  useEffect(() => {
    // Fetch status every 5 seconds
    const fetchStatus = async () => {
      try {
        const response = await fetch('http://localhost:3000/api/v1/status');
        const data = await response.json();
        setStatus(data);
        setLoading(false);
      } catch (error) {
        console.error('Failed to fetch status:', error);
      }
    };
    
    fetchStatus();
    const interval = setInterval(fetchStatus, 5000);
    
    return () => clearInterval(interval);
  }, []);
  
  if (loading) return <div>Loading...</div>;
  
  return (
    <div>
      <h1>Queen Dashboard</h1>
      <div className="stats">
        <div>Total Messages: {status.messages.total}</div>
        <div>Pending: {status.messages.pending}</div>
        <div>Processing: {status.messages.processing}</div>
        <div>Completed: {status.messages.completed}</div>
        <div>Failed: {status.messages.failed}</div>
      </div>
      <div className="queues">
        <h2>Active Queues ({status.queues.length})</h2>
        {status.queues.map(queue => (
          <div key={queue.id}>
            {queue.name} - {queue.partitions} partitions
          </div>
        ))}
      </div>
    </div>
  );
}
```

### Python Example

```python
import requests
from datetime import datetime, timedelta

BASE_URL = "http://localhost:3000"

def get_dashboard_status(namespace=None):
    params = {}
    if namespace:
        params['namespace'] = namespace
    
    response = requests.get(f"{BASE_URL}/api/v1/status", params=params)
    return response.json()

def get_queue_detail(queue_name):
    response = requests.get(f"{BASE_URL}/api/v1/status/queues/{queue_name}")
    return response.json()

def get_analytics(hours_back=1):
    now = datetime.now()
    from_time = now - timedelta(hours=hours_back)
    
    params = {
        'from': from_time.isoformat() + 'Z',
        'to': now.isoformat() + 'Z',
        'interval': 'minute'
    }
    
    response = requests.get(f"{BASE_URL}/api/v1/status/analytics", params=params)
    return response.json()

# Usage
status = get_dashboard_status(namespace='production')
print(f"Total messages: {status['messages']['total']}")
print(f"Active queues: {len(status['queues'])}")

analytics = get_analytics(hours_back=2)
print(f"Throughput totals: {analytics['throughput']['totals']}")
```

## Performance Notes

1. **Caching**: Consider implementing a 30-second cache for dashboard status to reduce database load
2. **Pagination**: Always use pagination for message listing endpoints
3. **Time Ranges**: Avoid very large time ranges (>7 days) for analytics to maintain performance
4. **Indexes**: The implementation uses existing database indexes for optimal query performance

## Error Handling

All endpoints return consistent error responses:

```json
{
  "error": "Queue not found"
}
```

Common HTTP status codes:
- `200 OK` - Success
- `400 Bad Request` - Invalid parameters
- `404 Not Found` - Resource not found
- `500 Internal Server Error` - Server error

## Next Steps

1. Review the full API documentation in `DASH.md`
2. Test the endpoints with `examples/test-dashboard-api.js`
3. Implement the dashboard UI using these endpoints
4. Set up monitoring and alerting based on the metrics
5. Consider implementing WebSocket support for real-time updates

## Support

For issues or questions:
- Review the full documentation in `DASH.md`
- Check the example scripts in `examples/`
- Examine the implementation in `src/routes/status.js`

