/**
 * Admin API for Queen - Administrative and observability endpoints
 * These APIs are typically used by dashboards and admin tools, not regular applications
 */

import * as logger from '../utils/logger'
import type { HttpClient } from '../http/HttpClient'

export class Admin {
  private httpClient: HttpClient

  constructor(httpClient: HttpClient) {
    this.httpClient = httpClient
  }

  // ===========================
  // Resources API
  // ===========================

  async getOverview(): Promise<unknown> {
    logger.log('Admin.getOverview', {})
    return this.httpClient.get('/api/v1/resources/overview')
  }

  async getNamespaces(): Promise<unknown> {
    logger.log('Admin.getNamespaces', {})
    return this.httpClient.get('/api/v1/resources/namespaces')
  }

  async getTasks(): Promise<unknown> {
    logger.log('Admin.getTasks', {})
    return this.httpClient.get('/api/v1/resources/tasks')
  }

  // ===========================
  // Queues API
  // ===========================

  async listQueues(params: Record<string, string | number> = {}): Promise<unknown> {
    logger.log('Admin.listQueues', { params })
    const queryString = this.buildQueryString(params)
    return this.httpClient.get(`/api/v1/resources/queues${queryString}`)
  }

  async getQueue(name: string): Promise<unknown> {
    logger.log('Admin.getQueue', { name })
    return this.httpClient.get(`/api/v1/resources/queues/${encodeURIComponent(name)}`)
  }

  async clearQueue(name: string, partition: string | null = null): Promise<unknown> {
    logger.log('Admin.clearQueue', { name, partition })
    const queryString = partition ? `?partition=${encodeURIComponent(partition)}` : ''
    return this.httpClient.delete(`/api/v1/queues/${encodeURIComponent(name)}/clear${queryString}`)
  }

  async getPartitions(params: Record<string, string | number> = {}): Promise<unknown> {
    logger.log('Admin.getPartitions', { params })
    const queryString = this.buildQueryString(params)
    return this.httpClient.get(`/api/v1/resources/partitions${queryString}`)
  }

  // ===========================
  // Messages API
  // ===========================

  async listMessages(params: Record<string, string | number> = {}): Promise<unknown> {
    logger.log('Admin.listMessages', { params })
    const queryString = this.buildQueryString(params)
    return this.httpClient.get(`/api/v1/messages${queryString}`)
  }

  async getMessage(partitionId: string, transactionId: string): Promise<unknown> {
    logger.log('Admin.getMessage', { partitionId, transactionId })
    return this.httpClient.get(`/api/v1/messages/${partitionId}/${transactionId}`)
  }

  async deleteMessage(partitionId: string, transactionId: string): Promise<unknown> {
    logger.log('Admin.deleteMessage', { partitionId, transactionId })
    return this.httpClient.delete(`/api/v1/messages/${partitionId}/${transactionId}`)
  }

  async retryMessage(partitionId: string, transactionId: string): Promise<unknown> {
    logger.log('Admin.retryMessage', { partitionId, transactionId })
    return this.httpClient.post(`/api/v1/messages/${partitionId}/${transactionId}/retry`, {})
  }

  async moveMessageToDLQ(partitionId: string, transactionId: string): Promise<unknown> {
    logger.log('Admin.moveMessageToDLQ', { partitionId, transactionId })
    return this.httpClient.post(`/api/v1/messages/${partitionId}/${transactionId}/dlq`, {})
  }

  // ===========================
  // Traces API
  // ===========================

  async getTracesByName(traceName: string, params: Record<string, string | number> = {}): Promise<unknown> {
    logger.log('Admin.getTracesByName', { traceName, params })
    const queryString = this.buildQueryString(params)
    return this.httpClient.get(`/api/v1/traces/by-name/${encodeURIComponent(traceName)}${queryString}`)
  }

  async getTraceNames(params: Record<string, string | number> = {}): Promise<unknown> {
    logger.log('Admin.getTraceNames', { params })
    const queryString = this.buildQueryString(params)
    return this.httpClient.get(`/api/v1/traces/names${queryString}`)
  }

  async getTracesForMessage(partitionId: string, transactionId: string): Promise<unknown> {
    logger.log('Admin.getTracesForMessage', { partitionId, transactionId })
    return this.httpClient.get(`/api/v1/traces/${partitionId}/${transactionId}`)
  }

  // ===========================
  // Analytics/Status API
  // ===========================

  async getStatus(params: Record<string, string | number> = {}): Promise<unknown> {
    logger.log('Admin.getStatus', { params })
    const queryString = this.buildQueryString(params)
    return this.httpClient.get(`/api/v1/status${queryString}`)
  }

  async getQueueStats(params: Record<string, string | number> = {}): Promise<unknown> {
    logger.log('Admin.getQueueStats', { params })
    const queryString = this.buildQueryString(params)
    return this.httpClient.get(`/api/v1/status/queues${queryString}`)
  }

  async getQueueDetail(name: string, params: Record<string, string | number> = {}): Promise<unknown> {
    logger.log('Admin.getQueueDetail', { name, params })
    const queryString = this.buildQueryString(params)
    return this.httpClient.get(`/api/v1/status/queues/${encodeURIComponent(name)}${queryString}`)
  }

  async getAnalytics(params: Record<string, string | number> = {}): Promise<unknown> {
    logger.log('Admin.getAnalytics', { params })
    const queryString = this.buildQueryString(params)
    return this.httpClient.get(`/api/v1/status/analytics${queryString}`)
  }

  // ===========================
  // Consumer Groups API
  // ===========================

  async listConsumerGroups(): Promise<unknown> {
    logger.log('Admin.listConsumerGroups', {})
    return this.httpClient.get('/api/v1/consumer-groups')
  }

  async refreshConsumerStats(): Promise<unknown> {
    logger.log('Admin.refreshConsumerStats', {})
    return this.httpClient.post('/api/v1/stats/refresh', {})
  }

  async getConsumerGroup(name: string): Promise<unknown> {
    logger.log('Admin.getConsumerGroup', { name })
    return this.httpClient.get(`/api/v1/consumer-groups/${encodeURIComponent(name)}`)
  }

  async getLaggingConsumers(minLagSeconds: number = 60): Promise<unknown> {
    logger.log('Admin.getLaggingConsumers', { minLagSeconds })
    return this.httpClient.get(`/api/v1/consumer-groups/lagging?minLagSeconds=${minLagSeconds}`)
  }

  async deleteConsumerGroupForQueue(consumerGroup: string, queueName: string, deleteMetadata: boolean = true): Promise<unknown> {
    logger.log('Admin.deleteConsumerGroupForQueue', { consumerGroup, queueName, deleteMetadata })
    const url = `/api/v1/consumer-groups/${encodeURIComponent(consumerGroup)}/queues/${encodeURIComponent(queueName)}?deleteMetadata=${deleteMetadata}`
    return this.httpClient.delete(url)
  }

  async seekConsumerGroup(consumerGroup: string, queueName: string, options: Record<string, unknown> = {}): Promise<unknown> {
    logger.log('Admin.seekConsumerGroup', { consumerGroup, queueName, options })
    const url = `/api/v1/consumer-groups/${encodeURIComponent(consumerGroup)}/queues/${encodeURIComponent(queueName)}/seek`
    return this.httpClient.post(url, options)
  }

  // ===========================
  // System API
  // ===========================

  async health(): Promise<unknown> {
    logger.log('Admin.health', {})
    return this.httpClient.get('/health')
  }

  async metrics(): Promise<unknown> {
    logger.log('Admin.metrics', {})
    return this.httpClient.get('/metrics')
  }

  async getMaintenanceMode(): Promise<unknown> {
    logger.log('Admin.getMaintenanceMode', {})
    return this.httpClient.get('/api/v1/system/maintenance')
  }

  async setMaintenanceMode(enabled: boolean): Promise<unknown> {
    logger.log('Admin.setMaintenanceMode', { enabled })
    return this.httpClient.post('/api/v1/system/maintenance', { enabled })
  }

  async getPopMaintenanceMode(): Promise<unknown> {
    logger.log('Admin.getPopMaintenanceMode', {})
    return this.httpClient.get('/api/v1/system/maintenance/pop')
  }

  async setPopMaintenanceMode(enabled: boolean): Promise<unknown> {
    logger.log('Admin.setPopMaintenanceMode', { enabled })
    return this.httpClient.post('/api/v1/system/maintenance/pop', { enabled })
  }

  async getSystemMetrics(params: Record<string, string | number> = {}): Promise<unknown> {
    logger.log('Admin.getSystemMetrics', { params })
    const queryString = this.buildQueryString(params)
    return this.httpClient.get(`/api/v1/analytics/system-metrics${queryString}`)
  }

  async getWorkerMetrics(params: Record<string, string | number> = {}): Promise<unknown> {
    logger.log('Admin.getWorkerMetrics', { params })
    const queryString = this.buildQueryString(params)
    return this.httpClient.get(`/api/v1/analytics/worker-metrics${queryString}`)
  }

  async getPostgresStats(): Promise<unknown> {
    logger.log('Admin.getPostgresStats', {})
    return this.httpClient.get('/api/v1/analytics/postgres-stats')
  }

  // ===========================
  // Helper Methods
  // ===========================

  private buildQueryString(params: Record<string, string | number | boolean | null | undefined>): string {
    const searchParams = new URLSearchParams()
    for (const [key, value] of Object.entries(params)) {
      if (value !== undefined && value !== null) {
        searchParams.append(key, String(value))
      }
    }
    const queryString = searchParams.toString()
    return queryString ? `?${queryString}` : ''
  }
}
