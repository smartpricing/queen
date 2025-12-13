import apiClient from './client';

export const systemApi = {
  // Get maintenance mode status (includes both push and pop modes)
  getMaintenanceStatus: () => apiClient.get('/api/v1/system/maintenance'),
  
  // ============================================================
  // Push Maintenance Mode (buffers writes to file)
  // ============================================================
  
  // Enable push maintenance mode
  enableMaintenance: () => apiClient.post('/api/v1/system/maintenance', { enabled: true }),
  
  // Disable push maintenance mode
  disableMaintenance: () => apiClient.post('/api/v1/system/maintenance', { enabled: false }),
  
  // Toggle push maintenance mode
  setMaintenanceMode: (enabled) => apiClient.post('/api/v1/system/maintenance', { enabled }),
  
  // ============================================================
  // Pop Maintenance Mode (consumers receive empty arrays)
  // ============================================================
  
  // Get pop maintenance mode status
  getPopMaintenanceStatus: () => apiClient.get('/api/v1/system/maintenance/pop'),
  
  // Enable pop maintenance mode
  enablePopMaintenance: () => apiClient.post('/api/v1/system/maintenance/pop', { enabled: true }),
  
  // Disable pop maintenance mode
  disablePopMaintenance: () => apiClient.post('/api/v1/system/maintenance/pop', { enabled: false }),
  
  // Toggle pop maintenance mode
  setPopMaintenanceMode: (enabled) => apiClient.post('/api/v1/system/maintenance/pop', { enabled }),
};

