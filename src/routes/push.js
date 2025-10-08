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
      if (!item.payload) {
        throw new Error('Each item must have a payload');
      }
      // partition is optional, defaults to "Default"
    }
    
    const results = await queueManager.pushMessages(items);
    
    return {
      messages: results
    };
  };
};
