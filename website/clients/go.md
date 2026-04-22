# Go Client

Native Go client library for Queen MQ with a fluent builder-pattern API.

## Installation

```bash
go get github.com/smartpricing/queen/client-go
```

**Requirements:**
- Go 1.21 or higher

## Quick Start

```go
package main

import (
    "context"
    "fmt"
    "log"

    queen "github.com/smartpricing/queen/client-go"
)

func main() {
    // Connect
    client, err := queen.New("http://localhost:6632")
    if err != nil {
        log.Fatal(err)
    }
    defer client.Close(context.Background())

    ctx := context.Background()

    // Create a queue
    client.Queue("tasks").Create().Execute(ctx)

    // Push a message
    client.Queue("tasks").Push(map[string]interface{}{
        "orderId": 123,
        "amount":  99.99,
    }).Execute(ctx)

    // Consume messages
    client.Queue("tasks").
        Group("workers").
        Consume(ctx, func(ctx context.Context, msg *queen.Message) error {
            fmt.Printf("Processing: %v\n", msg.Data)
            return nil  // auto-ack on success
        }).Execute(ctx)
}
```

## Features

- **Fluent API** — intuitive builder pattern for all operations
- **Load Balancing** — round-robin, session, or affinity-based strategies
- **Failover** — automatic failover with per-server health tracking
- **Client-side Buffering** — batch messages for improved throughput
- **Consumer Groups** — Kafka-style pub/sub with independent cursors
- **Transactions** — atomic ack + push across multiple queues
- **Automatic Lease Renewal** — for long-running handlers
- **Tracing** — built-in message trace events
- **Context-aware** — every call respects `context.Context` cancellation

## Configuration

### Single server

```go
client, _ := queen.New("http://localhost:6632")
```

### Multi-server cluster (HA + load balancing)

```go
client, _ := queen.New(queen.ClientConfig{
    URLs: []string{
        "http://server1:6632",
        "http://server2:6632",
        "http://server3:6632",
    },
    LoadBalancingStrategy: "affinity",  // or "round-robin", "session"
    EnableFailover:        true,
    TimeoutMillis:         30000,
    RetryAttempts:         3,
    BearerToken:           "your-token",  // for proxy authentication
})
```

## Queue Operations

### Create/Configure Queue

```go
// Default settings
client.Queue("my-queue").Create().Execute(ctx)

// Custom configuration
client.Queue("my-queue").Config(queen.QueueConfig{
    LeaseTime:         60,    // seconds before redelivery
    RetryLimit:        3,     // retries before DLQ
    Priority:          10,    // higher = processed first
    MaxSize:           1000,  // max messages
    EncryptionEnabled: true,
}).Create().Execute(ctx)
```

### Push

```go
// Simple push
client.Queue("my-queue").Push(map[string]interface{}{
    "data": "value",
}).Execute(ctx)

// To a specific partition (affinity / ordering guarantee)
client.Queue("my-queue").Partition("user-123").Push(data).Execute(ctx)

// Batch push
client.Queue("my-queue").Push([]interface{}{msg1, msg2, msg3}).Execute(ctx)

// Client-side buffered push (batches small pushes into fewer HTTP calls)
client.Queue("my-queue").Buffer(queen.BufferConfig{
    MessageCount: 100,
    TimeMillis:   1000,
}).Push(data).Execute(ctx)
```

### Consume (long-running worker)

```go
client.Queue("my-queue").
    Group("workers").            // consumer group
    Concurrency(5).              // 5 parallel in-flight handlers
    Batch(10).                   // 10 messages per poll
    AutoAck(true).               // auto-ack on handler success
    RenewLease(true, 10000).     // renew lease every 10s
    Consume(ctx, func(ctx context.Context, msg *queen.Message) error {
        // Process message; return non-nil to nack + retry
        return processOrder(msg.Data)
    }).Execute(ctx)
```

### Pop (one-shot)

```go
// Pop a single message
messages, _ := client.Queue("my-queue").Pop(ctx)

// Long-poll pop with timeout
messages, _ := client.Queue("my-queue").
    Wait(true).
    TimeoutMillis(30000).
    Pop(ctx)

// Batch pop with consumer group
messages, _ := client.Queue("my-queue").
    Group("my-group").
    Batch(10).
    Pop(ctx)
```

### Ack / Nack

```go
// Ack single
client.Ack(ctx, msg, true, queen.AckOptions{})

// Nack (retry or route to DLQ after retry limit)
client.Ack(ctx, msg, false, queen.AckOptions{Error: "Processing failed"})

// Batch ack with consumer-group context
client.Ack(ctx, messages, true, queen.AckOptions{ConsumerGroup: "my-group"})
```

## Transactions

Atomic "process this message, produce N new messages, mark it done" — all or nothing:

```go
result, err := client.Transaction().
    Ack(inputMsg, queen.AckStatusCompleted, queen.AckOptions{}).
    Queue("output").Push(processedData).
    Queue("audit-log").Push(auditEntry).
    Commit(ctx)

if result.Success {
    fmt.Println("Atomic commit succeeded")
}
```

## Dead Letter Queue

```go
response, _ := client.Queue("my-queue").
    DLQ("my-group").
    Limit(100).
    From("2024-01-01T00:00:00Z").
    To("2024-01-02T00:00:00Z").
    Get(ctx)

for _, msg := range response.Messages {
    fmt.Printf("DLQ: %v\n", msg.Data)
}
```

## Admin API

```go
admin := client.Admin()

health, _   := admin.Health(ctx)
queues, _   := admin.ListQueues(ctx, queen.ListQueuesParams{})
queue, _    := admin.GetQueue(ctx, "my-queue")
groups, _   := admin.ListConsumerGroups(ctx)
metrics, _  := admin.GetSystemMetrics(ctx, "", "")
```

## Logging

Enable debug logging via environment variable:

```bash
QUEEN_CLIENT_LOG=debug ./myapp
```

Levels: `debug`, `info`, `warn`, `error`.

## Default Values

| Setting              | Default                |
|----------------------|------------------------|
| Timeout              | 30 seconds             |
| Retry attempts       | 3                      |
| Retry delay          | 1 second (exponential) |
| Load balancing       | `affinity`             |
| Consumer concurrency | 1                      |
| Consumer batch size  | 1                      |
| Auto-ack             | `true`                 |
| Long-poll (consume)  | `true`                 |
| Buffer count         | 100                    |
| Buffer time          | 1 second               |

## Complete Documentation

- [Go Client README](https://github.com/smartpricing/queen/blob/master/client-go/README.md)
- [Source on GitHub](https://github.com/smartpricing/queen/tree/master/client-go)

## Support

[GitHub Repository](https://github.com/smartpricing/queen)
