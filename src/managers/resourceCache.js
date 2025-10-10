import { EventTypes } from './systemEventManager.js';

// In-memory cache for resource existence to avoid repeated DB lookups
export const createResourceCache = () => {
  const cache = new Map();
  // Make TTL configurable - set to 0 to disable caching in multi-server setups
  const TTL = process.env.QUEEN_CACHE_TTL ? parseInt(process.env.QUEEN_CACHE_TTL) : 60000; // Default 1 minute
  
  const getCacheKey = (queue, partition) => `${queue}:${partition || 'Default'}`;
  
  const checkResource = (queue, partition) => {
    // If TTL is 0, caching is disabled
    if (TTL === 0) return null;
    
    const key = getCacheKey(queue, partition);
    const cached = cache.get(key);
    
    if (cached && Date.now() - cached.timestamp < TTL) {
      return cached.data;
    }
    
    return null; // Not in cache
  };
  
  const cacheResource = (queue, partition, data) => {
    // If TTL is 0, caching is disabled
    if (TTL === 0) return;
    
    const key = getCacheKey(queue, partition);
    cache.set(key, { data, timestamp: Date.now() });
  };
  
  const invalidate = (queue, partition) => {
    if (queue && partition) {
      cache.delete(getCacheKey(queue, partition));
    } else if (queue) {
      // Clear all partitions for this queue
      for (const key of cache.keys()) {
        if (key.startsWith(`${queue}:`)) {
          cache.delete(key);
        }
      }
    } else {
      cache.clear(); // Clear all if no specific resource
    }
  };
  
  const invalidateQueue = (queue) => {
    // Clear all partitions for this queue when queue config changes
    for (const key of cache.keys()) {
      if (key.startsWith(`${queue}:`)) {
        cache.delete(key);
      }
    }
  };
  
  // Cleanup old entries periodically (only if caching is enabled)
  if (TTL > 0) {
    setInterval(() => {
      const now = Date.now();
      for (const [key, value] of cache.entries()) {
        if (now - value.timestamp > TTL) {
          cache.delete(key);
        }
      }
    }, Math.min(TTL, 60000)); // Check at most once per minute
  }
  
  // Register event handlers for cache invalidation
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
    checkResource,
    cacheResource,
    invalidate,
    invalidateQueue,
    registerEventHandlers
  };
};
