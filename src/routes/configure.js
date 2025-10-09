export const createConfigureRoute = (queueManager) => {
  return async (body) => {
    const { queue, partition, namespace, task, options = {} } = body;
    
    if (!queue) {
      throw new Error('queue is required');
    }
    
    if (partition) {
      // Partition specified - configure partition with options
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
        // New retention options
        retentionSeconds: options.retentionSeconds || 0,
        completedRetentionSeconds: options.completedRetentionSeconds || 0,
        partitionRetentionSeconds: options.partitionRetentionSeconds || 0,
        retentionEnabled: options.retentionEnabled || false,
        // Queue-level options passed through
        encryptionEnabled: options.encryptionEnabled,
        maxWaitTimeSeconds: options.maxWaitTimeSeconds
      };
      
      const result = await queueManager.configureQueue(queue, partition, validOptions, namespace, task);
      
      return {
        queue,
        partition,
        namespace,
        task,
        configured: true,
        options: result.options
      };
    } else {
      // No partition specified - only configure queue-level settings
      const result = await queueManager.configureQueueOnly(queue, namespace, task, {
        encryptionEnabled: options.encryptionEnabled,
        maxWaitTimeSeconds: options.maxWaitTimeSeconds
      });
      
      return {
        queue,
        namespace,
        task,
        configured: true,
        options: {
          encryptionEnabled: options.encryptionEnabled,
          maxWaitTimeSeconds: options.maxWaitTimeSeconds
        }
      };
    }
  };
};
