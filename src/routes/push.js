export const createPushRoute = (queueManager) => {
  return async (body) => {
    const { items = [], config = {} } = body;
    
    if (!items || items.length === 0) {
      throw new Error('No items to push');
    }
    
    // Validate items
    for (const item of items) {
      if (!item.ns || !item.task || !item.queue) {
        throw new Error('Each item must have ns, task, and queue');
      }
      if (!item.payload) {
        throw new Error('Each item must have a payload');
      }
    }
    
    const results = await queueManager.pushMessages(items);
    
    return {
      messages: results
    };
  };
};
