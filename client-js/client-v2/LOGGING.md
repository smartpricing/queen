# Queen Client V2 - Logging Documentation

## Overview

The Queen Client V2 includes comprehensive operation logging that captures every significant action performed by the client. Logging is **disabled by default** and can be enabled via the `QUEEN_CLIENT_LOG` environment variable.

## Enabling Logging

```bash
export QUEEN_CLIENT_LOG=true
node your-app.js
```

Or inline:
```bash
QUEEN_CLIENT_LOG=true node your-app.js
```

## Log Format

All logs follow this format:
```
[TIMESTAMP] [LEVEL] [OPERATION] DETAILS
```

- **TIMESTAMP**: ISO 8601 format (e.g., `2025-10-28T10:30:45.123Z`)
- **LEVEL**: `INFO`, `WARN`, or `ERROR`
- **OPERATION**: Component and method (e.g., `Queen.push`, `HttpClient.request`)
- **DETAILS**: JSON object with relevant context

### Example Log Output

```
[2025-10-28T10:30:45.123Z] [INFO] [Queen.constructor] {"status":"initialized","urls":1}
[2025-10-28T10:30:45.234Z] [INFO] [QueueBuilder.push] {"queue":"tasks","partition":"Default","count":5,"buffered":true}
[2025-10-28T10:30:45.456Z] [INFO] [BufferManager.addMessage] {"queueAddress":"tasks/Default","messageCount":5}
[2025-10-28T10:30:46.789Z] [INFO] [HttpClient.request] {"method":"POST","url":"http://localhost:6632/api/v1/push","hasBody":true,"timeout":30000}
[2025-10-28T10:30:46.890Z] [INFO] [HttpClient.response] {"method":"POST","url":"http://localhost:6632/api/v1/push","status":200}
[2025-10-28T10:30:47.123Z] [INFO] [BufferManager.flushBuffer] {"queueAddress":"tasks/Default","status":"success","messagesSent":5}
```

## Logged Operations

### Queen (Main Client)

| Operation | Logged Details |
|-----------|---------------|
| `Queen.constructor` | Configuration summary, URL count |
| `Queen.ack` | Batch/single, message count, status, context |
| `Queen.renew` | Lease ID count, success/failure per lease |
| `Queen.flushAllBuffers` | Start and completion status |
| `Queen.getBufferStats` | Buffer statistics |
| `Queen.close` | Shutdown phases, errors |

### QueueBuilder (Queue Operations)

| Operation | Logged Details |
|-----------|---------------|
| `QueueBuilder.create` | Queue name, namespace, task |
| `QueueBuilder.delete` | Queue name |
| `QueueBuilder.push` | Queue, partition, count, buffered flag |
| `QueueBuilder.pop` | Queue, partition, batch size, wait mode, result count |
| `QueueBuilder.flushBuffer` | Queue address |
| `QueueBuilder.dlq` | Queue, consumer group, partition |

### HttpClient (Network Operations)

| Operation | Logged Details |
|-----------|---------------|
| `HttpClient.constructor` | Configuration (timeout, retries, failover) |
| `HttpClient.request` | Method, URL, body presence, timeout |
| `HttpClient.response` | Method, URL, HTTP status |
| `HttpClient.retry` | Attempt number, delay, error message |
| `HttpClient.failover` | Server count, attempted URLs, failures |

### BufferManager (Client-Side Buffering)

| Operation | Logged Details |
|-----------|---------------|
| `BufferManager.createBuffer` | Queue address, buffer options |
| `BufferManager.addMessage` | Queue address, current message count |
| `BufferManager.flushBuffer` | Queue address, message count, status |
| `BufferManager.flushAllBuffers` | Buffer count, pending flushes |
| `BufferManager.getStats` | Active buffers, total messages, oldest age |
| `BufferManager.cleanup` | Buffer count being cleaned |

### ConsumerManager (Message Consumption)

| Operation | Logged Details |
|-----------|---------------|
| `ConsumerManager.start` | Queue, concurrency, batch size, auto-ack |
| `ConsumerManager.worker` | Worker ID, lifecycle events, processed count |
| `ConsumerManager.processMessage` | Transaction ID, ack/nack status, errors |
| `ConsumerManager.processBatch` | Message count, ack/nack status, errors |

### TransactionBuilder (Atomic Operations)

| Operation | Logged Details |
|-----------|---------------|
| `TransactionBuilder.ack` | Message count, status |
| `TransactionBuilder.queue.push` | Queue name, item count |
| `TransactionBuilder.commit` | Operation count, required leases, success/failure |

### Other Builders

| Operation | Logged Details |
|-----------|---------------|
| `OperationBuilder.execute` | Method, path, success/failure |
| `PushBuilder.execute` | Queue, partition, count, buffered flag, results |
| `DLQBuilder.get` | Queue, consumer group, limit, offset, results |

## Use Cases

### 1. Debugging

Use logging to trace through client operations and identify where issues occur:

```bash
QUEEN_CLIENT_LOG=true node my-app.js 2>&1 | grep ERROR
```

### 2. Performance Analysis

Monitor HTTP request/response times and buffer flush patterns:

```bash
QUEEN_CLIENT_LOG=true node my-app.js 2>&1 | grep HttpClient
```

### 3. Audit Trail

Create an audit log of all queue operations:

```bash
QUEEN_CLIENT_LOG=true node my-app.js 2>&1 | tee audit.log
```

### 4. Development

Enable logging during development to understand client behavior:

```javascript
// In .env file
QUEEN_CLIENT_LOG=true
```

### 5. Production Troubleshooting

Temporarily enable logging in production to diagnose issues:

```bash
# Enable for one process
QUEEN_CLIENT_LOG=true pm2 restart my-app --update-env

# Disable after troubleshooting
pm2 restart my-app
```

## Performance Impact

- **Disabled (default)**: Zero performance impact - all logging calls are no-ops
- **Enabled**: Minimal impact - logging is asynchronous and uses `console.log/warn/error`

## Log Levels

- **INFO**: Normal operations (most logs)
- **WARN**: Recoverable issues (retries, failover, network errors)
- **ERROR**: Operation failures (push failed, ack failed, etc.)

## Filtering Logs

### By Component
```bash
QUEEN_CLIENT_LOG=true node app.js 2>&1 | grep "HttpClient"
QUEEN_CLIENT_LOG=true node app.js 2>&1 | grep "BufferManager"
QUEEN_CLIENT_LOG=true node app.js 2>&1 | grep "ConsumerManager"
```

### By Level
```bash
QUEEN_CLIENT_LOG=true node app.js 2>&1 | grep "ERROR"
QUEEN_CLIENT_LOG=true node app.js 2>&1 | grep "WARN"
```

### By Operation
```bash
QUEEN_CLIENT_LOG=true node app.js 2>&1 | grep "push"
QUEEN_CLIENT_LOG=true node app.js 2>&1 | grep "ack"
QUEEN_CLIENT_LOG=true node app.js 2>&1 | grep "pop"
```

## Integration with Log Aggregation

The structured JSON format makes it easy to integrate with log aggregation tools:

### Winston
```javascript
import winston from 'winston'

// Redirect console.log to Winston
console.log = winston.info
console.error = winston.error
console.warn = winston.warn
```

### Pino
```javascript
import pino from 'pino'
const logger = pino()

console.log = (msg) => logger.info(msg)
console.error = (msg) => logger.error(msg)
console.warn = (msg) => logger.warn(msg)
```

### Datadog, CloudWatch, etc.

The ISO 8601 timestamps and JSON format are compatible with most log aggregation services.

## Best Practices

1. **Disable in Production**: Only enable when needed for troubleshooting
2. **Use Log Rotation**: If keeping logs enabled, use log rotation to manage disk space
3. **Filter Sensitive Data**: Payloads are NOT logged - only metadata
4. **Monitor Log Volume**: High-throughput applications generate many logs when enabled
5. **Use Structured Search**: Leverage JSON format for precise filtering

## Implementation Details

The logging system is implemented in `utils/logger.js` and imported by all major components:

- `Queen.js` - Main client operations
- `HttpClient.js` - Network layer
- `BufferManager.js` - Client-side buffering
- `ConsumerManager.js` - Message consumption
- `QueueBuilder.js` - Queue operations
- `TransactionBuilder.js` - Atomic transactions

All logging calls check the `QUEEN_CLIENT_LOG` environment variable and are no-ops when disabled, ensuring zero performance impact by default.

