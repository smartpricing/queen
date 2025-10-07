import axios from 'axios'

const API_URL = import.meta.env.VITE_API_URL || 'http://localhost:6632'

const client = axios.create({
  baseURL: API_URL,
  headers: {
    'Content-Type': 'application/json'
  }
})

// Request interceptor
client.interceptors.request.use(
  config => {
    // Add auth token if available
    // config.headers.Authorization = `Bearer ${token}`
    return config
  },
  error => Promise.reject(error)
)

// Response interceptor
client.interceptors.response.use(
  response => response.data,
  error => {
    console.error('API Error:', error)
    const message = error.response?.data?.error || error.message || 'An error occurred'
    return Promise.reject(new Error(message))
  }
)

export const api = {
  // Configuration
  configure: (config) => client.post('/api/v1/configure', config),

  // Messages
  push: (data) => client.post('/api/v1/push', data),
  
  pop: (scope, options = {}) => {
    const { ns, task, queue } = scope
    let path = '/api/v1/pop'
    
    if (ns) path += `/ns/${ns}`
    if (task) path += `/task/${task}`
    if (queue) path += `/queue/${queue}`
    
    const params = new URLSearchParams({
      wait: options.wait || false,
      timeout: options.timeout || 30000,
      batch: options.batch || 1
    })
    
    return client.get(`${path}?${params}`)
  },
  
  ack: (transactionId, status, error) => 
    client.post('/api/v1/ack', { transactionId, status, error }),
  
  ackBatch: (acknowledgments) => 
    client.post('/api/v1/ack/batch', { acknowledgments }),

  // Analytics
  analytics: {
    getQueues: () => client.get('/api/v1/analytics/queues'),
    getNamespace: (ns) => client.get(`/api/v1/analytics/ns/${ns}`),
    getTask: (ns, task) => client.get(`/api/v1/analytics/ns/${ns}/task/${task}`),
    getQueueStats: (ns, task, queue) => {
      // For specific queue stats, use task endpoint and filter
      return client.get(`/api/v1/analytics/ns/${ns}/task/${task}`)
    },
    getQueueDepths: () => client.get('/api/v1/analytics/queue-depths'),
    getThroughput: () => client.get('/api/v1/analytics/throughput')
  },

  // Health
  health: () => client.get('/health'),

  // Messages management
  messages: {
    list: (filters = {}) => {
      const params = new URLSearchParams()
      Object.entries(filters).forEach(([key, value]) => {
        if (value !== null && value !== undefined) {
          params.append(key, value)
        }
      })
      return client.get(`/api/v1/messages?${params}`)
    },
    get: (transactionId) => client.get(`/api/v1/messages/${transactionId}`),
    delete: (transactionId) => client.delete(`/api/v1/messages/${transactionId}`),
    retry: (transactionId) => client.post(`/api/v1/messages/${transactionId}/retry`),
    moveToDLQ: (transactionId) => client.post(`/api/v1/messages/${transactionId}/dlq`),
    getRelated: (transactionId) => client.get(`/api/v1/messages/${transactionId}/related`)
  },

  // Queue management
  queues: {
    clear: (ns, task, queue) => client.delete(`/api/v1/queues/${ns}/${task}/${queue}/clear`)
  }
}

export default api
