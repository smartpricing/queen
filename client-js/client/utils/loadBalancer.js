/**
 * Load balancer utility for distributing requests across multiple servers
 */

export const LoadBalancingStrategy = {
  ROUND_ROBIN: 'ROUND_ROBIN',
  SESSION: 'SESSION'
};

/**
 * Create a load balancer for distributing requests across multiple base URLs
 */
export const createLoadBalancer = (urls, strategy = LoadBalancingStrategy.ROUND_ROBIN) => {
  if (!Array.isArray(urls) || urls.length === 0) {
    throw new Error('URLs must be a non-empty array');
  }
  
  // Normalize URLs (remove trailing slashes)
  const normalizedUrls = urls.map(url => url.replace(/\/$/, ''));
  
  // Round robin state
  let currentIndex = 0;
  
  // Session affinity state - map of session ID to URL index
  const sessionMap = new Map();
  
  // Generate a session ID for the current client instance
  const sessionId = generateSessionId();
  
  /**
   * Get the next URL based on the configured strategy
   */
  const getNextUrl = (options = {}) => {
    const { sessionKey = sessionId } = options;
    
    switch (strategy) {
      case LoadBalancingStrategy.ROUND_ROBIN:
        // Round robin: cycle through URLs
        const url = normalizedUrls[currentIndex];
        currentIndex = (currentIndex + 1) % normalizedUrls.length;
        return url;
        
      case LoadBalancingStrategy.SESSION:
        // Session affinity: stick to the same server per session
        if (!sessionMap.has(sessionKey)) {
          // Assign a server to this session (using round robin for initial assignment)
          const assignedIndex = currentIndex;
          sessionMap.set(sessionKey, assignedIndex);
          currentIndex = (currentIndex + 1) % normalizedUrls.length;
        }
        return normalizedUrls[sessionMap.get(sessionKey)];
        
      default:
        throw new Error(`Unknown load balancing strategy: ${strategy}`);
    }
  };
  
  /**
   * Get all configured URLs
   */
  const getAllUrls = () => [...normalizedUrls];
  
  /**
   * Get current strategy
   */
  const getStrategy = () => strategy;
  
  /**
   * Reset the load balancer state
   */
  const reset = () => {
    currentIndex = 0;
    sessionMap.clear();
  };
  
  /**
   * Get statistics about URL usage (for debugging)
   */
  const getStats = () => {
    return {
      strategy,
      urls: normalizedUrls,
      currentIndex,
      sessionCount: sessionMap.size,
      sessions: Array.from(sessionMap.entries()).map(([key, index]) => ({
        sessionKey: key,
        url: normalizedUrls[index]
      }))
    };
  };
  
  return {
    getNextUrl,
    getAllUrls,
    getStrategy,
    reset,
    getStats
  };
};

/**
 * Generate a unique session ID
 */
const generateSessionId = () => {
  return `session_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
};

/**
 * Create a failover wrapper that tries different servers on failure
 */
export const createFailoverWrapper = (loadBalancer, maxRetries = 3) => {
  const attemptedUrls = new Set();
  
  const executeWithFailover = async (requestFn, options = {}) => {
    const urls = loadBalancer.getAllUrls();
    let lastError = null;
    
    // Try each URL up to maxRetries times total
    for (let attempt = 0; attempt < Math.min(maxRetries, urls.length); attempt++) {
      const url = loadBalancer.getNextUrl(options);
      
      // Skip if we've already tried this URL in this request
      if (attemptedUrls.has(url)) {
        continue;
      }
      
      attemptedUrls.add(url);
      
      try {
        const result = await requestFn(url);
        attemptedUrls.clear(); // Clear for next request
        return result;
      } catch (error) {
        lastError = error;
        console.warn(`Request failed for ${url}, attempting next server...`, error.message);
        
        // If it's a client error (4xx), don't retry on other servers
        if (error.status && error.status >= 400 && error.status < 500) {
          attemptedUrls.clear();
          throw error;
        }
      }
    }
    
    attemptedUrls.clear();
    throw lastError || new Error('All servers failed');
  };
  
  return {
    executeWithFailover
  };
};
