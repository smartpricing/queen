export const createPushRoute = (queueManager) => {
  return async (body) => {
    const { items = [], config = {} } = body;
    
    if (!items || items.length === 0) {
      throw new Error('No items to push');
    }
    
    // Validate items
    for (const item of items) {
      if (!item.queue) {
        throw new Error('Each item must have a queue');
      }
      // Allow missing, null or undefined payloads - they'll be stored as null in JSONB
      // If payload is not provided, set it to null
      if (!('payload' in item)) {
        item.payload = null;
      }
      // partition is optional, defaults to "Default"
    }
    
    const results = await queueManager.pushMessages(items);
    
    return {
      messages: results
    };
  };
};
