# Queen V2 Architecture Migration Plan

## Overview
Restructuring the queue system from a rigid 3-tier hierarchy (Namespaces → Tasks → Queues) to a flexible 2-tier system (Queues → Partitions) with optional grouping fields.

## New Data Model

### Core Structure
```
Queues (with optional namespace/task grouping)
  └── Partitions (FIFO ordering)
       └── Messages
```

### Key Changes
1. **Remove** `namespaces` table
2. **Rename** `tasks` table → `queues` table
3. **Rename** `queues` table → `partitions` table
4. **Add** optional `namespace` and `task` fields to `queues` table for grouping
5. **Default Partition**: Each queue automatically gets a "Default" partition

## Database Schema Changes

### 1. Update schema.sql
- Drop the `namespaces` table
- Rename and restructure tables:
  ```sql
  -- New queues table (was tasks)
  CREATE TABLE queen.queues (
    id UUID PRIMARY KEY,
    name VARCHAR(255) UNIQUE NOT NULL,
    namespace VARCHAR(255), -- optional grouping
    task VARCHAR(255),      -- optional grouping
    created_at TIMESTAMP
  );
  
  -- New partitions table (was queues)
  CREATE TABLE queen.partitions (
    id UUID PRIMARY KEY,
    queue_id UUID REFERENCES queen.queues(id),
    name VARCHAR(255) NOT NULL DEFAULT 'Default',
    priority INTEGER DEFAULT 0,
    options JSONB,
    created_at TIMESTAMP,
    UNIQUE(queue_id, name)
  );
  
  -- Messages table stays similar, references partitions
  CREATE TABLE queen.messages (
    id UUID PRIMARY KEY,
    partition_id UUID REFERENCES queen.partitions(id),
    -- rest stays the same
  );
  ```

### 2. Update Indexes
- Adjust all indexes to work with new structure
- Add indexes for optional namespace/task fields on queues table
- Maintain performance-critical indexes on messages table

## API Route Changes

### Push Routes
**Current**: `POST /api/v1/push`
```json
{
  "items": [{
    "ns": "namespace",
    "task": "task", 
    "queue": "queue",
    "payload": {}
  }]
}
```

**New**: `POST /api/v1/push`
```json
{
  "items": [{
    "queue": "queue",
    "partition": "partition", // optional, defaults to "Default"
    "payload": {}
  }]
}
```

### Pop Routes
**Current**:
- `/api/v1/pop/ns/:ns/task/:task/queue/:queue`
- `/api/v1/pop/ns/:ns/task/:task`
- `/api/v1/pop/ns/:ns`

**New**:
- `/api/v1/pop/queue/:queue/partition/:partition` - Pop from specific partition
- `/api/v1/pop/queue/:queue` - Pop from any available partition in queue
- `/api/v1/pop?namespace=:ns` - Pop from any queue in namespace (optional filter)
- `/api/v1/pop?task=:task` - Pop from any queue in task (optional filter)

### Configure Routes
**Current**: `POST /api/v1/configure`
```json
{
  "ns": "namespace",
  "task": "task",
  "queue": "queue",
  "options": {}
}
```

**New**: `POST /api/v1/configure`
```json
{
  "queue": "queue",
  "partition": "partition", // optional, defaults to "Default"
  "options": {}
}
```

### Analytics Routes
**Current**:
- `/api/v1/analytics/ns/:ns`
- `/api/v1/analytics/ns/:ns/task/:task`

**New**:
- `/api/v1/analytics/queue/:queue`
- `/api/v1/analytics?namespace=:ns` - Filter by namespace
- `/api/v1/analytics?task=:task` - Filter by task

## Code Changes Required

### 1. Database Layer (`src/database/`)
- [ ] Rewrite `schema.sql` with new structure
- [ ] Update `connection.js` initialization if needed

### 2. Manager Classes (`src/managers/`)
- [ ] Update `queueManagerOptimized.js`:
  - [ ] Change `ensureResources()` to handle queue → partition structure
  - [ ] Update `pushMessages()` to default to "Default" partition
  - [ ] Modify `popMessages()` for new partition logic
  - [ ] Adjust `getQueueStats()` for new structure
- [ ] Update `resourceCache.js`:
  - [ ] Change cache key format from `ns:task:queue` to `queue:partition`
- [ ] Update `eventManager.js`:
  - [ ] Adjust event paths from `ns/task/queue` to `queue/partition`

### 3. Route Handlers (`src/routes/`)
- [ ] Update `push.js`:
  - [ ] Remove ns/task requirements
  - [ ] Add partition defaulting logic
- [ ] Update `pop.js`:
  - [ ] New route parameter structure
  - [ ] Implement "any available partition" logic
- [ ] Update `configure.js`:
  - [ ] Work with queue/partition instead of ns/task/queue
- [ ] Update `analytics.js`:
  - [ ] New aggregation logic based on optional namespace/task fields
- [ ] Update `messages.js`:
  - [ ] Adjust queries for new structure

### 4. Server (`src/server.js`)
- [ ] Update all route definitions
- [ ] Adjust URL patterns
- [ ] Update WebSocket event paths

### 5. Client Library (`src/client/`)
- [ ] Update `queenClient.js` with new API structure
- [ ] Adjust method signatures

### 6. WebSocket (`src/websocket/`)
- [ ] Update event naming and paths
- [ ] Adjust queue depth updates

## Implementation Order

1. **Database Schema**
   - Create new `schema-v2.sql`
   - Test schema independently

2. **Core Manager Updates**
   - Update `queueManagerOptimized.js` with new logic
   - Update resource caching
   - Update event manager

3. **Route Handlers**
   - Update each route file for new structure
   - Maintain backwards compatibility temporarily if needed

4. **Server Integration**
   - Update `server.js` with new routes
   - Test each endpoint

5. **Client Updates**
   - Update client library
   - Update examples

6. **Frontend Updates**
   - Update API calls in frontend
   - Adjust data structures

## Partition Selection Logic

When popping from a queue (without specifying partition):
1. Select the partition with the oldest pending message
2. Use `ORDER BY created_at ASC` across partitions
3. Maintain FIFO within each partition
4. Use `FOR UPDATE SKIP LOCKED` for concurrency

## Default Partition Behavior

1. **Auto-creation**: When a queue is created, automatically create a "Default" partition
2. **Push behavior**: If no partition specified, use "Default"
3. **Pop behavior**: Include "Default" when popping from queue level

## Node
Always use "nvm use 22 && your node.js command"

## Testing Checklist

- [ ] Push to queue with no partition (should use Default)
- [ ] Push to specific partition
- [ ] Pop from specific partition
- [ ] Pop from queue (any partition)
- [ ] Queue configuration
- [ ] Partition configuration
- [ ] Analytics with namespace filtering
- [ ] Analytics with task filtering
- [ ] Message acknowledgment
- [ ] Batch operations
- [ ] WebSocket events
- [ ] Long polling
- [ ] Lease expiration
- [ ] Performance with multiple partitions

## Migration Notes

- No data migration needed (starting fresh)
