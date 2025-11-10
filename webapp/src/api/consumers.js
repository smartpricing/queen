import apiClient from './client';

export const consumersApi = {
  // Get consumer groups from dedicated endpoint
  getConsumerGroups: async () => {
    try {
      const response = await apiClient.get('/api/v1/consumer-groups');
      return response.data || [];
    } catch (error) {
      console.error('Error fetching consumer groups:', error);
      return [];
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
};

