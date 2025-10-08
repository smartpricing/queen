import { API_BASE_URL, API_VERSION } from '../utils/constants.js'

class ApiService {
  constructor() {
    this.baseUrl = `${API_BASE_URL}${API_VERSION}`
    this.headers = {
      'Content-Type': 'application/json'
    }
  }

  async request(endpoint, options = {}) {
    const url = endpoint.startsWith('http') ? endpoint : `${this.baseUrl}${endpoint}`
    
    try {
      const response = await fetch(url, {
        ...options,
        headers: {
          ...this.headers,
          ...options.headers
        }
      })

      if (response.status === 204) {
        return null // No content
      }

      const data = await response.json()

      if (!response.ok) {
        throw new Error(data.error || `HTTP error! status: ${response.status}`)
      }

      return data
    } catch (error) {
      console.error('API request failed:', error)
      throw error
    }
  }

  // GET request
  async get(endpoint, params = {}) {
    const queryString = new URLSearchParams(params).toString()
    const url = queryString ? `${endpoint}?${queryString}` : endpoint
    return this.request(url, { method: 'GET' })
  }

  // POST request
  async post(endpoint, data = {}) {
    return this.request(endpoint, {
      method: 'POST',
      body: JSON.stringify(data)
    })
  }

  // PUT request
  async put(endpoint, data = {}) {
    return this.request(endpoint, {
      method: 'PUT',
      body: JSON.stringify(data)
    })
  }

  // DELETE request
  async delete(endpoint) {
    return this.request(endpoint, { method: 'DELETE' })
  }

  // Health & Metrics
  async getHealth() {
    return this.request(`${API_BASE_URL}/health`)
  }

  async getMetrics() {
    return this.request(`${API_BASE_URL}/metrics`)
  }

  // Resources
  async getSystemOverview() {
    return this.get('/resources/overview')
  }

  async getQueues(params = {}) {
    return this.get('/resources/queues', params)
  }

  async getQueueDetail(queueName) {
    return this.get(`/resources/queues/${encodeURIComponent(queueName)}`)
  }

  async getPartitions(params = {}) {
    return this.get('/resources/partitions', params)
  }

  async getNamespaces() {
    return this.get('/resources/namespaces')
  }

  async getTasks() {
    return this.get('/resources/tasks')
  }

  // Analytics
  async getQueueAnalytics(queueName) {
    return this.get(`/analytics/queue/${encodeURIComponent(queueName)}`)
  }

  async getAllQueuesAnalytics(params = {}) {
    return this.get('/analytics/queues', params)
  }

  async getQueueDepths() {
    return this.get('/analytics/queue-depths')
  }

  async getThroughput() {
    return this.get('/analytics/throughput')
  }

  async getQueueStats(params = {}) {
    return this.get('/analytics/queue-stats', params)
  }

  async getQueueLag(params = {}) {
    return this.get('/analytics/queue-lag', params)
  }

  // Messages
  async getMessages(params = {}) {
    return this.get('/messages', params)
  }

  async getMessage(transactionId) {
    return this.get(`/messages/${transactionId}`)
  }

  async getRelatedMessages(transactionId) {
    return this.get(`/messages/${transactionId}/related`)
  }

  async deleteMessage(transactionId) {
    return this.delete(`/messages/${transactionId}`)
  }

  async retryMessage(transactionId) {
    return this.post(`/messages/${transactionId}/retry`)
  }

  async moveMessageToDLQ(transactionId) {
    return this.post(`/messages/${transactionId}/dlq`)
  }

  async clearQueue(queueName, partition = null) {
    const params = partition ? { partition } : {}
    return this.delete(`/queues/${encodeURIComponent(queueName)}/clear`, params)
  }

  // Push & Pop
  async pushMessages(items) {
    return this.post('/push', { items })
  }

  async popMessages(queue, partition = null, options = {}) {
    const endpoint = partition 
      ? `/pop/queue/${encodeURIComponent(queue)}/partition/${encodeURIComponent(partition)}`
      : `/pop/queue/${encodeURIComponent(queue)}`
    return this.get(endpoint, options)
  }

  // Acknowledge
  async acknowledgeMessage(transactionId, status, error = null) {
    const data = { transactionId, status }
    if (error) data.error = error
    return this.post('/ack', data)
  }

  async acknowledgeBatch(acknowledgments) {
    return this.post('/ack/batch', { acknowledgments })
  }

  // Configuration
  async configurePartition(queue, partition = 'Default', options = {}) {
    return this.post('/configure', { queue, partition, options })
  }
}

// Export singleton instance
export default new ApiService()
