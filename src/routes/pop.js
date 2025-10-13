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
    
    // Long polling with event-driven optimization and exponential backoff
    const queuePath = scope.partition 
      ? `${scope.queue}/${scope.partition}`
      : scope.queue;
    
    const startTime = Date.now();
    const initialPollInterval = Math.min(config.QUEUE.POLL_INTERVAL, effectiveTimeout / 10);
    const maxPollInterval = config.QUEUE.MAX_POLL_INTERVAL || 2000; // Default 2 seconds
    const backoffThreshold = config.QUEUE.BACKOFF_THRESHOLD || 5; // Retries before backoff
    const backoffMultiplier = config.QUEUE.BACKOFF_MULTIPLIER || 2;
    
    let currentPollInterval = initialPollInterval;
    let consecutiveEmptyPolls = 0;
    
    while (Date.now() - startTime < effectiveTimeout) {
      // Wait for message notification or current poll interval
      const notification = await Promise.race([
        eventManager.waitForMessage(queuePath, currentPollInterval),
        new Promise(resolve => setTimeout(() => resolve(null), currentPollInterval))
      ]);
      
      // Try to get messages whether notified or not
      result = await queueManager.uniquePop(scope, { batch, wait: false, subscriptionMode, subscriptionFrom });
      if (result.messages.length > 0) {
        return result;
      }
      
      // Implement exponential backoff after threshold
      consecutiveEmptyPolls++;
      if (consecutiveEmptyPolls >= backoffThreshold) {
        currentPollInterval = Math.min(
          currentPollInterval * backoffMultiplier,
          maxPollInterval
        );
      }
    }
    
    // Timeout reached, no messages
    return { messages: [] };
  };
};
