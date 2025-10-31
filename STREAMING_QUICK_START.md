# Queen Streaming API - Quick Start

**Phase 1**: Filter, Map, GroupBy, Aggregate - READY TO USE!

---

## Installation

```javascript
import { Queen } from './client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')
```

---

## Basic Usage

### Create a Stream

```javascript
const stream = queen.stream('events@analytics')
```

Format: `queue@consumerGroup` or `queue@consumerGroup/partition`

---

## Operations

### Filter

```javascript
// Single condition
.filter({ 'payload.amount': { $gt: 1000 } })

// Multiple conditions (AND)
.filter({
  'payload.amount': { $gt: 1000 },
  'payload.status': 'completed'
})

// Available operators
$eq   $ne   $gt   $gte   $lt   $lte   $in   $nin
```

### Map

```javascript
// Map to new structure
.map({
  userId: 'payload.userId',
  amount: 'payload.amount',
  timestamp: 'created_at'
})
```

### GroupBy + Aggregations

```javascript
// Count per group
.groupBy('payload.userId').count()

// Sum per group
.groupBy('payload.userId').sum('payload.amount')

// Multiple aggregations
.groupBy('payload.userId').aggregate({
  count: { $count: '*' },
  total: { $sum: 'payload.amount' },
  avg: { $avg: 'payload.amount' },
  min: { $min: 'payload.amount' },
  max: { $max: 'payload.amount' }
})
```

### Utility

```javascript
.distinct('payload.userId')  // Unique values
.limit(100)                  // First 100
.skip(50)                    // Skip first 50
```

---

## Execute

```javascript
// Get results
const result = await stream.execute()
console.log(result.messages)

// Stream results
for await (const msg of stream) {
  console.log(msg)
}

// Collect into array
const all = await stream.collect()
const first10 = await stream.take(10)
```

---

## Complete Example

```javascript
import { Queen } from './client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

// Create queue
await queen.queue('sales').create()

// Push data
await queen.queue('sales').push([
  { customer: 'alice', product: 'laptop', amount: 1200, status: 'completed' },
  { customer: 'bob', product: 'mouse', amount: 25, status: 'completed' },
  { customer: 'alice', product: 'keyboard', amount: 80, status: 'completed' },
  { customer: 'charlie', product: 'monitor', amount: 300, status: 'pending' }
])

// Analyze completed high-value sales per customer
const stats = await queen
  .stream('sales@analytics')
  .filter({ 'payload.status': 'completed' })
  .filter({ 'payload.amount': { $gt: 50 } })
  .map({
    customer: 'payload.customer',
    amount: 'payload.amount'
  })
  .groupBy('customer')
  .aggregate({
    purchases: { $count: '*' },
    revenue: { $sum: 'amount' },
    avgOrder: { $avg: 'amount' }
  })
  .execute()

console.log(stats.messages)
// [
//   { customer: 'alice', purchases: 2, revenue: 1280, avgOrder: 640 },
//   { customer: 'charlie', purchases: 1, revenue: 300, avgOrder: 300 }
// ]

await queen.close()
```

---

## Start Testing

### 1. Start Server

```bash
cd server
DB_POOL_SIZE=50 ./bin/queen-server
```

### 2. Run Example

```bash
cd client-js
nvm use 22 && node ../examples/16-streaming-phase1.js
```

### 3. Run Tests

```bash
nvm use 22 && node test-v2/streaming/phase1_basic.js
```

### 4. Or use automated script

```bash
./TEST_STREAMING_PHASE1.sh
```

---

## Cheat Sheet

```javascript
// Basic pipeline pattern
const result = await queen
  .stream('queue@consumerGroup')
  .filter({ /* conditions */ })
  .map({ /* field mappings */ })
  .groupBy(/* key */)
  .aggregate({ /* aggregations */ })
  .execute()

// Filter operators
{ 'field': value }              // equals
{ 'field': { $gt: value } }     // greater than
{ 'field': { $gte: value } }    // greater or equal
{ 'field': { $lt: value } }     // less than
{ 'field': { $lte: value } }    // less or equal
{ 'field': { $ne: value } }     // not equal
{ 'field': { $in: [1,2,3] } }   // in array
{ 'field': { $nin: [1,2,3] } }  // not in array

// Aggregation functions
{ $count: '*' }                 // count
{ $sum: 'field' }               // sum
{ $avg: 'field' }               // average
{ $min: 'field' }               // minimum
{ $max: 'field' }               // maximum

// Field paths
'payload.userId'                // simple field
'payload.nested.field'          // nested field
'created_at'                    // message timestamp
```

---

## Tips

1. **Consumer Groups** - Each stream uses a consumer group to track progress
2. **Field Paths** - Use dot notation: `payload.userId`
3. **Chaining** - Operations return new stream objects, chain freely
4. **Security** - No SQL injection, all queries built server-side
5. **Performance** - PostgreSQL-optimized queries with proper indexes

---

## API Reference

See: `client-js/client-v2/stream/README.md`

---

## Troubleshooting

**Issue**: "Source queue is required"
- **Fix**: Ensure stream source is in format `queue@consumerGroup`

**Issue**: Query returns no results
- **Fix**: Check if messages exist and consumer group hasn't already consumed them

**Issue**: Type conversion errors
- **Fix**: Ensure numeric fields are being compared as numbers (server handles this)

---

## What's Next?

**Phase 2** - Windows (tumbling, sliding, session)  
**Phase 3** - Joins (stream-stream, stream-table)  
**Phase 4** - Stateful operations  
**Phase 5** - Output to queues/tables  

---

**Phase 1 is READY** - Start building streaming pipelines! 🚀

