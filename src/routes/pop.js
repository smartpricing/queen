export const createPopRoute = (queueManager, eventManager) => {
  return async (scope, options = {}) => {
    const { wait = false, timeout = 30000, batch = 1 } = options;
    const maxTimeout = 60000;
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
      const pollInterval = Math.min(1000, effectiveTimeout / 10);
      
      while (Date.now() - startTime < effectiveTimeout) {
        await new Promise(resolve => setTimeout(resolve, pollInterval));
        result = await queueManager.popMessagesWithFilters(scope, { batch, wait: false });
        if (result.messages.length > 0) {
          return result;
        }
      }
      
      return { messages: [] };
    }
    
    // Regular queue/partition pop
    let result = await queueManager.popMessages(scope, { batch, wait: false, timeout: effectiveTimeout });
    
    if (result.messages.length > 0 || !wait) {
      return result;
    }
    
    // Long polling with event-driven optimization
    const queuePath = scope.partition 
      ? `${scope.queue}/${scope.partition}`
      : scope.queue;
    
    const startTime = Date.now();
    const pollInterval = Math.min(100, effectiveTimeout / 10);
    
    while (Date.now() - startTime < effectiveTimeout) {
      // Wait for message notification or short timeout
      const notification = await Promise.race([
        eventManager.waitForMessage(queuePath, pollInterval),
        new Promise(resolve => setTimeout(() => resolve(null), pollInterval))
      ]);
      
      // Try to get messages whether notified or not
      result = await queueManager.popMessages(scope, { batch, wait: false });
      if (result.messages.length > 0) {
        return result;
      }
    }
    
    // Timeout reached, no messages
    return { messages: [] };
  };
};
