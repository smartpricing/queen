# Queue/Bus Mode Implementation Plan

## Overview
Implement dynamic queue/bus mode based on consumer group parameter, allowing the same queue to serve both competing consumers (queue mode) and broadcast to multiple consumer groups (bus mode).

## Core Concept
- **Queue Mode**: No `consumerGroup` specified → competing consumers (traditional queue behavior)
- **Bus Mode**: `consumerGroup` specified → each consumer group gets all messages (pub/sub behavior)
- **Mixed Mode**: Same queue can serve both patterns simultaneously

## Architecture Changes

### 1. Database Schema Changes (`src/database/schema-v2.sql`)

#### 1.1 Modify Messages Table
- Remove all status-related fields from `queen.messages`
- Keep only: `id`, `transaction_id`, `partition_id`, `payload`, `created_at`, `is_encrypted`
- Keep `transaction_id` in messages table (set during push, immutable)

#### 1.2 Create Messages Status Table
```sql
CREATE TABLE queen.messages_status (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    message_id UUID REFERENCES queen.messages(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255),  -- NULL for queue mode
    status VARCHAR(20) DEFAULT 'pending',
    worker_id VARCHAR(255),
    locked_at TIMESTAMP,
    completed_at TIMESTAMP,
    failed_at TIMESTAMP,
    error_message TEXT,
    retry_count INTEGER DEFAULT 0,
    lease_expires_at TIMESTAMP,
    processing_at TIMESTAMP,
    created_at TIMESTAMP DEFAULT NOW(),
    UNIQUE(message_id, consumer_group)
);
```

#### 1.3 Consumer Group Registry (Optional)
```sql
CREATE TABLE queen.consumer_groups (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    queue_id UUID REFERENCES queen.queues(id) ON DELETE CASCADE,
    name VARCHAR(255) NOT NULL,
    created_at TIMESTAMP DEFAULT NOW(),
    subscription_start_from TIMESTAMP,  -- NULL = consume all, timestamp = consume from this point
    active BOOLEAN DEFAULT true,
    UNIQUE(queue_id, name)
);
```

#### 1.3 Create Optimized Indexes
- Index for queue mode lookups (consumer_group IS NULL)
- Index for bus mode lookups (consumer_group IS NOT NULL)
- Composite indexes for efficient pop operations
- Indexes for lease expiration checks

### 2. Consumer Group Subscription Strategies

#### Option 1: Consume All Messages (Default)
- New consumer groups start from the beginning
- Process all existing messages in the queue
- Good for: Analytics, audit logs, data replication

#### Option 2: Consume From Subscription Time
- New consumer groups only see messages created after subscription
- Skip all historical messages
- Good for: Real-time notifications, monitoring

#### Option 3: Consume From Specific Time
- Consumer group specifies a start timestamp
- Process messages from that point forward
- Good for: Replay scenarios, recovery

#### Implementation Approach:
```javascript
// Option 1: Consume all (default)
client.pop({ 
  queue: 'events', 
  consumerGroup: 'analytics' 
});

// Option 2: From subscription time
client.pop({ 
  queue: 'events', 
  consumerGroup: 'notifications',
  subscriptionMode: 'new'  // Only new messages
});

// Option 3: From specific time
client.pop({ 
  queue: 'events', 
  consumerGroup: 'replay',
  subscriptionFrom: '2024-01-01T00:00:00Z'
});
```

#### Database Implementation:
```sql
-- When popping with a new consumer group:
-- 1. Check if consumer group exists in registry
-- 2. If not, create it with subscription preferences
-- 3. Only return messages based on subscription_start_from

-- For "new only" mode:
INSERT INTO queen.consumer_groups (queue_id, name, subscription_start_from)
VALUES ($1, $2, NOW());

-- For "all messages" mode:
INSERT INTO queen.consumer_groups (queue_id, name, subscription_start_from)
VALUES ($1, $2, NULL);  -- NULL means consume all

-- Pop query considers subscription time:
SELECT m.* FROM queen.messages m
WHERE m.partition_id = $1
  AND NOT EXISTS (
    SELECT 1 FROM queen.messages_status ms
    WHERE ms.message_id = m.id 
      AND ms.consumer_group = $2
  )
  AND m.created_at >= COALESCE(
    (SELECT subscription_start_from 
     FROM queen.consumer_groups 
     WHERE queue_id = $3 AND name = $2),
    '1970-01-01'::timestamp  -- If NULL, consume all
  )
ORDER BY m.created_at ASC
LIMIT $4;
```

### 3. Backend Changes

#### 3.1 Queue Manager (`src/managers/queueManagerOptimized.js`)

##### Push Messages
- Keep existing behavior: generate transaction_id during push
- Only insert into messages table (no status entry)
- Transaction_id remains in messages table (immutable)

##### Pop Messages
- **Queue Mode Logic** (no consumer group):
  ```javascript
  // 1. Find messages without status OR with pending status where consumer_group IS NULL
  // 2. Create/update status entry with consumer_group = NULL
  // 3. Mark as processing with lease
  // 4. Return messages
  ```

- **Bus Mode Logic** (with consumer group):
  ```javascript
  // 1. Find messages without status entry for this specific consumer_group
  // 2. Create status entry for this consumer_group
  // 3. Mark as processing with lease
  // 4. Return messages
  ```

##### Acknowledge Messages
- Update status for specific consumer_group (or NULL)
- In bus mode, message stays available for other consumer groups
- In queue mode, message is done after single acknowledgment

##### Reclaim Leases
- Check lease expiration per consumer_group
- Reset to pending for that specific consumer_group only

#### 3.2 Pop Route (`src/routes/pop.js`)
- Accept optional `consumerGroup` parameter
- Pass through to queue manager

#### 3.3 Server Routes (`src/server.js`)
- Update pop endpoint to accept consumer_group query parameter
- Ensure backward compatibility (no consumer_group = queue mode)

### 4. Client SDK Changes (`src/client/queenClient.js`)

#### 4.1 Pop Method
```javascript
pop({ queue, partition, consumerGroup, wait, timeout, batch })
```

#### 4.2 Consume Method
```javascript
consume({ queue, partition, consumerGroup, handler, options })
```

#### 4.3 Documentation
- Add consumer group examples
- Document queue vs bus mode behavior

### 5. Testing (`src/test/test.js`)

#### 5.1 Queue Mode Tests
- Test competing consumers (no consumer group)
- Verify only one consumer gets each message
- Test acknowledgment completes message

#### 5.2 Bus Mode Tests
- Test multiple consumer groups
- Verify all groups get all messages
- Test independent acknowledgment per group
- Test consumer group isolation

#### 5.3 Mixed Mode Tests
- Test same queue with both modes simultaneously
- Verify queue mode consumers compete
- Verify bus mode consumers all receive messages

#### 5.4 Edge Cases
- Test consumer group with special characters
- Test very long consumer group names
- Test lease expiration per consumer group
- Test retry logic per consumer group

### 6. Examples

#### 6.1 Create Bus Mode Example (`examples/bus-mode.js`)
- Demonstrate multiple consumer groups
- Show independent progress tracking
- Compare with queue mode

#### 6.2 Create Mixed Mode Example (`examples/mixed-mode.js`)
- Same queue, different consumption patterns
- Workers + Analytics + Notifications

### 7. Documentation Updates

#### 7.1 README.md
- Add Bus Mode section
- Document consumer groups
- Add comparison with Kafka
- Update API reference

#### 7.2 API.md
- Document consumer_group parameter
- Add bus mode examples

## Implementation Steps

### Phase 1: Database Schema
1. Backup existing schema
2. Modify `schema-v2.sql` with new structure
3. Drop and recreate database:
   ```bash
   nvm use 22 && node init-db.js
   ```

### Phase 2: Backend Core
1. Update queue manager push logic
2. Implement dual-mode pop logic
3. Update acknowledgment logic
4. Update lease reclaim logic
5. Test with basic operations

### Phase 3: API Layer
1. Update pop route to accept consumer_group
2. Update server endpoints
3. Ensure backward compatibility
4. Test API endpoints

### Phase 4: Client SDK
1. Add consumerGroup parameter to pop
2. Update consume method
3. Add TypeScript types (if applicable)
4. Test client operations

### Phase 5: Testing
1. Write comprehensive test suite
2. Test queue mode (backward compatibility)
3. Test bus mode (new functionality)
4. Test mixed mode scenarios
5. Performance testing

### Phase 6: Examples & Documentation
1. Create example scripts
2. Update README
3. Update API documentation
4. Create migration guide

## Testing Commands

```bash
# Reinitialize database
nvm use 22 && node init-db.js

# Start server
nvm use 22 && npm start

# Run tests
nvm use 22 && node src/test/test.js

# Test bus mode
nvm use 22 && node examples/bus-mode.js

# Test mixed mode
nvm use 22 && node examples/mixed-mode.js
```

## Performance Considerations

### Optimizations
1. Use partial indexes for NULL/NOT NULL consumer_group
2. Batch status insertions for bus mode
3. Consider materialized views for message counts
4. Add connection pooling per consumer group

### Monitoring
1. Track consumer group lag
2. Monitor status table growth
3. Add metrics for queue vs bus mode usage
4. Dashboard updates for consumer groups

## Rollback Plan
1. Keep backup of original schema
2. Maintain backward compatibility
3. Feature flag for bus mode (optional)
4. Gradual rollout strategy

## Success Criteria
- [ ] All existing tests pass (backward compatibility)
- [ ] Queue mode works as before
- [ ] Bus mode delivers to all consumer groups
- [ ] Mixed mode works correctly
- [ ] Performance remains acceptable
- [ ] Documentation is complete
- [ ] Examples demonstrate all patterns

## Notes
- Consumer group = NULL is special case for queue mode
- Transaction ID moves to status table
- Messages table becomes immutable after insert
- Status table handles all mutable state
- Consider TTL for old status entries
- Add cleanup job for completed messages in bus mode
