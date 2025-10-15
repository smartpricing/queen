import apiClient from './client';

export const messagesApi = {
  getMessages: (params) => apiClient.get('/api/v1/messages', { params }),
  getMessage: (transactionId) => apiClient.get(`/api/v1/messages/${transactionId}`),
  deleteMessage: (transactionId) => apiClient.delete(`/api/v1/messages/${transactionId}`),
  retryMessage: (transactionId) => apiClient.post(`/api/v1/messages/${transactionId}/retry`),
  moveToDLQ: (transactionId) => apiClient.post(`/api/v1/messages/${transactionId}/dlq`),
  getRelatedMessages: (transactionId) => apiClient.get(`/api/v1/messages/${transactionId}/related`),
  
  // Push messages
  pushMessages: (data) => apiClient.post('/api/v1/push', data),
  
  // Pop messages
  popMessages: (queue, partition, params) => {
    if (partition) {
      return apiClient.get(`/api/v1/pop/queue/${queue}/partition/${partition}`, { params });
    }
    return apiClient.get(`/api/v1/pop/queue/${queue}`, { params });
  },
  
  // Acknowledgment
  ackMessage: (data) => apiClient.post('/api/v1/ack', data),
  batchAck: (data) => apiClient.post('/api/v1/ack/batch', data),
};

