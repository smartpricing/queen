# Cursor-Based Consumption Model

## Overview

Queen now uses a **cursor-based consumption model** for optimal performance at scale. This provides O(batch_size) constant-time POP operations regardless of queue depth.

## Key Concepts

### 1. **Partition Cursors**

Each partition maintains a cursor position per consumer group:
- `last_consumed_created_at`: Timestamp of last consumed message
- `last_consumed_id`: UUID of last consumed message (tie-breaker)

The cursor always moves **forward** in time, ensuring strict FIFO ordering.

### 2. **Batch Acknowledgment Semantics**

#### **Partial Success** (some messages succeed, some fail):
```
Batch: 10,000 messages (msg1-msg10000)
Success: 9,999 messages
Failed: 1 message (msg5000)

Behavior:
✅ Cursor advances to msg10000
✅ Failed message (msg5000) moved to Dead Letter Queue
✅ Next POP starts from msg10001
✅ FIFO maintained, but msg5000 NOT retried in main queue
```

#### **Total Batch Failure** (all messages fail):
```
Batch: 10,000 messages (msg1-msg10000)
Success: 0 messages
Failed: 10,000 messages

Behavior:
❌ Cursor DOES NOT advance
❌ Messages NOT moved to DLQ
✅ Lease released
✅ Next POP gets SAME batch (retry)
✅ Allows recovery from transient failures
```

## Performance Characteristics

### **Query Complexity:**

| Operation | Old Approach | Cursor Approach | Improvement |
|-----------|--------------|-----------------|-------------|
| POP @ 0% consumed | O(partition_size) | O(batch_size) | Same |
| POP @ 50% consumed | O(partition_size) | O(batch_size) | **10x faster** |
| POP @ 99% consumed | O(partition_size) | O(batch_size) | **100x faster** |

### **Observed Performance:**

```
1M message benchmark:

Old approach:
  Early: 300ms per POP
  Late:  3500ms per POP (10x degradation)
  
Cursor approach:
  Early: 150ms per POP
  Late:  200ms per POP (constant!)
```

## Failed Message Handling

### **Individual Failures → Dead Letter Queue**

Failed messages are moved to `queen.dead_letter_queue` table with:
- Original message reference
- Error message
- Timestamp
- Consumer group

**Access failed messages:**
```sql
SELECT * FROM queen.dead_letter_queue
WHERE consumer_group = 'my-group'
ORDER BY failed_at DESC;
```

### **Batch Retry Strategy**

**Philosophy:** If an entire batch fails, it's likely a transient issue (network, DB, external service). Retry makes sense.

**Examples:**
- Database timeout → Retry batch
- External API down → Retry batch  
- Worker crash → Retry batch

**Individual failures** indicate message-specific issues:
- Malformed payload → DLQ
- Business logic rejection → DLQ
- Data validation failure → DLQ

## Trade-offs

### ✅ **Advantages:**

1. **Constant-time performance** regardless of consumption progress
2. **Strict FIFO ordering** maintained
3. **Simpler architecture** (no complex retry-in-queue logic)
4. **Scales to billions** of messages
5. **Clear failure semantics** (DLQ vs retry)

### ⚠️ **Considerations:**

1. **Individual message retries require manual intervention**
   - Failed messages go to DLQ
   - User can inspect, fix, and re-push if needed
   
2. **Batch retries are all-or-nothing**
   - If 1 message fails repeatedly, whole batch retries
   - Mitigation: Keep batch sizes reasonable
   
3. **No per-message retry limits in main queue**
   - Retry limits apply to entire batches
   - Individual failures immediately go to DLQ

## Migration Notes

**Schema changes:**
- Added `queen.partition_cursors` table
- Added `queen.dead_letter_queue` table

**Behavioral changes:**
- Individual failures no longer retry in main queue
- Cursor always advances on partial success
- Total batch failures retry entire batch

**Backward compatibility:**
- `messages_status` table still used for bus mode and observability
- Existing messages can be consumed with cursor starting from beginning
- No data loss during migration

## Best Practices

### **Handling DLQ Messages:**

```javascript
// Monitor DLQ
const dlqMessages = await queen.getDLQMessages({ limit: 100 });

// Inspect and fix issues
for (const msg of dlqMessages) {
  console.log(`Failed: ${msg.error_message}`);
  
  // After fixing data/issue, re-push if needed
  await queen.push(msg.queuePath, fixedPayload);
}
```

### **Batch Size Guidelines:**

```
Smaller batches (100-1000):
  ✅ Faster individual batch processing
  ✅ Less impact if entire batch fails
  ❌ More network round-trips

Larger batches (5000-10000):
  ✅ Higher throughput
  ✅ Fewer network round-trips
  ❌ More messages retry if entire batch fails
```

**Recommendation:** Start with 1000-2000 for balance.

## Observability

Monitor cursor progress:
```sql
SELECT 
  p.name as partition,
  pc.consumer_group,
  pc.total_messages_consumed,
  pc.total_batches_consumed,
  pc.last_consumed_at
FROM queen.partition_cursors pc
JOIN queen.partitions p ON p.id = pc.partition_id;
```

Monitor DLQ:
```sql
SELECT COUNT(*), consumer_group
FROM queen.dead_letter_queue
GROUP BY consumer_group;
```

