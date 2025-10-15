import apiClient from './client';

export const resourcesApi = {
  getOverview: () => apiClient.get('/api/v1/resources/overview'),
  getNamespaces: () => apiClient.get('/api/v1/resources/namespaces'),
  getTasks: () => apiClient.get('/api/v1/resources/tasks'),
};

