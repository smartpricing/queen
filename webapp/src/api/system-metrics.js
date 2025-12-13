import client from './client';

export const systemMetricsApi = {
  /**
   * Get system metrics time series (CPU, Memory, Thread Pool)
   * @param {Object} params - Query parameters
   * @param {string} params.from - Start timestamp (ISO 8601)
   * @param {string} params.to - End timestamp (ISO 8601)
   * @param {string} params.hostname - Filter by hostname (optional)
   * @param {string} params.workerId - Filter by worker ID (optional)
   * @returns {Promise} Axios response with system metrics time series
   */
  getSystemMetrics(params = {}) {
    return client.get('/api/v1/analytics/system-metrics', { params });
  },
  
  /**
   * Get worker metrics time series (throughput, lag, event loop, pool)
   * @param {Object} params - Query parameters
   * @param {string} params.from - Start timestamp (ISO 8601)
   * @param {string} params.to - End timestamp (ISO 8601)
   * @param {string} params.hostname - Filter by hostname (optional)
   * @param {string} params.workerId - Filter by worker ID (optional)
   * @returns {Promise} Axios response with worker metrics time series
   */
  getWorkerMetrics(params = {}) {
    return client.get('/api/v1/analytics/worker-metrics', { params });
  },
  
  /**
   * Get shared state (UDPSYNC) stats for distributed cache monitoring
   * @returns {Promise} Axios response with shared state statistics
   */
  getSharedStateStats() {
    return client.get('/api/v1/system/shared-state');
  },
};

