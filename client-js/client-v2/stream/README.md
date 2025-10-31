# Queen Streaming API - Phase 1

Declarative stream processing for Queen MQ, inspired by Kafka Streams.

## Overview

The streaming API allows you to build data processing pipelines using a fluent, declarative syntax. All processing happens **server-side** using SQL, providing:

- **Security**: No SQL injection - safe predicate building
- **Performance**: PostgreSQL-optimized queries
- **Familiarity**: Kafka Streams-like API
- **Power**: Full SQL capabilities under the hood

## Installation

The streaming API is built into Queen client v2:

```javascript
import { Queen } from 'queen-mq'

const queen = new Queen('http://localhost:6632')
const stream = queen.stream('events@analytics')
```

## Basic Operations

### Filter

Filter messages based on conditions:

```javascript
// Object syntax (recommended)
const stream = queen
  .stream('events@processor')
  .filter({
    'payload.amount': { $gt: 1000 },
    'payload.status': 'completed'
  })

// Operators: $eq, $ne, $gt, $gte, $lt, $lte, $in, $nin, $contains
```

### Map

Transform message structure:

```javascript
const stream = queen
  .stream('events@processor')
  .map({
    userId: 'payload.userId',
    amount: 'payload.amount',
    timestamp: 'created_at'
  })
```

### GroupBy

Group messages by key(s):

```javascript
// Single key
const grouped = queen
  .stream('events@processor')
  .groupBy('payload.userId')

// Multiple keys
const grouped = queen
  .stream('events@processor')
  .groupBy(['payload.userId', 'payload.country'])
```

### Aggregations

After grouping, aggregate the groups:

```javascript
// Count
const counts = await queen
  .stream('events@processor')
  .groupBy('payload.userId')
  .count()
  .execute()

// Sum
const totals = await queen
  .stream('events@processor')
  .groupBy('payload.userId')
  .sum('payload.amount')
  .execute()

// Multiple aggregations
const stats = await queen
  .stream('events@processor')
  .groupBy('payload.userId')
  .aggregate({
    count: { $count: '*' },
    total: { $sum: 'payload.amount' },
    avg: { $avg: 'payload.amount' },
    min: { $min: 'payload.amount' },
    max: { $max: 'payload.amount' }
  })
  .execute()
```

## Execution Modes

### Immediate Execution

Execute once and return results:

```javascript
const result = await queen
  .stream('events@processor')
  .filter({ 'payload.status': 'completed' })
  .execute()

console.log(result.messages)
```

### Streaming Iteration

Process results as they arrive:

```javascript
for await (const message of queen.stream('events@processor')) {
  console.log(message)
}
```

### Collect Results

Collect all messages into an array:

```javascript
const messages = await queen
  .stream('events@processor')
  .filter({ 'payload.amount': { $gt: 1000 } })
  .take(100)  // First 100 messages
```

## Complete Example

```javascript
import { Queen } from 'queen-mq'

const queen = new Queen('http://localhost:6632')

// Push events
await queen.queue('events').push([
  { userId: 'alice', action: 'purchase', amount: 150 },
  { userId: 'bob', action: 'purchase', amount: 200 },
  { userId: 'alice', action: 'purchase', amount: 75 }
])

// Build pipeline
const pipeline = await queen
  .stream('events@analytics')
  .filter({ 'payload.action': 'purchase' })
  .map({
    user: 'payload.userId',
    amount: 'payload.amount'
  })
  .groupBy('user')
  .aggregate({
    purchases: { $count: '*' },
    revenue: { $sum: 'amount' },
    avgOrder: { $avg: 'amount' }
  })
  .execute()

console.log(pipeline.messages)
// [
//   { user: 'alice', purchases: 2, revenue: 225, avgOrder: 112.5 },
//   { user: 'bob', purchases: 1, revenue: 200, avgOrder: 200 }
// ]
```

## Chaining Operations

Operations can be chained fluently:

```javascript
const result = await queen
  .stream('events@processor')
  .filter({ 'payload.type': 'order' })              // Filter orders
  .filter({ 'payload.amount': { $gt: 100 } })       // High value only
  .map({                                              // Simplify structure
    orderId: 'payload.orderId',
    customerId: 'payload.customerId',
    amount: 'payload.amount'
  })
  .groupBy('customerId')                             // Group by customer
  .aggregate({                                        // Aggregate stats
    orders: { $count: '*' },
    total: { $sum: 'amount' },
    avg: { $avg: 'amount' }
  })
  .execute()
```

## Utility Operations

```javascript
// Distinct
const uniqueUsers = await queen
  .stream('events@processor')
  .distinct('payload.userId')
  .execute()

// Limit
const first100 = await queen
  .stream('events@processor')
  .limit(100)
  .execute()

// Skip
const page2 = await queen
  .stream('events@processor')
  .skip(100)
  .limit(100)
  .execute()
```

## Consumer Groups

Streams use consumer groups to track progress:

```javascript
// Different consumer groups process independently
const stream1 = queen.stream('events@fraud-detection')
const stream2 = queen.stream('events@analytics')

// Each tracks its own position
```

## Field Paths

Access nested fields using dot notation:

```javascript
// Simple field
'payload.userId'

// Nested field
'payload.customer.address.country'

// Array element (if supported by server)
'payload.items[0].price'
```

## Aggregation Functions

| Function | Syntax | Description |
|----------|--------|-------------|
| Count | `{ $count: '*' }` | Count messages |
| Sum | `{ $sum: 'field' }` | Sum field values |
| Avg | `{ $avg: 'field' }` | Average field values |
| Min | `{ $min: 'field' }` | Minimum field value |
| Max | `{ $max: 'field' }` | Maximum field value |

## Filter Operators

| Operator | Syntax | Description |
|----------|--------|-------------|
| Equals | `{ field: value }` | Exact match |
| Not equals | `{ field: { $ne: value } }` | Not equal |
| Greater than | `{ field: { $gt: value } }` | Greater than |
| Greater/equal | `{ field: { $gte: value } }` | Greater or equal |
| Less than | `{ field: { $lt: value } }` | Less than |
| Less/equal | `{ field: { $lte: value } }` | Less or equal |
| In array | `{ field: { $in: [1,2,3] } }` | In list |
| Not in array | `{ field: { $nin: [1,2,3] } }` | Not in list |
| Contains (JSONB) | `{ field: { $contains: {...} } }` | JSONB contains |

## Server Requirements

**Phase 1 requires server-side implementation:**

The client builds execution plans (AST) and sends them to the server. The server must:

1. Accept execution plans at `/api/v1/stream/query`
2. Compile plans to SQL
3. Execute queries with consumer group tracking
4. Return results

See `STREAMING_V2.md` for complete server implementation details.

## Next Steps (Future Phases)

- **Phase 2**: Windows (tumbling, sliding, session)
- **Phase 3**: Joins (stream-stream, stream-table)
- **Phase 4**: Stateful operations
- **Phase 5**: Output to queues and tables
- **Phase 6**: Advanced operations (branch, foreach, etc.)

## Examples

See `examples/16-streaming-phase1.js` for complete working examples.

## Testing

Run tests:

```bash
cd client-js
node test-v2/streaming/phase1_basic.js
```

## API Reference

### Stream

- `.filter(predicate)` → Stream
- `.map(fields)` → Stream
- `.mapValues(mapper)` → Stream
- `.groupBy(key)` → GroupedStream
- `.distinct(field)` → Stream
- `.limit(n)` → Stream
- `.skip(n)` → Stream
- `.execute()` → Promise<Result>
- `.collect()` → Promise<Array>
- `.take(n)` → Promise<Array>

### GroupedStream

- `.count()` → Stream
- `.sum(field)` → Stream
- `.avg(field)` → Stream
- `.min(field)` → Stream
- `.max(field)` → Stream
- `.aggregate(specs)` → Stream
- `.execute()` → Promise<Result>

## Architecture

```
Client (JS)                          Server (C++)
┌─────────────┐                     ┌──────────────┐
│   Stream    │                     │ SQL Compiler │
│   Builder   │  Execution Plan     │              │
│             ├────────────────────>│  Validator   │
│  filter()   │  (JSON)             │              │
│  map()      │                     │  Executor    │
│  groupBy()  │                     │              │
└─────────────┘                     └──────┬───────┘
                                           │
                                           ↓
                                    ┌──────────────┐
                                    │  PostgreSQL  │
                                    │              │
                                    │  SQL Query   │
                                    └──────────────┘
```

## License

Same as Queen MQ - Apache 2.0

