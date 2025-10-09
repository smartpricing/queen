export const createConfigureRoute = (queueManager) => {
  return async (body) => {
    const { queue, partition, namespace, task, options = {} } = body;
    
    if (!queue) {
      throw new Error('queue is required');
    }
    
    // Note: partition parameter is now ignored but still accepted for backward compatibility
    // All configuration is at queue level now
    
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
      windowBuffer: options.windowBuffer || 0,
      retentionSeconds: options.retentionSeconds || 0,
      completedRetentionSeconds: options.completedRetentionSeconds || 0,
      retentionEnabled: options.retentionEnabled || false,
      encryptionEnabled: options.encryptionEnabled,
      maxWaitTimeSeconds: options.maxWaitTimeSeconds
    };
    
    // Call the new queue-only configuration method
    const result = await queueManager.configureQueue(queue, validOptions, namespace, task);
    
    return {
      queue,
      namespace: result.namespace,
      task: result.task,
      configured: true,
      options: result.options,
      // Include partition in response for backward compatibility but mark as deprecated
      ...(partition && { 
        partition,
        _deprecation_notice: 'Partition-level configuration is deprecated. All configuration is now at queue level.'
      })
    };
  };
};