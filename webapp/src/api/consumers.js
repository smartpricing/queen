import apiClient from './client';

export const consumersApi = {
  // Get consumer groups summary (without partition details)
  getConsumerGroups: async () => {
    try {
      const response = await apiClient.get('/api/v1/consumer-groups');
      return response.data || [];
    } catch (error) {
      console.error('Error fetching consumer groups:', error);
      return [];
    }
  },
  
  // Get consumer group details (partition data)
  getConsumerGroupDetails: async (consumerGroup) => {
    try {
      const response = await apiClient.get(`/api/v1/consumer-groups/${encodeURIComponent(consumerGroup)}`);
      return response.data || {};
    } catch (error) {
      console.error('Error fetching consumer group details:', error);
      throw error;
    }
  },
  
  // Get lagging partitions
  getLaggingPartitions: async (minLagSeconds) => {
    try {
      const response = await apiClient.get(`/api/v1/consumer-groups/lagging?minLagSeconds=${minLagSeconds}`);
      return response.data || [];
    } catch (error) {
      console.error('Error fetching lagging partitions:', error);
      throw error;
    }
  },
  
  // Delete a consumer group
  deleteConsumerGroup: async (consumerGroup, deleteMetadata = true) => {
    try {
      const url = `/api/v1/consumer-groups/${encodeURIComponent(consumerGroup)}?deleteMetadata=${deleteMetadata}`;
      const response = await apiClient.delete(url);
      return response.data;
    } catch (error) {
      console.error('Error deleting consumer group:', error);
      throw error;
    }
  },
  
  // Update consumer group subscription timestamp
  updateSubscriptionTimestamp: async (consumerGroup, newTimestamp) => {
    try {
      const url = `/api/v1/consumer-groups/${encodeURIComponent(consumerGroup)}/subscription`;
      const response = await apiClient.post(url, {
        subscriptionTimestamp: newTimestamp
      });
      return response.data;
    } catch (error) {
      console.error('Error updating subscription timestamp:', error);
      throw error;
    }
  },
  
  // Delete a consumer group for a specific queue only
  deleteConsumerGroupForQueue: async (consumerGroup, queueName, deleteMetadata = true) => {
    try {
      const url = `/api/v1/consumer-groups/${encodeURIComponent(consumerGroup)}/queues/${encodeURIComponent(queueName)}?deleteMetadata=${deleteMetadata}`;
      const response = await apiClient.delete(url);
      return response.data;
    } catch (error) {
      console.error('Error deleting consumer group for queue:', error);
      throw error;
    }
  },
  
  // Seek consumer group cursor to end (skip all) or specific timestamp
  seekConsumerGroup: async (consumerGroup, queueName, { toEnd = false, timestamp = null } = {}) => {
    try {
      const url = `/api/v1/consumer-groups/${encodeURIComponent(consumerGroup)}/queues/${encodeURIComponent(queueName)}/seek`;
      const body = toEnd ? { toEnd: true } : { timestamp };
      const response = await apiClient.post(url, body);
      return response.data;
    } catch (error) {
      console.error('Error seeking consumer group:', error);
      throw error;
    }
  },
};

