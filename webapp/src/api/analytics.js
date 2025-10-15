import apiClient from './client';

export const analyticsApi = {
  getStatus: (params) => apiClient.get('/api/v1/status', { params }),
  getQueues: (params) => apiClient.get('/api/v1/status/queues', { params }),
  getQueueDetail: (queueName, params) => apiClient.get(`/api/v1/status/queues/${queueName}`, { params }),
  getQueueMessages: (queueName, params) => apiClient.get(`/api/v1/status/queues/${queueName}/messages`, { params }),
  getAnalytics: (params) => apiClient.get('/api/v1/status/analytics', { params }),
};

