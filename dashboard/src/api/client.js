const BASE_URL = 'http://localhost:6632';

class ApiClient {
  constructor(baseUrl = BASE_URL) {
    this.baseUrl = baseUrl;
  }
  
  async request(endpoint, options = {}) {
    const url = `${this.baseUrl}${endpoint}`;
    
    try {
      const response = await fetch(url, {
        headers: {
          'Content-Type': 'application/json',
          ...options.headers
        },
        ...options
      });
      
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }
      
      return await response.json();
    } catch (error) {
      console.error('API request failed:', error);
      throw error;
    }
  }
  
  // Dashboard endpoints
  getStatus(params = {}) {
    const query = new URLSearchParams(params).toString();
    const endpoint = query ? `/api/v1/status?${query}` : '/api/v1/status';
    return this.request(endpoint);
  }
  
  getQueues(params = {}) {
    const query = new URLSearchParams(params).toString();
    const endpoint = query ? `/api/v1/status/queues?${query}` : '/api/v1/status/queues';
    return this.request(endpoint);
  }
  
  getQueueDetail(queueName, params = {}) {
    const query = new URLSearchParams(params).toString();
    const endpoint = query ? `/api/v1/status/queues/${queueName}?${query}` : `/api/v1/status/queues/${queueName}`;
    return this.request(endpoint);
  }
  
  getQueueMessages(queueName, params = {}) {
    const query = new URLSearchParams(params).toString();
    const endpoint = query ? `/api/v1/status/queues/${queueName}/messages?${query}` : `/api/v1/status/queues/${queueName}/messages`;
    return this.request(endpoint);
  }
  
  getAnalytics(params = {}) {
    const query = new URLSearchParams(params).toString();
    const endpoint = query ? `/api/v1/status/analytics?${query}` : '/api/v1/status/analytics';
    return this.request(endpoint);
  }
}

export default new ApiClient();
