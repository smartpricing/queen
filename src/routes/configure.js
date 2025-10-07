export const createConfigureRoute = (queueManager) => {
  return async (body) => {
    const { ns, task, queue, options = {} } = body;
    
    if (!ns || !task || !queue) {
      throw new Error('ns, task, and queue are required');
    }
    
    // Validate options
    const validOptions = {
      leaseTime: options.leaseTime || 300,
      maxSize: options.maxSize || 10000,
      ttl: options.ttl || 3600,
      retryLimit: options.retryLimit || 3,
      retryDelay: options.retryDelay || 1000,
      deadLetterQueue: options.deadLetterQueue || false,
      priority: options.priority || 0,
      delayedProcessing: options.delayedProcessing || 0,
      windowBuffer: options.windowBuffer || 0
    };
    
    const result = await queueManager.configureQueue(ns, task, queue, validOptions);
    
    return {
      queue: `${ns}/${task}/${queue}`,
      configured: true,
      options: result.options
    };
  };
};
