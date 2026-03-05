/**
 * HTTP client with retry, load balancing, and failover support
 */

import * as logger from '../utils/logger'
import type { HttpClientOptions } from '../types'
import type { LoadBalancer } from './LoadBalancer'

export class HttpClient {
  private baseUrl: string | null
  private loadBalancer: LoadBalancer | null
  private timeoutMillis: number
  private retryAttempts: number
  private retryDelayMillis: number
  private enableFailover: boolean
  private bearerToken: string | null
  private headers: Record<string, string>

  constructor(options: HttpClientOptions = {}) {
    const {
      baseUrl = null,
      loadBalancer = null,
      timeoutMillis = 30000,
      retryAttempts = 3,
      retryDelayMillis = 1000,
      enableFailover = true,
      bearerToken = null,
      headers = {}
    } = options

    this.baseUrl = baseUrl ?? null
    this.loadBalancer = loadBalancer ?? null
    this.timeoutMillis = timeoutMillis
    this.retryAttempts = retryAttempts
    this.retryDelayMillis = retryDelayMillis
    this.enableFailover = enableFailover
    this.bearerToken = bearerToken ?? null
    this.headers = headers || {}

    logger.log('HttpClient.constructor', {
      hasLoadBalancer: !!loadBalancer,
      baseUrl: baseUrl || 'load-balanced',
      timeoutMillis,
      retryAttempts,
      enableFailover,
      hasAuth: !!bearerToken,
      customHeaders: Object.keys(this.headers).length
    })
  }

  private async executeRequest(url: string, method: string, body: unknown = null, requestTimeoutMillis: number | null = null): Promise<unknown> {
    const effectiveTimeout = requestTimeoutMillis || this.timeoutMillis
    logger.log('HttpClient.request', { method, url, hasBody: !!body, timeout: effectiveTimeout })

    const controller = new AbortController()
    const timeoutId = setTimeout(() => controller.abort(), effectiveTimeout)

    try {
      const headers: Record<string, string> = { 'Content-Type': 'application/json' }
      if (this.bearerToken) {
        headers['Authorization'] = `Bearer ${this.bearerToken}`
      }
      Object.assign(headers, this.headers)

      const options: RequestInit = {
        method,
        signal: controller.signal,
        headers
      }

      if (body) {
        options.body = JSON.stringify(body)
      }

      const response = await fetch(url, options)

      logger.log('HttpClient.response', { method, url, status: response.status })

      if (response.status === 204) {
        return null
      }

      if (!response.ok) {
        const error: Error & { status?: number } = new Error(`HTTP ${response.status}: ${response.statusText}`)
        error.status = response.status

        try {
          const text = await response.text()
          if (text) {
            const body = JSON.parse(text)
            error.message = body.error || error.message
          }
        } catch (_e) {
          // Ignore JSON parse errors
        }

        logger.error('HttpClient.request', { method, url, status: error.status, error: error.message })
        throw error
      }

      const contentType = response.headers.get('content-type')
      const contentLength = response.headers.get('content-length')

      if (!contentType || !contentType.includes('application/json') || contentLength === '0') {
        const text = await response.text()
        if (!text || text.length === 0) {
          return null
        }
        try {
          return JSON.parse(text)
        } catch (_e) {
          return null
        }
      }

      return response.json()

    } catch (error) {
      if (error instanceof Error && error.name === 'AbortError') {
        const timeoutError: Error & { timeout?: number } = new Error(`Request timeout after ${effectiveTimeout}ms`)
        timeoutError.name = 'AbortError'
        timeoutError.timeout = effectiveTimeout
        logger.error('HttpClient.request', { method, url, error: 'timeout', timeout: effectiveTimeout })
        throw timeoutError
      }
      logger.error('HttpClient.request', { method, url, error: (error as Error).message })
      throw error
    } finally {
      clearTimeout(timeoutId)
    }
  }

  private async requestWithRetry(method: string, path: string, body: unknown = null, requestTimeoutMillis: number | null = null): Promise<unknown> {
    let lastError: Error | null = null

    for (let attempt = 0; attempt < this.retryAttempts; attempt++) {
      try {
        const url = this.getUrl() + path
        return await this.executeRequest(url, method, body, requestTimeoutMillis)
      } catch (error) {
        lastError = error as Error

        const err = error as Error & { status?: number }
        if (err.status && err.status >= 400 && err.status < 500) {
          throw error
        }

        if (attempt < this.retryAttempts - 1) {
          const delay = this.retryDelayMillis * Math.pow(2, attempt)
          logger.warn('HttpClient.retry', { method, path, attempt: attempt + 1, delay, error: (error as Error).message })
          await new Promise(resolve => setTimeout(resolve, delay))
        }
      }
    }

    logger.error('HttpClient.retry', { method, path, error: 'Max retries exceeded', attempts: this.retryAttempts })
    throw lastError
  }

  private async requestWithFailover(method: string, path: string, body: unknown = null, requestTimeoutMillis: number | null = null, affinityKey: string | null = null): Promise<unknown> {
    if (!this.loadBalancer || !this.enableFailover) {
      return this.requestWithRetry(method, path, body, requestTimeoutMillis)
    }

    const urls = this.loadBalancer.getAllUrls()
    const attemptedUrls = new Set<string>()
    let lastError: Error | null = null

    logger.log('HttpClient.failover', { method, path, totalServers: urls.length, affinityKey })

    for (let i = 0; i < urls.length; i++) {
      const url = this.loadBalancer.getNextUrl(affinityKey)

      if (attemptedUrls.has(url)) {
        continue
      }

      attemptedUrls.add(url)

      try {
        const result = await this.executeRequest(url + path, method, body, requestTimeoutMillis)

        this.loadBalancer.markHealthy(url)

        return result
      } catch (error) {
        lastError = error as Error

        const err = error as Error & { status?: number }
        if (!err.status || err.status >= 500) {
          this.loadBalancer.markUnhealthy(url)
        }

        logger.warn('HttpClient.failover', { url, method, path, error: (error as Error).message })
        console.warn(`Request failed for ${url}: ${method} ${path} - ${(error as Error).message}`)

        if (err.status && err.status >= 400 && err.status < 500) {
          throw error
        }
      }
    }

    logger.error('HttpClient.failover', { method, path, error: 'All servers failed', attempted: attemptedUrls.size })
    throw lastError || new Error('All servers failed')
  }

  private getUrl(): string {
    if (this.loadBalancer) {
      return this.loadBalancer.getNextUrl()
    }
    return this.baseUrl!
  }

  async get(path: string, requestTimeoutMillis: number | null = null, affinityKey: string | null = null): Promise<unknown> {
    return this.requestWithFailover('GET', path, null, requestTimeoutMillis, affinityKey)
  }

  async post(path: string, body: unknown = null, requestTimeoutMillis: number | null = null, affinityKey: string | null = null): Promise<unknown> {
    return this.requestWithFailover('POST', path, body, requestTimeoutMillis, affinityKey)
  }

  async put(path: string, body: unknown = null, requestTimeoutMillis: number | null = null, affinityKey: string | null = null): Promise<unknown> {
    return this.requestWithFailover('PUT', path, body, requestTimeoutMillis, affinityKey)
  }

  async delete(path: string, requestTimeoutMillis: number | null = null, affinityKey: string | null = null): Promise<unknown> {
    return this.requestWithFailover('DELETE', path, null, requestTimeoutMillis, affinityKey)
  }

  getLoadBalancer(): LoadBalancer | null {
    return this.loadBalancer
  }
}
