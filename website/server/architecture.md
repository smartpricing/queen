# Server Architecture

Queen uses a high-performance **acceptor/worker pattern** with fully asynchronous, non-blocking PostgreSQL architecture.

## Core Components

### Network Layer
- **Acceptor**: Single thread on port 6632, round-robin distribution
- **Workers**: 10 event loop threads (configurable via `NUM_WORKERS`)
- **WebSocket**: Real-time streaming support

### Database Layer
- **AsyncDbPool**: 142 non-blocking PostgreSQL connections
- **Non-blocking I/O**: Socket-based with `select()`
- **RAII Management**: Automatic connection cleanup

### Background Services
- **Poll Workers**: 4 threads for long-polling
- **Background Pool**: 8 connections for metrics/retention

## Request Flow

```
Client → Acceptor → Worker → AsyncQueueManager → AsyncDbPool → PostgreSQL
```

## Performance

- **Latency**: 10-50ms (POP/ACK), 50-200ms (TRANSACTION)
- **Throughput**: 148,000+ msg/s peak, 130,000+ sustained
- **Resources**: 14 threads, 150 DB connections

![Architecture](/architecture.svg)

[Complete architecture documentation](https://github.com/smartpricing/queen/blob/master/documentation/ARCHITECTURE.md)
