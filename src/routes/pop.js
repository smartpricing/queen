export const createPopRoute = (queueManager, eventManager) => {
  return async (scope, options = {}) => {
    const { wait = false, timeout = 30000, batch = 1 } = options;
    const maxTimeout = 60000;
    const effectiveTimeout = Math.min(timeout, maxTimeout);
    
    // Try to get messages immediately
    let messages = await queueManager.popMessages(scope, { batch });
    
    if (messages.length > 0 || !wait) {
      return { messages };
    }
    
    // Long polling with event-driven optimization
    const queuePath = scope.queue 
      ? `${scope.ns}/${scope.task}/${scope.queue}`
      : scope.task
      ? `${scope.ns}/${scope.task}`
      : scope.ns;
    
    const startTime = Date.now();
    
    while (Date.now() - startTime < effectiveTimeout) {
      // Wait for message notification or timeout
      const notification = await Promise.race([
        eventManager.waitForMessage(queuePath, 1000),
        new Promise(resolve => setTimeout(() => resolve(null), 1000))
      ]);
      
      // Try to get messages whether notified or not
      messages = await queueManager.popMessages(scope, { batch });
      if (messages.length > 0) {
        return { messages };
      }
      
      // If we got a notification but no messages, someone else got them
      // Continue waiting
    }
    
    // Timeout reached, no messages
    return { messages: [] };
  };
};
