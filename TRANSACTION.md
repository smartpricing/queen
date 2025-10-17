# Transaction Implementation Plan

## Core Changes

### 1. Database Schema
```sql
-- No changes needed - use existing worker_id field as lease_id
-- worker_id VARCHAR(255) already exists in partition_consumers table
```

### 2. Queue Manager Refactoring (`src/managers/queueManagerOptimized.js`)

#### 2.1 Extract Internal Operations
```javascript
// Line 205: Extract pushMessagesBatch logic to accept client parameter
const pushMessagesInternal = async (client, items) => {
  // Move existing pushMessagesBatch logic here
  // Replace withTransaction(pool, ...) with direct client usage
};

// Line 467: Extract acknowledgeMessages logic  
const acknowledgeMessageInternal = async (client, acknowledgments, consumerGroup) => {
  // Move existing acknowledgeMessages logic here
  // Replace withTransaction(pool, ...) with direct client usage
};

// Keep public methods that create their own transactions
const pushMessagesBatch = async (items) => {
  return withTransaction(pool, async (client) => {
    return pushMessagesInternal(client, items);
  });
};

const acknowledgeMessages = async (acknowledgments, consumerGroup) => {
  return withTransaction(pool, async (client) => {
    return acknowledgeMessageInternal(client, acknowledgments, consumerGroup);
  });
};
```

#### 2.2 Modify uniquePop to Return Lease ID
```javascript
// Line 1001: Inside uniquePop function
// Generate unique lease ID and store in worker_id field
const leaseId = generateUUID();

// Line 1237: Update lease acquisition query
const leaseResult = await client.query(`
  UPDATE queen.partition_consumers
  SET lease_expires_at = NOW() + INTERVAL '1 second' * $3,
      worker_id = $4,  -- Store lease ID here
      lease_acquired_at = NOW(),
      message_batch = NULL,
      batch_size = 0,
      acked_count = 0
  WHERE partition_id = $1 
    AND consumer_group = $2
    AND (lease_expires_at IS NULL OR lease_expires_at <= NOW())
  RETURNING partition_id, worker_id as lease_id
`, [candidate.partition_id, actualConsumerGroup, candidate.lease_time, leaseId]);

// Line 1370: Include lease ID in each message
const formattedMessages = await Promise.all(messages.map(async (msg) => {
  // ... existing decryption logic ...
  return {
    ...existingFields,
    leaseId: leaseId  // Add lease ID to each message
  };
}));
```

#### 2.3 Add Transaction Execution Method
```javascript
// Add new method after line 1416
const executeTransaction = async (operations, requiredLeases = []) => {
  return withTransaction(pool, async (client) => {
    // Validate leases
    if (requiredLeases.length > 0) {
      const leaseCheck = await client.query(`
        SELECT COUNT(*) = $2 as all_valid
        FROM queen.partition_consumers
        WHERE worker_id = ANY($1::varchar[])
          AND lease_expires_at > NOW()
      `, [requiredLeases, requiredLeases.length]);
      
      if (!leaseCheck.rows[0].all_valid) {
        throw new Error('One or more leases invalid');
      }
    }
    
    // Execute operations
    const results = [];
    for (const op of operations) {
      switch (op.type) {
        case 'push':
          results.push(await pushMessagesInternal(client, op.items));
          break;
        case 'ack':
          const acks = [{
            transactionId: op.transactionId,
            status: op.status || 'completed',
            error: op.error
          }];
          results.push(await acknowledgeMessageInternal(client, acks, op.consumerGroup));
          break;
        case 'extend':
          const extendResult = await client.query(`
            UPDATE queen.partition_consumers
            SET lease_expires_at = GREATEST(lease_expires_at, NOW() + INTERVAL '1 second' * $1)
            WHERE worker_id = $2 AND lease_expires_at > NOW()
            RETURNING lease_expires_at
          `, [op.seconds || 60, op.leaseId]);
          results.push({ type: 'extend', result: extendResult.rows[0] });
          break;
      }
    }
    return results;
  });
};

// Line 1430: Export new method
return {
  ...existingExports,
  executeTransaction
};
```

### 3. Server Endpoints (`src/server.js`)

#### 3.1 Add Lease Extension Endpoint (after line 573)
```javascript
app.post('/api/v1/lease/:leaseId/extend', (res, req) => {
  const abortedRef = { aborted: false };
  res.onAborted(() => { abortedRef.aborted = true; });
  
  const leaseId = req.params.leaseId;
  
  readJson(res, async (body) => {
    if (abortedRef.aborted) return;
    
    const { seconds = 60 } = body;
    
    try {
      const result = await pool.query(`
        UPDATE queen.partition_consumers
        SET lease_expires_at = GREATEST(lease_expires_at, NOW() + INTERVAL '1 second' * $1)
        WHERE worker_id = $2 AND lease_expires_at > NOW()
        RETURNING lease_expires_at, partition_id
      `, [seconds, leaseId]);
      
      if (result.rows.length === 0) {
        return sendError(res, 'Lease not found or expired', 404);
      }
      
      sendJSON(res, { 
        leaseId,
        newExpiresAt: result.rows[0].lease_expires_at
      });
    } catch (error) {
      if (!abortedRef.aborted) {
        sendError(res, error.message, 500);
      }
    }
  });
});
```

#### 3.2 Add Transaction Endpoint (after lease extension)
```javascript
app.post('/api/v1/transaction', (res, req) => {
  const abortedRef = { aborted: false };
  res.onAborted(() => { abortedRef.aborted = true; });
  
  readJson(res, async (body) => {
    if (abortedRef.aborted) return;
    
    const { operations = [], requiredLeases = [] } = body;
    
    if (!Array.isArray(operations) || operations.length === 0) {
      return sendError(res, 'Operations array required', 400);
    }
    
    try {
      const results = await queueManager.executeTransaction(operations, requiredLeases);
      
      if (!abortedRef.aborted) {
        sendJSON(res, {
          success: true,
          results,
          transactionId: generateUUID()
        });
      }
    } catch (error) {
      if (!abortedRef.aborted) {
        sendError(res, error.message, 400);
      }
    }
  });
});
```

### 4. ACK Route Update (`src/routes/ack.js`)

```javascript
// Line 3: Accept leaseId parameter
const { transactionId, status, error, consumerGroup, leaseId } = body;

// Line 13: Pass leaseId to queue manager
const result = await queueManager.acknowledgeMessage(
  transactionId, 
  status, 
  error, 
  consumerGroup,
  leaseId  // New parameter
);
```

### 5. Update acknowledgeMessage to Validate Lease

```javascript
// In queueManagerOptimized.js, line 461
const acknowledgeMessage = async (transactionId, status = 'completed', error = null, consumerGroup = null, leaseId = null) => {
  // Validate lease if provided
  if (leaseId) {
    const valid = await pool.query(`
      SELECT 1 FROM queen.partition_consumers pc
      JOIN queen.messages m ON m.partition_id = pc.partition_id
      WHERE m.transaction_id = $1 
        AND pc.worker_id = $2
        AND pc.lease_expires_at > NOW()
    `, [transactionId, leaseId]);
    
    if (valid.rows.length === 0) {
      throw new Error('Invalid lease');
    }
  }
  
  // Continue with existing logic
  const batchResult = await acknowledgeMessages([{ transactionId, status, error }], consumerGroup);
  return batchResult[0] || { status: 'not_found', transaction_id: transactionId };
};
```

## Testing

### Manual Testing Commands
```bash
# 1. Test POP returns lease ID
curl -X POST http://localhost:6632/api/v1/pop?queue=test
# Response should include leaseId in each message

# 2. Test lease extension
LEASE_ID="<from-pop-response>"
curl -X POST http://localhost:6632/api/v1/lease/$LEASE_ID/extend \
  -d '{"seconds": 120}'

# 3. Test transaction
curl -X POST http://localhost:6632/api/v1/transaction \
  -H "Content-Type: application/json" \
  -d '{
    "operations": [
      {"type": "ack", "transactionId": "...", "leaseId": "..."},
      {"type": "push", "items": [{"queue": "test", "payload": {}}]},
      {"type": "extend", "leaseId": "...", "seconds": 60}
    ],
    "requiredLeases": ["..."]
  }'

# 4. Test ACK with lease validation
curl -X POST http://localhost:6632/api/v1/ack \
  -d '{"transactionId": "...", "leaseId": "...", "status": "completed"}'
```

## Implementation Order

1. **queueManagerOptimized.js**: Add lease ID to uniquePop messages
2. **server.js**: Add lease extension endpoint  
3. **queueManagerOptimized.js**: Extract internal operations (pushMessagesInternal, acknowledgeMessageInternal)
4. **queueManagerOptimized.js**: Add executeTransaction method
5. **server.js**: Add transaction endpoint
6. **routes/ack.js**: Update to accept and validate lease ID

## Notes

- No database schema changes required
- Uses existing `worker_id` field as lease identifier
- Backward compatible - existing clients continue to work
- Lease validation is optional (only if leaseId provided)
