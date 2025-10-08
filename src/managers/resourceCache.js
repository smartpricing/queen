// In-memory cache for resource existence to avoid repeated DB lookups
export const createResourceCache = () => {
  const cache = new Map();
  const TTL = 60000; // 1 minute TTL
  
  const getCacheKey = (queue, partition) => `${queue}:${partition || 'Default'}`;
  
  const checkResource = (queue, partition) => {
    const key = getCacheKey(queue, partition);
    const cached = cache.get(key);
    
    if (cached && Date.now() - cached.timestamp < TTL) {
      return cached.exists;
    }
    
    return null; // Not in cache
  };
  
  const cacheResource = (queue, partition, exists = true) => {
    const key = getCacheKey(queue, partition);
    cache.set(key, { exists, timestamp: Date.now() });
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
    invalidate
  };
};
