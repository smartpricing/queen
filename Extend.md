# Queen Message Queue System Extension Plan

## Overview
This document outlines the implementation plan for adding three major features to the Queen message queue system:
1. **Optional Encryption**: System-wide encryption for sensitive message payloads using a single key
2. **Automatic Message Retention**: Time-based automatic deletion of old messages and partitions
3. **Message Eviction**: Automatic eviction of messages that exceed maximum wait time

## Current Architecture Analysis

### Database Structure
- **Queues** → **Partitions** → **Messages** hierarchy
- Messages stored with `payload JSONB` field
- Partitions have `options JSONB` for configuration
- No current encryption or TTL mechanisms

### Message Flow
1. **Push**: Client → Server → Database (plain JSONB payload)
2. **Pop**: Database → Server → Client (plain JSONB payload)
3. **Configuration**: Stored in partition `options` field

### Key Components
- `queueManagerOptimized.js`: Core message operations
- `routes/configure.js`: Queue/partition configuration
- `routes/push.js` & `routes/pop.js`: Message ingress/egress
- `server.js`: Background jobs (lease reclamation)

## Feature 1: Optional Encryption

### Design Decisions
- **Encryption Level**: System-wide with single key
- **Algorithm**: AES-256-GCM (authenticated encryption)
- **Key Management**: 
  - Single master key from environment variable
  - Used for all encrypted queues
  - Simple enable/disable flag per queue
- **Backward Compatibility**: Must support existing non-encrypted queues

### Implementation Plan

#### 1.1 Database Schema Changes
```sql
-- Add encryption flag to queues table (simplified)
ALTER TABLE queen.queues ADD COLUMN IF NOT EXISTS encryption_enabled BOOLEAN DEFAULT FALSE;

-- Add encrypted flag to messages for tracking
ALTER TABLE queen.messages ADD COLUMN IF NOT EXISTS is_encrypted BOOLEAN DEFAULT FALSE;

-- No key storage table needed - using single environment variable key
```

#### 1.2 Encryption Service Module
Create `src/services/encryptionService.js`:
```javascript
import crypto from 'crypto';

export const createEncryptionService = () => {
  const ALGORITHM = 'aes-256-gcm';
  const MASTER_KEY = process.env.QUEEN_ENCRYPTION_KEY; // 32-byte key from env
  
  if (!MASTER_KEY) {
    console.warn('QUEEN_ENCRYPTION_KEY not set - encryption will be disabled');
  }
  
  // Validate key is 32 bytes (256 bits)
  const getKey = () => {
    if (!MASTER_KEY) return null;
    const key = Buffer.from(MASTER_KEY, 'hex');
    if (key.length !== 32) {
      throw new Error('QUEEN_ENCRYPTION_KEY must be 32 bytes (64 hex characters)');
    }
    return key;
  };
  
  // Encrypt payload
  const encryptPayload = async (payload) => {
    const key = getKey();
    if (!key) throw new Error('Encryption key not configured');
    
    const iv = crypto.randomBytes(16);
    const cipher = crypto.createCipheriv(ALGORITHM, key, iv);
    
    const encrypted = Buffer.concat([
      cipher.update(JSON.stringify(payload), 'utf8'),
      cipher.final()
    ]);
    
    const authTag = cipher.getAuthTag();
    
    return {
      encrypted: encrypted.toString('base64'),
      iv: iv.toString('base64'),
      authTag: authTag.toString('base64')
    };
  };
  
  // Decrypt payload
  const decryptPayload = async (encryptedData) => {
    const key = getKey();
    if (!key) throw new Error('Encryption key not configured');
    
    const decipher = crypto.createDecipheriv(
      ALGORITHM,
      key,
      Buffer.from(encryptedData.iv, 'base64')
    );
    
    decipher.setAuthTag(Buffer.from(encryptedData.authTag, 'base64'));
    
    const decrypted = Buffer.concat([
      decipher.update(Buffer.from(encryptedData.encrypted, 'base64')),
      decipher.final()
    ]);
    
    return JSON.parse(decrypted.toString('utf8'));
  };
  
  const isEnabled = () => !!MASTER_KEY;
  
  return {
    encryptPayload,
    decryptPayload,
    isEnabled
  };
};
```

#### 1.3 Modified Push Flow
Update `src/managers/queueManagerOptimized.js`:
```javascript
// In pushMessagesBatch function
const pushMessagesBatch = async (items, encryptionService) => {
  // ... existing code ...
  
  // Check if queue has encryption enabled
  const queueResult = await client.query(
    'SELECT encryption_enabled FROM queen.queues WHERE name = $1',
    [queueName]
  );
  
  const encryptionEnabled = queueResult.rows[0]?.encryption_enabled;
  
  let finalPayload = payload;
  let isEncrypted = false;
  
  if (encryptionEnabled && encryptionService.isEnabled()) {
    const encryptedData = await encryptionService.encryptPayload(payload);
    finalPayload = encryptedData;
    isEncrypted = true;
  }
  
  // Insert with encryption flag
  const insertQuery = `
    INSERT INTO queen.messages 
    (transaction_id, partition_id, payload, is_encrypted, created_at)
    VALUES ($1, $2, $3, $4, NOW())
  `;
  
  await client.query(insertQuery, [
    transactionId,
    partitionId,
    finalPayload,
    isEncrypted
  ]);
};
```

#### 1.4 Modified Pop Flow
Update pop operations to decrypt:
```javascript
// In popMessages function
const popMessages = async (scope, options, encryptionService) => {
  // ... existing pop logic ...
  
  // After fetching messages
  if (result.rows.length > 0) {
    const messages = await Promise.all(
      result.rows.map(async (row) => {
        let payload = row.payload;
        
        if (row.is_encrypted && encryptionService.isEnabled()) {
          payload = await encryptionService.decryptPayload(payload);
        }
        
        return {
          ...row,
          payload
        };
      })
    );
    
    return { messages };
  }
};
```

#### 1.5 Configuration API Extension
Update `src/routes/configure.js`:
```javascript
// Add encryption flag to queue configuration
if (options.encryptionEnabled !== undefined) {
  await client.query(
    'UPDATE queen.queues SET encryption_enabled = $1 WHERE name = $2',
    [options.encryptionEnabled, queue]
  );
}
```

## Feature 2: Automatic Message Retention

### Design Decisions
- **Retention Granularity**: Partition-level configuration
- **Retention Types**:
  - Message retention: Delete messages after `retentionSeconds`
  - Completed message retention: Separate retention for completed/failed messages
  - Partition retention: Delete empty partitions after inactivity
- **Cleanup Strategy**: Background job with configurable interval
- **Performance**: Use partitioned deletes for efficiency

### Implementation Plan

#### 2.1 Database Schema Changes
```sql
-- Add retention configuration to partition options
-- Extend existing options JSONB to include:
-- {
--   "retentionSeconds": 86400,        -- Delete messages after 24 hours
--   "completedRetentionSeconds": 3600, -- Delete completed messages after 1 hour
--   "partitionRetentionSeconds": 604800, -- Delete empty partition after 7 days
--   "retentionEnabled": true
-- }

-- Add last_activity timestamp to partitions for partition retention
ALTER TABLE queen.partitions 
ADD COLUMN IF NOT EXISTS last_activity TIMESTAMP DEFAULT NOW();

-- Create retention tracking table
CREATE TABLE IF NOT EXISTS queen.retention_history (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    messages_deleted INTEGER DEFAULT 0,
    completed_deleted INTEGER DEFAULT 0,
    failed_deleted INTEGER DEFAULT 0,
    retention_type VARCHAR(50), -- 'retention', 'completed', 'failed', 'partition'
    executed_at TIMESTAMP DEFAULT NOW()
);

CREATE INDEX idx_retention_history_partition ON queen.retention_history(partition_id);
CREATE INDEX idx_retention_history_executed ON queen.retention_history(executed_at);

-- Add index for efficient retention queries
CREATE INDEX idx_messages_retention 
ON queen.messages(partition_id, created_at, status)
WHERE status IN ('completed', 'failed');
```

#### 2.2 Retention Service Module
Create `src/services/retentionService.js`:
```javascript
export const createRetentionService = (pool, eventManager) => {
  const RETENTION_INTERVAL = process.env.RETENTION_INTERVAL || 300000; // 5 minutes
  const BATCH_SIZE = 1000;
  
  // Main retention function
  const performRetention = async () => {
    const client = await pool.connect();
    try {
      // Get partitions with retention enabled
      const partitionsResult = await client.query(`
        SELECT p.id, p.name, p.queue_id, p.options, q.name as queue_name
        FROM queen.partitions p
        JOIN queen.queues q ON p.queue_id = q.id
        WHERE (p.options->>'retentionEnabled')::boolean = true
      `);
      
      for (const partition of partitionsResult.rows) {
        await retentionPartition(client, partition);
      }
      
      // Cleanup empty partitions
      await cleanupEmptyPartitions(client);
      
    } finally {
      client.release();
    }
  };
  
  // Apply retention to messages in a partition
  const retentionPartition = async (client, partition) => {
    const options = partition.options;
    const retentionSeconds = options.retentionSeconds || 86400;
    const completedRetentionSeconds = options.completedRetentionSeconds || retentionSeconds;
    
    let totalDeleted = 0;
    
    // Delete old pending messages
    if (retentionSeconds > 0) {
      const deleteResult = await client.query(`
        DELETE FROM queen.messages
        WHERE partition_id = $1
          AND status = 'pending'
          AND created_at < NOW() - INTERVAL '1 second' * $2
        RETURNING id
      `, [partition.id, retentionSeconds]);
      
      totalDeleted += deleteResult.rowCount;
    }
    
    // Delete completed/failed messages
    if (completedRetentionSeconds > 0) {
      const deleteResult = await client.query(`
        DELETE FROM queen.messages
        WHERE partition_id = $1
          AND status IN ('completed', 'failed')
          AND completed_at < NOW() - INTERVAL '1 second' * $2
        RETURNING id
      `, [partition.id, completedRetentionSeconds]);
      
      totalDeleted += deleteResult.rowCount;
    }
    
    // Log retention
    if (totalDeleted > 0) {
      await client.query(`
        INSERT INTO queen.retention_history 
        (partition_id, messages_deleted, retention_type)
        VALUES ($1, $2, 'retention')
      `, [partition.id, totalDeleted]);
      
      console.log(`Retained ${totalDeleted} messages from ${partition.queue_name}/${partition.name}`);
    }
    
    // Update partition last_activity
    await client.query(`
      UPDATE queen.partitions 
      SET last_activity = COALESCE(
        (SELECT MAX(created_at) FROM queen.messages WHERE partition_id = $1),
        last_activity
      )
      WHERE id = $1
    `, [partition.id]);
  };
  
  // Cleanup empty partitions
  const cleanupEmptyPartitions = async (client) => {
    const result = await client.query(`
      DELETE FROM queen.partitions p
      WHERE p.name != 'Default'
        AND (p.options->>'partitionRetentionSeconds')::int > 0
        AND NOT EXISTS (
          SELECT 1 FROM queen.messages m WHERE m.partition_id = p.id
        )
        AND p.last_activity < NOW() - INTERVAL '1 second' * (p.options->>'partitionRetentionSeconds')::int
      RETURNING id, name
    `);
    
    if (result.rowCount > 0) {
      console.log(`Deleted ${result.rowCount} empty partitions`);
    }
  };
  
  // Start retention job
  const startRetentionJob = () => {
    setInterval(async () => {
      try {
        await performRetention();
      } catch (error) {
        console.error('Retention job error:', error);
      }
    }, RETENTION_INTERVAL);
    
    console.log(`Retention job started (interval: ${RETENTION_INTERVAL}ms)`);
  };
  
  return {
    performRetention,
    startRetentionJob,
    retentionPartition,
    cleanupEmptyPartitions
  };
};
```

#### 2.3 Configuration API Extension
Update `src/routes/configure.js`:
```javascript
const validOptions = {
  // ... existing options ...
  retentionSeconds: options.retentionSeconds || 0, // 0 = disabled
  completedRetentionSeconds: options.completedRetentionSeconds || 0,
  partitionRetentionSeconds: options.partitionRetentionSeconds || 0,
  retentionEnabled: options.retentionEnabled || false
};
```

#### 2.4 Server Integration
Update `src/server.js`:
```javascript
import { createRetentionService } from './services/retentionService.js';
import { createEncryptionService } from './services/encryptionService.js';

// Initialize services
const encryptionService = createEncryptionService();
const retentionService = createRetentionService(pool, eventManager);

// Start retention job
retentionService.startRetentionJob();

// Pass services to queue manager
const queueManager = createOptimizedQueueManager(
  pool, 
  resourceCache, 
  eventManager,
  encryptionService
);
```

## Feature 3: Message Eviction

### Design Decisions
- **Eviction Level**: Queue-level configuration (applies to all partitions)
- **Eviction Logic**: Messages exceeding `maxWaitTimeSeconds` are marked as 'evicted' during pop
- **Status**: New status 'evicted' for messages that exceeded wait time
- **Performance**: Check eviction at pop time to avoid unnecessary background processing

### Implementation Plan

#### 3.1 Database Schema Changes
```sql
-- Add maxWaitTimeSeconds to queue configuration
ALTER TABLE queen.queues 
ADD COLUMN IF NOT EXISTS max_wait_time_seconds INTEGER DEFAULT 0; -- 0 = disabled

-- Update message status enum to include 'evicted'
-- Note: PostgreSQL doesn't support direct enum modification, so we need to recreate
ALTER TABLE queen.messages 
ALTER COLUMN status TYPE VARCHAR(20);

-- Add index for eviction queries
CREATE INDEX idx_messages_eviction 
ON queen.messages(partition_id, status, created_at)
WHERE status = 'pending';
```

#### 3.2 Modified Pop Flow with Eviction
Update `src/managers/queueManagerOptimized.js`:
```javascript
// In popMessages function - add eviction check
const popMessages = async (scope, options = {}) => {
  const { queue, partition } = scope;
  const { wait = false, timeout = 30000, batch = 1 } = options;
  
  return withTransaction(pool, async (client) => {
    // First, check and evict expired messages
    const evictionResult = await client.query(`
      WITH queue_config AS (
        SELECT q.id, q.max_wait_time_seconds
        FROM queen.queues q
        WHERE q.name = $1 AND q.max_wait_time_seconds > 0
        LIMIT 1
      ),
      evicted AS (
        UPDATE queen.messages m
        SET status = 'evicted',
            completed_at = NOW(),
            error_message = 'Message exceeded maximum wait time'
        FROM queue_config qc
        WHERE m.partition_id IN (
          SELECT p.id FROM queen.partitions p WHERE p.queue_id = qc.id
        )
        AND m.status = 'pending'
        AND m.created_at < NOW() - INTERVAL '1 second' * qc.max_wait_time_seconds
        RETURNING m.id, m.transaction_id
      )
      SELECT COUNT(*) as evicted_count FROM evicted
    `, [queue]);
    
    if (evictionResult.rows[0].evicted_count > 0) {
      console.log(`Evicted ${evictionResult.rows[0].evicted_count} messages from queue ${queue}`);
      
      // Emit events for evicted messages
      eventManager.emit('messages:evicted', {
        queue,
        count: evictionResult.rows[0].evicted_count
      });
    }
    
    // Now proceed with normal pop logic for non-evicted messages
    let result;
    if (partition) {
      // Specific partition pop (existing logic)
      result = await client.query(`
        WITH partition_info AS (
          SELECT p.id, p.name as partition_name, p.options, p.priority,
                 q.name as queue_name, q.max_wait_time_seconds,
                 COALESCE((p.options->>'delayedProcessing')::int, 0) as delayed_processing,
                 COALESCE((p.options->>'windowBuffer')::int, 0) as window_buffer
          FROM queen.partitions p
          JOIN queen.queues q ON p.queue_id = q.id
          WHERE q.name = $1 AND p.name = $2
          LIMIT 1
        )
        SELECT m.*, pi.partition_name, pi.queue_name, pi.options, pi.priority
        FROM queen.messages m
        JOIN partition_info pi ON m.partition_id = pi.id
        WHERE m.status = 'pending'
          AND m.created_at <= NOW() - INTERVAL '1 second' * pi.delayed_processing
          -- Exclude messages that would be evicted
          AND (pi.max_wait_time_seconds = 0 OR 
               m.created_at > NOW() - INTERVAL '1 second' * pi.max_wait_time_seconds)
          AND (pi.window_buffer = 0 OR NOT EXISTS (
            SELECT 1 FROM queen.messages m2 
            WHERE m2.partition_id = pi.id 
              AND m2.status = 'pending'
              AND m2.created_at > NOW() - INTERVAL '1 second' * pi.window_buffer
          ))
        ORDER BY m.created_at ASC
        LIMIT $3
        FOR UPDATE OF m SKIP LOCKED
      `, [queue, partition, batch]);
    } else {
      // Queue-level pop with eviction check (modify existing logic similarly)
      // ... existing queue-level pop logic with eviction filter ...
    }
    
    // ... rest of pop logic ...
  });
};
```

#### 3.3 Eviction Service Module (Optional Background Job)
Create `src/services/evictionService.js`:
```javascript
export const createEvictionService = (pool, eventManager) => {
  const EVICTION_INTERVAL = process.env.EVICTION_INTERVAL || 60000; // 1 minute
  
  // Proactive eviction for all queues
  const performEviction = async () => {
    const client = await pool.connect();
    try {
      const result = await client.query(`
        WITH queue_configs AS (
          SELECT q.id, q.name, q.max_wait_time_seconds
          FROM queen.queues q
          WHERE q.max_wait_time_seconds > 0
        ),
        evicted AS (
          UPDATE queen.messages m
          SET status = 'evicted',
              completed_at = NOW(),
              error_message = 'Message exceeded maximum wait time'
          FROM queue_configs qc
          WHERE m.partition_id IN (
            SELECT p.id FROM queen.partitions p WHERE p.queue_id = qc.id
          )
          AND m.status = 'pending'
          AND m.created_at < NOW() - INTERVAL '1 second' * qc.max_wait_time_seconds
          RETURNING m.id, m.partition_id, qc.name as queue_name
        )
        SELECT queue_name, COUNT(*) as count 
        FROM evicted 
        GROUP BY queue_name
      `);
      
      for (const row of result.rows) {
        console.log(`Evicted ${row.count} messages from queue ${row.queue_name}`);
        eventManager.emit('messages:evicted', {
          queue: row.queue_name,
          count: row.count
        });
      }
    } finally {
      client.release();
    }
  };
  
  // Start eviction job
  const startEvictionJob = () => {
    setInterval(async () => {
      try {
        await performEviction();
      } catch (error) {
        console.error('Eviction job error:', error);
      }
    }, EVICTION_INTERVAL);
    
    console.log(`Eviction job started (interval: ${EVICTION_INTERVAL}ms)`);
  };
  
  return {
    performEviction,
    startEvictionJob
  };
};
```

#### 3.4 Configuration API Extension
Update `src/routes/configure.js`:
```javascript
// Add maxWaitTimeSeconds to queue configuration
const configureQueue = async (body) => {
  const { queue, partition, namespace, task, options = {} } = body;
  
  // If maxWaitTimeSeconds is specified, update queue-level setting
  if (options.maxWaitTimeSeconds !== undefined) {
    await client.query(
      'UPDATE queen.queues SET max_wait_time_seconds = $1 WHERE name = $2',
      [options.maxWaitTimeSeconds, queue]
    );
  }
  
  // ... existing configuration logic ...
};
```

#### 3.5 Analytics Extension
Add eviction metrics to analytics:
```javascript
// In analytics routes
const getQueueStats = async (queueName) => {
  // ... existing stats ...
  
  // Add evicted message count
  const evictedResult = await pool.query(`
    SELECT COUNT(*) as evicted_count
    FROM queen.messages m
    JOIN queen.partitions p ON m.partition_id = p.id
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE q.name = $1 AND m.status = 'evicted'
  `, [queueName]);
  
  return {
    // ... existing stats ...
    evicted: evictedResult.rows[0].evicted_count
  };
};
```

## Migration Strategy

### Phase 1: Database Changes
1. Run schema migrations to add new columns and tables
2. Deploy without activating features
3. Verify no impact on existing operations

### Phase 2: Encryption Rollout
1. Deploy encryption service
2. Enable for test queues first
3. Monitor performance impact
4. Gradual rollout to production queues

### Phase 3: Cleanup Feature
1. Deploy cleanup service disabled
2. Test on development environment
3. Enable with conservative settings (long TTLs)
4. Gradually tune based on usage patterns

## Client SDK Updates

### Encryption Support
```javascript
// Server configuration - set environment variable
// QUEEN_ENCRYPTION_KEY=<64-character-hex-string>

// Configure queue with encryption enabled
await client.configure({
  queue: 'sensitive-data',
  options: {
    encryptionEnabled: true  // Simple flag - uses system-wide key
  }
});
```

### Retention Configuration
```javascript
// Configure queue with retention
await client.configure({
  queue: 'temporary-jobs',
  partition: 'batch-1',
  options: {
    retentionSeconds: 86400, // 24 hours
    completedRetentionSeconds: 3600, // 1 hour  
    partitionRetentionSeconds: 604800, // 7 days
    retentionEnabled: true
  }
});
```

### Eviction Configuration
```javascript
// Configure queue with message eviction
await client.configure({
  queue: 'time-sensitive-tasks',
  options: {
    maxWaitTimeSeconds: 300  // Evict messages older than 5 minutes
  }
});

// Combined configuration example
await client.configure({
  queue: 'production-jobs',
  partition: 'high-priority',
  options: {
    // Encryption
    encryptionEnabled: true,
    
    // Retention
    retentionSeconds: 86400, // Keep messages for 24 hours
    completedRetentionSeconds: 3600, // Keep completed for 1 hour
    retentionEnabled: true,
    
    // Eviction
    maxWaitTimeSeconds: 600, // Evict if not processed within 10 minutes
    
    // Existing options
    priority: 10,
    leaseTime: 300,
    retryLimit: 3
  }
});
```

## Testing Plan

### Encryption Tests
1. **Integration Tests**:
   - Push encrypted, pop decrypted
   - Mixed encrypted/non-encrypted queues
   - Performance benchmarks

2. **Security Tests**:
   - Attempt to read encrypted messages without key
   - Key validation (32 bytes requirement)
   - Authentication tag validation

### Retention Tests
1. **Unit Tests**:
   - Retention calculation logic
   - Batch deletion efficiency
   - Partition retention conditions

2. **Integration Tests**:
   - Messages deleted after retentionSeconds
   - Completed messages retention
   - Empty partition removal
   - Retention history tracking

3. **Performance Tests**:
   - Retention job impact on throughput
   - Large batch deletion performance
   - Database lock contention

### Eviction Tests
1. **Unit Tests**:
   - Eviction time calculation
   - Status transition to 'evicted'
   - Queue-level configuration

2. **Integration Tests**:
   - Messages evicted during pop operation
   - Eviction with maxWaitTimeSeconds
   - Event emission for evicted messages
   - Analytics tracking evicted count

3. **Performance Tests**:
   - Eviction check overhead during pop
   - Background eviction job efficiency
   - Impact on pop latency

## Performance Considerations

### Encryption Impact
- **CPU**: ~10-15% overhead for encryption/decryption
- **Storage**: ~33% increase due to Base64 encoding
- **Mitigation**: 
  - Hardware acceleration (AES-NI)
  - Single key reduces complexity
  - Async encryption operations

### Retention Impact
- **I/O**: Periodic spikes during retention cleanup
- **Locks**: Minimal with proper indexing
- **Mitigation**:
  - Off-peak scheduling
  - Batch size tuning
  - Partitioned deletes
  - Async retention operations

### Eviction Impact
- **Pop Latency**: Small overhead for eviction check
- **Storage**: Evicted messages retained until cleanup
- **Mitigation**:
  - Efficient CTE queries during pop
  - Optional background eviction job
  - Proper indexing on created_at
  - Combine with retention for cleanup

## Monitoring & Observability

### Metrics to Track
1. **Encryption**:
   - Encryption/decryption latency
   - Encryption errors
   - Queues with encryption enabled

2. **Retention**:
   - Messages deleted per run
   - Retention job duration
   - Storage reclaimed
   - Partition lifecycle

3. **Eviction**:
   - Messages evicted per queue
   - Eviction rate over time
   - Average wait time before eviction
   - Pop operations with evictions

### Logging
```javascript
// Structured logging for operations
logger.info('encryption.enabled', { queue, enabled: true });
logger.info('retention.performed', { 
  partition, 
  messagesDeleted, 
  duration 
});
logger.info('eviction.performed', {
  queue,
  evictedCount,
  maxWaitTimeSeconds
});
```

## Security Considerations

### Key Management
1. **Single Master Key**: Store in environment variable (QUEEN_ENCRYPTION_KEY)
2. **Key Format**: 32-byte (64 hex characters) for AES-256
3. **Access Control**: Restrict key access to queue service only
4. **Key Rotation**: Plan for future key rotation with re-encryption

### Data Protection
1. **At Rest**: Encrypted in database
2. **In Transit**: TLS for all connections
3. **In Memory**: Clear sensitive data after use
4. **Backups**: Ensure backups are also encrypted

## Documentation Updates

### API Documentation
- New encryption parameters in `/configure`
- Cleanup options in configuration
- Error codes for encryption failures

### Client SDK Documentation
- Encryption setup examples
- Cleanup configuration examples
- Migration guide for existing queues

### Operations Guide
- Key rotation procedures
- Cleanup tuning guidelines
- Monitoring setup

## Implementation Timeline

### Week 1-2: Foundation
- Database schema changes
- Basic encryption service
- Basic cleanup service

### Week 3-4: Integration
- Queue manager integration
- Route updates
- Client SDK updates

### Week 5-6: Testing
- Comprehensive test suite
- Performance benchmarking
- Security audit

### Week 7-8: Deployment
- Staged rollout
- Monitoring setup
- Documentation completion

## Rollback Plan

### Encryption Rollback
1. Disable encryption for new messages
2. Maintain decryption capability
3. Gradually migrate encrypted messages
4. Remove encryption columns when empty

### Cleanup Rollback
1. Disable cleanup job
2. Restore from backups if needed
3. Remove cleanup configuration
4. Drop cleanup tables

## Success Criteria

### Encryption
- ✅ Zero data breaches
- ✅ < 15% performance impact
- ✅ Simple single-key management
- ✅ 100% backward compatibility

### Retention
- ✅ 50% storage reduction for temporary queues
- ✅ Zero accidental data loss
- ✅ < 5% performance impact
- ✅ Configurable per partition with retentionSeconds

### Eviction
- ✅ Automatic eviction of stale messages
- ✅ < 2% pop operation overhead
- ✅ Clear eviction status tracking
- ✅ Queue-level maxWaitTimeSeconds configuration

## Conclusion

This extension plan provides Queen with three enterprise-grade features for data protection, storage management, and SLA enforcement. The modular design ensures minimal impact on existing functionality while providing powerful new capabilities.

The implementation delivers:
- **Security**: System-wide encryption with a single master key for simplicity
- **Efficiency**: Automatic retention policies to prevent storage bloat
- **Reliability**: Message eviction to enforce processing SLAs
- **Flexibility**: Per-queue and per-partition configuration options
- **Compatibility**: Full backward compatibility with existing deployments
- **Performance**: Minimal impact on message throughput

Key improvements from the original design:
1. **Simplified Encryption**: Single system-wide key instead of per-queue keys reduces complexity
2. **Clear Naming**: `retentionSeconds` parameter clearly indicates time-based retention
3. **SLA Enforcement**: `maxWaitTimeSeconds` ensures messages don't wait indefinitely

With these features, Queen becomes suitable for:
- Handling sensitive data with encryption
- Long-running deployments with automatic storage management
- Time-sensitive workloads with SLA requirements
- Production environments requiring data governance
