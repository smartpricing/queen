// In-memory cache for resource existence to avoid repeated DB lookups
export const createResourceCache = () => {
  const cache = new Map();
  const TTL = 60000; // 1 minute TTL
  
  const getCacheKey = (queue, partition) => `${queue}:${partition || 'Default'}`;
  
  const checkResource = (queue, partition) => {
    const key = getCacheKey(queue, partition);
    const cached = cache.get(key);
    
    if (cached && Date.now() - cached.timestamp < TTL) {
      return cached.data;
    }
    
    return null; // Not in cache
  };
  
  const cacheResource = (queue, partition, data) => {
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
  
  // Cleanup old entries periodically
  setInterval(() => {
    const now = Date.now();
    for (const [key, value] of cache.entries()) {
      if (now - value.timestamp > TTL) {
        cache.delete(key);
      }
    }
  }, TTL);
  
  return {
    checkResource,
    cacheResource,
    invalidate,
    invalidateQueue
  };
};
