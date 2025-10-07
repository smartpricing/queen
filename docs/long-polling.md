# Long Polling in Queen Client

## Overview

The Queen client implements efficient long polling for real-time message consumption. When configured with `wait: true`, the client will maintain a persistent connection to the server, waiting for messages to arrive.

## Continuous Polling Behavior

The client has been optimized for continuous long polling without unnecessary delays:

### Timeout Handling

When a long polling request times out (no messages received within the timeout period), the client **immediately** starts a new request without any delay. This ensures:

- **Zero message loss**: No gap between polling requests where messages could be missed
- **Minimal latency**: Messages are received as soon as they're available
- **Efficient resource usage**: No unnecessary waiting or reconnection overhead

### Implementation Details

```javascript
// The consume function handles timeouts gracefully
const stop = client.consume({
  ns: 'myapp',
  task: 'processing',
  queue: 'orders',
  handler: async (message) => {
    // Process message
  },
  options: {
    wait: true,         // Enable long polling
    timeout: 30000,     // 30 second timeout
    batch: 10,          // Get up to 10 messages at once
    stopOnError: false  // Continue on errors
  }
});
```

### How It Works

1. **Normal Operation**: When messages are available, they are immediately returned and processed
2. **Timeout Scenario**: If no messages arrive within the timeout period:
   - Server returns an empty response (not an error)
   - Client immediately starts a new long polling request
   - No delay or backoff is applied
3. **Error Handling**: Only actual errors (network issues, server errors) trigger retry delays

### Client-Server Timeout Coordination

The client automatically adds a buffer to its timeout when long polling:
- Server timeout: 30 seconds (configured value)
- Client timeout: 35 seconds (server timeout + 5 second buffer)

This prevents premature client-side timeouts and ensures the server has time to respond.

## Example Usage

### Basic Consumer

```javascript
import { createQueenClient } from '@queen/client';

const client = createQueenClient({
  baseUrl: 'http://localhost:6632'
});

// Start continuous consumer
const stop = client.consume({
  ns: 'production',
  task: 'orders',
  queue: 'processing',
  handler: async (message) => {
    console.log('Received:', message);
    // Process the message
  },
  options: {
    wait: true,      // Enable long polling
    timeout: 30000,  // 30 second timeout
    batch: 5         // Process up to 5 messages at once
  }
});

// Stop when needed
// stop();
```

### Advanced Consumer with Statistics

```javascript
const stats = { processed: 0, errors: 0 };

const stop = client.consume({
  ns: 'production',
  task: 'orders',
  queue: 'processing',
  handler: async (message) => {
    try {
      await processOrder(message.data);
      stats.processed++;
    } catch (error) {
      stats.errors++;
      throw error; // Re-throw to trigger failed acknowledgment
    }
  },
  options: {
    wait: true,
    timeout: 30000,
    batch: 10,
    stopOnError: false  // Continue even if individual messages fail
  }
});

// Monitor statistics
setInterval(() => {
  console.log(`Processed: ${stats.processed}, Errors: ${stats.errors}`);
}, 60000);
```

## Benefits

1. **Real-time Processing**: Messages are processed immediately upon arrival
2. **Efficient**: No wasted time between polling cycles
3. **Reliable**: Automatic reconnection on network issues
4. **Scalable**: Supports multiple concurrent consumers
5. **Simple**: No complex configuration required

## Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `wait` | `false` | Enable long polling |
| `timeout` | `30000` | Polling timeout in milliseconds |
| `batch` | `1` | Number of messages to retrieve per request |
| `stopOnError` | `false` | Stop consumer on handler errors |

## Best Practices

1. **Set Appropriate Timeouts**: Use 30-60 second timeouts for long polling
2. **Handle Errors Gracefully**: Implement proper error handling in your message handler
3. **Use Batching**: Process multiple messages per request for better throughput
4. **Monitor Performance**: Track message processing rates and errors
5. **Graceful Shutdown**: Always call the stop function when shutting down

## Comparison with Traditional Polling

### Traditional Polling (with delays)
```
Request -> Response -> Wait 5s -> Request -> Response -> Wait 5s -> ...
```
- Messages may wait up to 5 seconds before being processed
- Unnecessary requests when no messages are available

### Long Polling (Queen implementation)
```
Request -> Wait for message/timeout -> Response -> Immediately Request -> ...
```
- Messages processed immediately upon arrival
- No delays between requests
- Efficient use of resources
