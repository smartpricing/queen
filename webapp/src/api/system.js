import apiClient from './client';

export const systemApi = {
  // Get maintenance mode status
  getMaintenanceStatus: () => apiClient.get('/api/v1/system/maintenance'),
  
  // Enable maintenance mode
  enableMaintenance: () => apiClient.post('/api/v1/system/maintenance', { enabled: true }),
  
  // Disable maintenance mode
  disableMaintenance: () => apiClient.post('/api/v1/system/maintenance', { enabled: false }),
  
  // Toggle maintenance mode
  setMaintenanceMode: (enabled) => apiClient.post('/api/v1/system/maintenance', { enabled }),
};

