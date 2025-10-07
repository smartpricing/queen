// In-memory cache for resource existence to avoid repeated DB lookups
export const createResourceCache = () => {
  const cache = new Map();
  const TTL = 60000; // 1 minute TTL
  
  const getCacheKey = (ns, task, queue) => `${ns}:${task}:${queue}`;
  
  const checkResource = (ns, task, queue) => {
    const key = getCacheKey(ns, task, queue);
    const cached = cache.get(key);
    
    if (cached && Date.now() - cached.timestamp < TTL) {
      return cached.exists;
    }
    
    return null; // Not in cache
  };
  
  const cacheResource = (ns, task, queue, exists = true) => {
    const key = getCacheKey(ns, task, queue);
    cache.set(key, { exists, timestamp: Date.now() });
  };
  
  const invalidate = (ns, task, queue) => {
    if (ns && task && queue) {
      cache.delete(getCacheKey(ns, task, queue));
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
