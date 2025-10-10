# Queen Internal Event Propagation System

## Overview
A general-purpose internal event system using Queen's own infrastructure to propagate system events (configuration changes, cache invalidations, etc.) across multiple server instances.

## Architecture

### Event Flow
```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   Server #1     │     │   Server #2     │     │   Server #3     │
│                 │     │                 │     │                 │
│ Event Producer  │     │ Event Consumer  │     │ Event Consumer  │
│ Event Consumer  │     │                 │     │                 │
└────────┬────────┘     └────────┬────────┘     └────────┬────────┘
         │                       │                       │
         └───────────┬───────────┴───────────────────────┘
                     ▼
         ┌───────────────────────────┐
         │    __system_events__      │
         │         Queue              │
         └───────────────────────────┘
```

### Event Structure
```javascript
{
  eventType: 'queue.updated',  // queue.created, queue.updated, queue.deleted
  entityType: 'queue',         // queue, partition, consumer_group
  entityId: 'user-tasks',      // queue name or other identifier
  changes: {                   // What changed
    retryLimit: { old: 3, new: 5 },
    priority: { old: 0, new: 10 }
  },
  timestamp: 1234567890,
  sourceServer: 'server_1234',
  version: 1                   // Event schema version
}
```

## Implementation

### Phase 1: Core Event System

#### 1.1 System Event Manager
**File**: `src/managers/systemEventManager.js`
```javascript
import { generateUUID } from '../utils/uuid.js';

export const SYSTEM_QUEUE = '__system_events__';

export const EventTypes = {
  // Queue events
  QUEUE_CREATED: 'queue.created',
  QUEUE_UPDATED: 'queue.updated',
  QUEUE_DELETED: 'queue.deleted',
  
  // Partition events
  PARTITION_CREATED: 'partition.created',
  PARTITION_DELETED: 'partition.deleted',
  
  // Future events
  CONSUMER_GROUP_CREATED: 'consumer_group.created',
  CONSUMER_GROUP_UPDATED: 'consumer_group.updated'
};

export class SystemEventManager {
  constructor(pool, serverInstanceId) {
    this.pool = pool;
    this.serverInstanceId = serverInstanceId;
    this.handlers = new Map();
    this.eventQueue = [];
    this.batchTimer = null;
  }
  
  // Register event handlers
  on(eventType, handler) {
    if (!this.handlers.has(eventType)) {
      this.handlers.set(eventType, []);
    }
    this.handlers.get(eventType).push(handler);
  }
  
  // Emit event locally and to system queue
  async emit(eventType, data) {
    const event = {
      id: generateUUID(),
      eventType,
      ...data,
      timestamp: Date.now(),
      sourceServer: this.serverInstanceId,
      version: 1
    };
    
    // Handle locally first (immediate consistency for originating server)
    await this.handleEventLocally(event);
    
    // Queue for batch publishing
    this.eventQueue.push(event);
    this.scheduleBatch();
  }
  
  // Process incoming event from system queue
  async processSystemEvent(event) {
    // Skip own events (already processed locally)
    if (event.sourceServer === this.serverInstanceId) {
      return;
    }
    
    await this.handleEventLocally(event);
  }
  
  async handleEventLocally(event) {
    const handlers = this.handlers.get(event.eventType) || [];
    for (const handler of handlers) {
      try {
        await handler(event);
      } catch (error) {
        console.error(`Handler failed for ${event.eventType}:`, error);
      }
    }
  }
  
  scheduleBatch() {
    if (this.batchTimer) return;
    
    this.batchTimer = setTimeout(() => {
      this.publishBatch();
    }, 10); // 10ms batching
  }
  
  async publishBatch() {
    if (this.eventQueue.length === 0) return;
    
    const events = [...this.eventQueue];
    this.eventQueue = [];
    this.batchTimer = null;
    
    try {
      // Direct database insert to avoid circular dependency
      const client = await this.pool.connect();
      try {
        for (const event of events) {
          await this.insertSystemEvent(client, event);
        }
      } finally {
        client.release();
      }
    } catch (error) {
      console.error('Failed to publish system events:', error);
    }
  }
  
  async insertSystemEvent(client, event) {
    // Direct insert bypassing queue manager to avoid circular dependency
    await client.query(`
      INSERT INTO queen.messages (
        id, transaction_id, partition_id, payload, 
        created_at, trace_id
      )
      SELECT 
        $1, $2, p.id, $3, NOW(), $4
      FROM queen.partitions p
      JOIN queen.queues q ON p.queue_id = q.id
      WHERE q.name = $5 AND p.name = 'Default'
    `, [
      generateUUID(),
      generateUUID(),
      JSON.stringify(event),
      event.id,
      SYSTEM_QUEUE
    ]);
  }
}
```

#### 1.2 Server Startup Synchronization
**File**: `src/services/startupSync.js`
```javascript
export async function syncSystemEvents(client, eventManager) {
  console.log('Synchronizing system events...');
  
  let processed = 0;
  let hasMore = true;
  
  while (hasMore) {
    // Pop and process system events in batches
    const result = await client.pop({
      queue: SYSTEM_QUEUE,
      batch: 100,
      wait: false
    });
    
    if (!result.messages || result.messages.length === 0) {
      hasMore = false;
      break;
    }
    
    for (const message of result.messages) {
      await eventManager.processSystemEvent(message.payload);
      await client.ack(message.transactionId, 'completed');
      processed++;
    }
  }
  
  console.log(`Synchronized ${processed} system events`);
  return processed;
}
```

#### 1.3 Resource Cache Integration
**File**: `src/managers/resourceCache.js` (additions)
```javascript
export const createResourceCache = () => {
  // ... existing code ...
  
  // Register event handlers
  const registerEventHandlers = (eventManager) => {
    // Invalidate cache on queue changes
    eventManager.on(EventTypes.QUEUE_UPDATED, (event) => {
      invalidateQueue(event.entityId);
    });
    
    eventManager.on(EventTypes.QUEUE_DELETED, (event) => {
      invalidateQueue(event.entityId);
    });
    
    eventManager.on(EventTypes.PARTITION_CREATED, (event) => {
      invalidate(event.queueName, event.entityId);
    });
    
    eventManager.on(EventTypes.PARTITION_DELETED, (event) => {
      invalidate(event.queueName, event.entityId);
    });
  };
  
  return {
    // ... existing methods ...
    registerEventHandlers
  };
};
```

### Phase 2: Queue Manager Integration

#### 2.1 Emit Events on Configuration Changes
**File**: `src/managers/queueManagerOptimized.js` (modifications)
```javascript
const configureQueue = async (queueName, options = {}, namespace = null, task = null) => {
  return withTransaction(pool, async (client) => {
    // Check if queue exists (for update vs create detection)
    const existingQueue = await client.query(
      'SELECT * FROM queen.queues WHERE name = $1',
      [queueName]
    );
    const isUpdate = existingQueue.rows.length > 0;
    
    // ... existing configuration code ...
    
    // Detect what changed
    const changes = {};
    if (isUpdate) {
      const old = existingQueue.rows[0];
      for (const [key, value] of Object.entries(options)) {
        const dbKey = optionMappings[key];
        if (old[dbKey] !== value) {
          changes[key] = { old: old[dbKey], new: value };
        }
      }
    }
    
    // Emit appropriate event
    await eventManager.emit(
      isUpdate ? EventTypes.QUEUE_UPDATED : EventTypes.QUEUE_CREATED,
      {
        entityType: 'queue',
        entityId: queueName,
        changes: isUpdate ? changes : options,
        namespace,
        task
      }
    );
    
    // Local cache invalidation (immediate)
    resourceCache.invalidateQueue(queueName);
    
    return result;
  });
};
```

### Phase 3: Server Integration

#### 3.1 Server Initialization
**File**: `src/server.js` (modifications)
```javascript
import { SystemEventManager, SYSTEM_QUEUE } from './managers/systemEventManager.js';
import { syncSystemEvents } from './services/startupSync.js';

// Generate unique server instance ID
const SERVER_INSTANCE_ID = `server_${process.pid}_${Date.now()}`;

// Initialize system event manager
const systemEventManager = new SystemEventManager(pool, SERVER_INSTANCE_ID);

// Register cache event handlers
resourceCache.registerEventHandlers(systemEventManager);

// Initialize system queue
async function initializeSystemQueue() {
  try {
    // Direct database operation to create system queue
    await pool.query(`
      INSERT INTO queen.queues (name, ttl, priority, max_queue_size)
      VALUES ($1, 300, 100, 10000)
      ON CONFLICT (name) DO UPDATE SET
        ttl = EXCLUDED.ttl,
        priority = EXCLUDED.priority,
        max_queue_size = EXCLUDED.max_queue_size
    `, [SYSTEM_QUEUE]);
    
    await pool.query(`
      INSERT INTO queen.partitions (queue_id, name)
      SELECT id, 'Default' FROM queen.queues WHERE name = $1
      ON CONFLICT (queue_id, name) DO NOTHING
    `, [SYSTEM_QUEUE]);
  } catch (error) {
    console.error('Failed to initialize system queue:', error);
    throw error;
  }
}

// Startup sequence
async function startServer() {
  console.log('Starting Queen server...');
  
  // 1. Initialize database connection
  await testDatabaseConnection();
  
  // 2. Initialize system queue
  await initializeSystemQueue();
  
  // 3. Create temporary client for synchronization
  const syncClient = createQueenClient({ 
    baseUrl: `http://localhost:${PORT}` 
  });
  
  // 4. Synchronize system events (catch up on missed events)
  await syncSystemEvents(syncClient, systemEventManager);
  
  // 5. Start consuming system events
  const systemEventConsumer = syncClient.consume({
    queue: SYSTEM_QUEUE,
    consumerGroup: SERVER_INSTANCE_ID,
    handler: async (message) => {
      await systemEventManager.processSystemEvent(message.payload);
    },
    options: {
      batch: 10,
      wait: true
    }
  });
  
  // 6. Start HTTP server
  app.listen(PORT, '0.0.0.0', (listenSocket) => {
    if (listenSocket) {
      console.log(`Queen server running on port ${PORT}`);
      console.log(`Server Instance ID: ${SERVER_INSTANCE_ID}`);
    }
  });
}

// Graceful shutdown
process.on('SIGTERM', async () => {
  console.log('Shutting down...');
  systemEventConsumer?.stop();
  await pool.end();
  process.exit(0);
});
```

#### 3.2 Bypass Cache for System Queue
**File**: `src/managers/queueManagerOptimized.js` (modification)
```javascript
const ensureResources = async (client, queueName, partitionName = 'Default', namespace = null, task = null) => {
  // CRITICAL: System queues bypass cache completely
  if (queueName.startsWith('__system_')) {
    // Direct database query for system queues
    const result = await client.query(`
      SELECT 
        q.id as queue_id,
        q.name as queue_name,
        p.id as partition_id,
        q.*
      FROM queen.queues q
      JOIN queen.partitions p ON p.queue_id = q.id
      WHERE q.name = $1 AND p.name = $2
    `, [queueName, partitionName]);
    
    if (result.rows.length === 0) {
      throw new Error(`System queue ${queueName} not found`);
    }
    
    const row = result.rows[0];
    return {
      queueId: row.queue_id,
      queueName: row.queue_name,
      partitionId: row.partition_id,
      queueConfig: {
        leaseTime: row.lease_time,
        retryLimit: row.retry_limit,
        // ... other config fields
      },
      encryptionEnabled: false,  // System queues never encrypted
      maxWaitTimeSeconds: row.max_wait_time_seconds
    };
  }
  
  // ... existing cache logic for regular queues ...
};
```

## Configuration

### Environment Variables
```bash
# System event configuration
QUEEN_SYSTEM_EVENTS_ENABLED=true         # Enable system event propagation
QUEEN_SYSTEM_EVENTS_BATCH_MS=10          # Batching window for events
QUEEN_SYSTEM_EVENTS_SYNC_TIMEOUT=30000   # Timeout for startup synchronization

# Cache configuration (unchanged)
QUEEN_CACHE_TTL=60000                    # Cache TTL when events are enabled
```

## Monitoring

### Key Metrics
```javascript
// System event metrics
{
  systemEventsPublished: counter,
  systemEventsProcessed: counter,
  systemEventLag: gauge,
  startupSyncDuration: histogram,
  eventProcessingErrors: counter
}
```

### Health Checks
```javascript
// Add to health endpoint
async function checkSystemEventHealth() {
  const result = await pool.query(`
    SELECT COUNT(*) as pending
    FROM queen.messages m
    JOIN queen.partitions p ON m.partition_id = p.id
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE q.name = $1
      AND m.status = 'pending'
  `, [SYSTEM_QUEUE]);
  
  return {
    systemEventQueueDepth: result.rows[0].pending,
    healthy: result.rows[0].pending < 1000
  };
}
```

## Migration Plan

### Step 1: Deploy with Feature Flag Off
```bash
QUEEN_SYSTEM_EVENTS_ENABLED=false
```

### Step 2: Enable and Monitor
```bash
QUEEN_SYSTEM_EVENTS_ENABLED=true
QUEEN_CACHE_TTL=60000  # Re-enable caching
```

### Step 3: Optimize
- Increase cache TTL once confidence is established
- Tune batching windows based on event volume

## Rollback Plan

### Immediate Rollback
```bash
# Disable system events
QUEEN_SYSTEM_EVENTS_ENABLED=false
QUEEN_CACHE_TTL=0  # Disable cache
```

### Gradual Rollback
```bash
# Keep events but reduce cache TTL
QUEEN_SYSTEM_EVENTS_ENABLED=true
QUEEN_CACHE_TTL=5000  # 5 second cache
```

## Future Extensions

The event system can be extended for:
- **Audit Logging**: Track all configuration changes
- **Metrics Collection**: Aggregate queue statistics
- **Distributed Locks**: Coordinate operations across servers
- **Leader Election**: Designate primary server for certain operations
- **Schema Migrations**: Coordinate database updates
- **Feature Flags**: Propagate feature flag changes

## Success Criteria

- [ ] System events published for all queue operations
- [ ] All servers process events and update caches
- [ ] Startup synchronization completes in < 5 seconds
- [ ] No circular dependencies with system queue
- [ ] Cache consistency across all servers within 100ms