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
    
    // Handle different pop scenarios
    if (scope.namespace || scope.task) {
      // Pop with filters (namespace or task)
      let result = await queueManager.popMessagesWithFilters(scope, { batch, wait: false, timeout: effectiveTimeout });
      
      if (result.messages.length > 0 || !wait) {
        return result;
      }
      
      // Simple polling for filtered pops (no specific queue path to wait on)
      const startTime = Date.now();
      const pollInterval = Math.min(config.QUEUE.POLL_INTERVAL_FILTERED, effectiveTimeout / 10);
      
      while (Date.now() - startTime < effectiveTimeout) {
        await new Promise(resolve => setTimeout(resolve, pollInterval));
        result = await queueManager.popMessagesWithFilters(scope, { batch, wait: false });
        if (result.messages.length > 0) {
          return result;
        }
      }
      
      return { messages: [] };
    }
    
    // Regular queue/partition pop (now with consumer group support)
    const popScope = {
      ...scope,
      consumerGroup: consumerGroup || scope.consumerGroup
    };
    
    const popOptions = {
      batch, 
      wait: false, 
      timeout: effectiveTimeout,
      subscriptionMode,
      subscriptionFrom
    };
    
    let result = await queueManager.popMessages(popScope, popOptions);
    
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
      result = await queueManager.popMessages(popScope, popOptions);
      if (result.messages.length > 0) {
        return result;
      }
    }
    
    // Timeout reached, no messages
    return { messages: [] };
  };
};
