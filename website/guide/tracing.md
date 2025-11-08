# Message Tracing

End-to-end tracing across queues and workflows for debugging distributed systems.

## What is Message Tracing?

Track messages as they flow through multiple queues and processing stages.

## Automatic Tracing

All messages are automatically traced:

```javascript
// Push with trace
await queen.queue('orders').push([{
  data: { orderId: 123 },
  traceId: 'trace-123'  // Optional: provide your own
}])

// Queen automatically creates trace entries
```

## Viewing Traces

### In Dashboard

Visit `http://localhost:6632` and navigate to "Traces" to see visual timelines.

![Traces](/traces.png)

### Via API

```javascript
// Get trace for a message
const trace = await queen.getTrace('transaction-id-123')

console.log(trace)
// {
//   traceId: 'trace-123',
//   messages: [
//     { queue: 'orders', timestamp: '...', event: 'pushed' },
//     { queue: 'orders', timestamp: '...', event: 'popped' },
//     { queue: 'fulfillment', timestamp: '...', event: 'pushed' },
//     { queue: 'fulfillment', timestamp: '...', event: 'completed' }
//   ]
// }
```

## Distributed Tracing

Trace messages across multiple queues:

```javascript
// Stage 1
await queen.queue('stage1').push([{
  traceId: workflowId,
  data: { step: 1 }
}])

// Stage 2 (same trace ID)
await queen.transaction()
  .ack(stage1Message)
  .queue('stage2')
  .push([{
    traceId: stage1Message.traceId,  // Continue trace
    data: { step: 2 }
  }])
  .commit()

// Now view complete workflow in traces dashboard!
```

## Use Cases

1. **Debug workflows** - See where messages get stuck
2. **Performance analysis** - Identify bottlenecks
3. **Audit trails** - Track message lifecycle
4. **Error investigation** - Find where failures occur

## Best Practices

1. **Use meaningful trace IDs** - business IDs, workflow IDs
2. **Propagate trace IDs** across services
3. **Review traces regularly** - find optimization opportunities
4. **Set up alerts** for trace anomalies

## Related

- [Transactions](/guide/transactions)
- [Dead Letter Queue](/guide/dlq)
- [Web Dashboard](/webapp/overview)

[Complete documentation](https://github.com/smartpricing/queen)
