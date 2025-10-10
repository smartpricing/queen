# Multi-Server Cache Consistency Solutions

## Problem
When running multiple Queen servers, each server maintains its own in-memory resource cache with a 1-minute TTL. Configuration changes on one server are not reflected on other servers until the cache expires.

## Solutions

### 1. **Disable Cache in Multi-Server Mode** (Quick Fix)
Set cache TTL to 0 or very short duration when running multiple servers.

```javascript
// In src/managers/resourceCache.js
const TTL = process.env.QUEEN_CACHE_TTL ? parseInt(process.env.QUEEN_CACHE_TTL) : 60000;
```

Run servers with:
```bash
QUEEN_CACHE_TTL=0 PORT=6632 node src/server.js
QUEEN_CACHE_TTL=0 PORT=6633 node src/server.js
```

### 2. **Cache Invalidation via Database Triggers** (Recommended)
Add a `cache_invalidation` table and use database triggers to notify servers of changes.

```sql
-- Add to schema
CREATE TABLE queen.cache_invalidations (
    id SERIAL PRIMARY KEY,
    resource_type VARCHAR(50) NOT NULL,
    resource_key VARCHAR(255) NOT NULL,
    invalidated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_cache_invalidations_time ON queen.cache_invalidations(invalidated_at);

-- Trigger on queue updates
CREATE OR REPLACE FUNCTION queen.notify_cache_invalidation()
RETURNS TRIGGER AS $$
BEGIN
    INSERT INTO queen.cache_invalidations (resource_type, resource_key)
    VALUES ('queue', NEW.name);
    
    PERFORM pg_notify('cache_invalidation', json_build_object(
        'type', 'queue',
        'key', NEW.name
    )::text);
    
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER queue_cache_invalidation
AFTER UPDATE ON queen.queues
FOR EACH ROW
EXECUTE FUNCTION queen.notify_cache_invalidation();
```

### 3. **Redis-Based Distributed Cache** (Best for Scale)
Replace in-memory cache with Redis for shared caching across servers.

```javascript
// src/managers/redisResourceCache.js
import Redis from 'ioredis';

export const createRedisResourceCache = (redisConfig) => {
  const redis = new Redis(redisConfig);
  const TTL = 60; // seconds
  
  const getCacheKey = (queue, partition) => 
    `queen:resource:${queue}:${partition || 'Default'}`;
  
  const checkResource = async (queue, partition) => {
    const key = getCacheKey(queue, partition);
    const cached = await redis.get(key);
    return cached ? JSON.parse(cached) : null;
  };
  
  const cacheResource = async (queue, partition, data) => {
    const key = getCacheKey(queue, partition);
    await redis.setex(key, TTL, JSON.stringify(data));
  };
  
  const invalidate = async (queue, partition) => {
    if (queue && partition) {
      await redis.del(getCacheKey(queue, partition));
    } else if (queue) {
      const keys = await redis.keys(`queen:resource:${queue}:*`);
      if (keys.length > 0) {
        await redis.del(...keys);
      }
    } else {
      const keys = await redis.keys('queen:resource:*');
      if (keys.length > 0) {
        await redis.del(...keys);
      }
    }
  };
  
  return {
    checkResource,
    cacheResource,
    invalidate,
    invalidateQueue: (queue) => invalidate(queue)
  };
};
```

### 4. **Polling-Based Cache Invalidation** (Simple)
Each server periodically checks for configuration changes.

```javascript
// Add to resourceCache.js
const pollForChanges = async (pool) => {
  const result = await pool.query(`
    SELECT name, updated_at 
    FROM queen.queues 
    WHERE updated_at > $1
  `, [lastCheckTime]);
  
  for (const row of result.rows) {
    invalidateQueue(row.name);
  }
  
  lastCheckTime = new Date();
};

// Poll every 5 seconds
setInterval(() => pollForChanges(pool), 5000);
```

### 5. **Event-Based Invalidation via Message Queue** 
Use the Queen queue itself to propagate cache invalidation events.

```javascript
// When configuration changes on one server
await client.push({
  items: [{
    queue: '_system_cache_invalidation',
    partition: 'Default',
    payload: {
      action: 'invalidate',
      resourceType: 'queue',
      resourceKey: queueName,
      timestamp: Date.now()
    }
  }]
});

// All servers consume from this queue
client.consume({
  queue: '_system_cache_invalidation',
  handler: async (message) => {
    const { resourceType, resourceKey } = message.payload;
    if (resourceType === 'queue') {
      resourceCache.invalidateQueue(resourceKey);
    }
  }
});
```

## Recommended Approach

For your immediate testing needs, use **Solution 1** (disable cache):
```bash
# Start servers without cache
QUEEN_CACHE_TTL=0 PORT=6632 node src/server.js
QUEEN_CACHE_TTL=0 PORT=6633 node src/server.js
```

For production multi-server deployment:
- **Small scale (2-3 servers)**: Solution 2 (Database triggers with LISTEN/NOTIFY)
- **Medium scale (4-10 servers)**: Solution 3 (Redis cache)
- **Large scale (10+ servers)**: Solution 3 (Redis) + Solution 5 (Event-based)

## Testing Cache Behavior

After implementing a solution, run:
```bash
node examples/test-cache-multi-server.js
```

The test should show:
- Both servers returning the same messages ✅
- Configuration changes immediately visible on both servers ✅
- No cache inconsistency warnings ✅
