import apiClient from './client';

export const consumersApi = {
  // Get consumer groups by querying status queues and extracting consumer info
  getConsumerGroups: async () => {
    try {
      const response = await apiClient.get('/api/v1/status/queues');
      const queues = response.data.queues;
      
      // Extract unique consumer groups from queue data
      const consumerMap = new Map();
      
      queues.forEach(queue => {
        // For now, we'll derive consumer groups from lag and performance data
        // In a real implementation, we'd need a dedicated API endpoint
        if (queue.lag || queue.performance) {
          const consumerGroup = queue.consumerGroup || '__QUEUE_MODE__';
          
          if (!consumerMap.has(consumerGroup)) {
            consumerMap.set(consumerGroup, {
              name: consumerGroup,
              topics: [],
              queues: new Set(),
              totalLag: 0,
              maxTimeLag: 0,
              state: 'Stable',
            });
          }
          
          const consumer = consumerMap.get(consumerGroup);
          consumer.queues.add(queue.name);
          consumer.topics.push(queue.name);
          
          if (queue.lag) {
            consumer.totalLag += queue.lag.offset || 0;
            consumer.maxTimeLag = Math.max(consumer.maxTimeLag, queue.lag.timeLagMs || 0);
          }
        }
      });
      
      return Array.from(consumerMap.values()).map(c => ({
        ...c,
        topics: Array.from(c.queues),
        members: c.topics.length,
      }));
    } catch (error) {
      console.error('Error fetching consumer groups:', error);
      return [];
    }
  },
};

