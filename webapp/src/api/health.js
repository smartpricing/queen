import apiClient from './client';

export const healthApi = {
  getHealth: () => apiClient.get('/health'),
  getMetrics: () => apiClient.get('/metrics'),
};

