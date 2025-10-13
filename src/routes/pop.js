import config from '../config.js';

export const createPopRoute = (queueManager, eventManager) => {
  return async (scope, options = {}) => {
    const { 
      wait = false, 
      timeout = config.QUEUE.DEFAULT_TIMEOUT, 
      batch = config.QUEUE.DEFAULT_BATCH_SIZE,
      consumerGroup = scope.consumerGroup || null,
      subscriptionMode = scope.subscriptionMode || null,
      subscriptionFrom = scope.subscriptionFrom || null
    } = options;
    const maxTimeout = config.QUEUE.MAX_TIMEOUT;
    const effectiveTimeout = Math.min(timeout, maxTimeout);
    
    // ALWAYS USE UNIFIED POP - handles all 3 modes (direct, queue-level, filtered)
    let result = await queueManager.uniquePop(scope, { 
      batch, 
      wait: false, 
      timeout: effectiveTimeout, 
      subscriptionMode, 
      subscriptionFrom 
    });
    
    if (result.messages.length > 0 || !wait) {
      return result;
    }
    
    // Long polling with event-driven optimization
    const queuePath = scope.partition 
      ? `${scope.queue}/${scope.partition}`
      : scope.queue;
    
    const startTime = Date.now();
    const pollInterval = Math.min(config.QUEUE.POLL_INTERVAL, effectiveTimeout / 10);
    
    while (Date.now() - startTime < effectiveTimeout) {
      // Wait for message notification or short timeout
      const notification = await Promise.race([
        eventManager.waitForMessage(queuePath, pollInterval),
        new Promise(resolve => setTimeout(() => resolve(null), pollInterval))
      ]);
      
      // Try to get messages whether notified or not
      result = await queueManager.uniquePop(scope, { batch, wait: false, subscriptionMode, subscriptionFrom });
      if (result.messages.length > 0) {
        return result;
      }
    }
    
    // Timeout reached, no messages
    return { messages: [] };
  };
};
