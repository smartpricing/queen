export const createAckRoute = (queueManager) => {
  return async (body) => {
    const { transactionId, status, error, consumerGroup } = body;
    
    if (!transactionId) {
      throw new Error('transactionId is required');
    }
    
    if (!status || !['completed', 'failed'].includes(status)) {
      throw new Error('status must be "completed" or "failed"');
    }
    
    const result = await queueManager.acknowledgeMessage(transactionId, status, error, consumerGroup);
    
    if (!result) {
      throw new Error('Message not found or not in processing state');
    }
    
    return {
      transactionId: result.transaction_id,
      status: result.status,
      consumerGroup: consumerGroup || null,
      acknowledgedAt: new Date().toISOString()
    };
  };
};
