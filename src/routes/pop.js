export const createPopRoute = (queueManager, eventManager) => {
  return async (scope, options = {}) => {
    const { wait = false, timeout = 30000, batch = 1 } = options;
    const maxTimeout = 60000;
    const effectiveTimeout = Math.min(timeout, maxTimeout);
    
    // Try to get messages immediately (pass all options including wait)
    let result = await queueManager.popMessages(scope, { batch, wait: false, timeout: effectiveTimeout });
    
    if (result.messages.length > 0 || !wait) {
      return result;
    }
    
    // Long polling with event-driven optimization
    const queuePath = scope.queue 
      ? `${scope.ns}/${scope.task}/${scope.queue}`
      : scope.task
      ? `${scope.ns}/${scope.task}`
      : scope.ns;
    
    const startTime = Date.now();
    
    // Optimized polling with shorter intervals for better responsiveness
    const pollInterval = Math.min(100, effectiveTimeout / 10); // 100ms max, or 1/10th of timeout
    
    while (Date.now() - startTime < effectiveTimeout) {
      // Wait for message notification or short timeout for faster response
      const notification = await Promise.race([
        eventManager.waitForMessage(queuePath, pollInterval),
        new Promise(resolve => setTimeout(() => resolve(null), pollInterval))
      ]);
      
      // Try to get messages whether notified or not
      result = await queueManager.popMessages(scope, { batch, wait: false });
      if (result.messages.length > 0) {
        return result;
      }
      
      // If we got a notification but no messages, someone else got them
      // Continue waiting with minimal delay
    }
    
    // Timeout reached, no messages
    return { messages: [] };
  };
};
