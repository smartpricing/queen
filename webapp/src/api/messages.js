import apiClient from './client';

export const messagesApi = {
  getMessages: (params) => apiClient.get('/api/v1/messages', { params }),
  getMessage: (partitionId, transactionId) => apiClient.get(`/api/v1/messages/${partitionId}/${transactionId}`),
  deleteMessage: (partitionId, transactionId) => apiClient.delete(`/api/v1/messages/${partitionId}/${transactionId}`),
  retryMessage: (partitionId, transactionId) => apiClient.post(`/api/v1/messages/${partitionId}/${transactionId}/retry`),
  moveToDLQ: (partitionId, transactionId) => apiClient.post(`/api/v1/messages/${partitionId}/${transactionId}/dlq`),
  getRelatedMessages: (partitionId, transactionId) => apiClient.get(`/api/v1/messages/${partitionId}/${transactionId}/related`),
  
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
  
  // Tracing
  getTraces: (partitionId, transactionId) => apiClient.get(`/api/v1/traces/${partitionId}/${transactionId}`),
  getTracesByName: (traceName, params) => apiClient.get(`/api/v1/traces/by-name/${traceName}`, { params }),
  getAvailableTraceNames: (params) => apiClient.get('/api/v1/traces/names', { params }),
};

