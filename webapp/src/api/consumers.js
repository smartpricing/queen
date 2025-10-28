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
};

