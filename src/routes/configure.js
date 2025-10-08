export const createConfigureRoute = (queueManager) => {
  return async (body) => {
    const { queue, partition, options = {} } = body;
    
    if (!queue) {
      throw new Error('queue is required');
    }
    
    // partition is optional, defaults to "Default"
    const partitionName = partition || 'Default';
    
    // Validate options
    const validOptions = {
      leaseTime: options.leaseTime || 300,
      maxSize: options.maxSize || 10000,
      ttl: options.ttl || 3600,
      retryLimit: options.retryLimit || 3,
      retryDelay: options.retryDelay || 1000,
      deadLetterQueue: options.deadLetterQueue || false,
      dlqAfterMaxRetries: options.dlqAfterMaxRetries || false,
      priority: options.priority || 0,
      delayedProcessing: options.delayedProcessing || 0,
      windowBuffer: options.windowBuffer || 0
    };
    
    const result = await queueManager.configureQueue(queue, partitionName, validOptions);
    
    return {
      queue,
      partition: partitionName,
      configured: true,
      options: result.options
    };
  };
};
