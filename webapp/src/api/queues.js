import apiClient from './client';

export const queuesApi = {
  getQueues: (params) => apiClient.get('/api/v1/resources/queues', { params }),
  getQueue: (queueName) => apiClient.get(`/api/v1/resources/queues/${queueName}`),
  deleteQueue: (queueName) => apiClient.delete(`/api/v1/resources/queues/${queueName}`),
  clearQueue: (queueName, partition) => {
    const params = partition ? { partition } : {};
    return apiClient.delete(`/api/v1/queues/${queueName}/clear`, { params });
  },
  configureQueue: (data) => apiClient.post('/api/v1/configure', data),
  getPartitions: (params) => apiClient.get('/api/v1/resources/partitions', { params }),
};

