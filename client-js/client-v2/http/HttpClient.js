/**
 * HTTP client with retry, load balancing, and failover support
 */

import * as logger from '../utils/logger.js'

export class HttpClient {
  #baseUrl
  #loadBalancer
  #timeoutMillis
  #retryAttempts
  #retryDelayMillis
  #enableFailover

  constructor(options = {}) {
    const {
      baseUrl = null,
      loadBalancer = null,
      timeoutMillis = 30000,
      retryAttempts = 3,
      retryDelayMillis = 1000,
      enableFailover = true
    } = options

    this.#baseUrl = baseUrl
    this.#loadBalancer = loadBalancer
    this.#timeoutMillis = timeoutMillis
    this.#retryAttempts = retryAttempts
    this.#retryDelayMillis = retryDelayMillis
    this.#enableFailover = enableFailover
    
    logger.log('HttpClient.constructor', { 
      hasLoadBalancer: !!loadBalancer, 
      baseUrl: baseUrl || 'load-balanced', 
      timeoutMillis, 
      retryAttempts,
      enableFailover 
    })
  }

  async #executeRequest(url, method, body = null, requestTimeoutMillis = null) {
    const effectiveTimeout = requestTimeoutMillis || this.#timeoutMillis
    logger.log('HttpClient.request', { method, url, hasBody: !!body, timeout: effectiveTimeout })
    
    const controller = new AbortController()
    const timeoutId = setTimeout(() => controller.abort(), effectiveTimeout)

    try {
      const options = {
        method,
        signal: controller.signal,
        headers: { 'Content-Type': 'application/json' }
      }

      if (body) {
        options.body = JSON.stringify(body)
      }

      const response = await fetch(url, options)
      
      logger.log('HttpClient.response', { method, url, status: response.status })

      // Handle 204 No Content
      if (response.status === 204) {
        return null
      }

      // Handle errors
      if (!response.ok) {
        const error = new Error(`HTTP ${response.status}: ${response.statusText}`)
        error.status = response.status

        try {
          const text = await response.text()
          if (text) {
            const body = JSON.parse(text)
            error.message = body.error || error.message
          }
        } catch (e) {
          // Ignore JSON parse errors
        }

        logger.error('HttpClient.request', { method, url, status: error.status, error: error.message })
        throw error
      }

      // Parse successful response
      const contentType = response.headers.get('content-type')
      const contentLength = response.headers.get('content-length')

      if (!contentType || !contentType.includes('application/json') || contentLength === '0') {
        const text = await response.text()
        if (!text || text.length === 0) {
          return null
        }
        try {
          return JSON.parse(text)
        } catch (e) {
          return null
        }
      }

      return response.json()

    } catch (error) {
      if (error.name === 'AbortError') {
        const timeoutError = new Error(`Request timeout after ${effectiveTimeout}ms`)
        timeoutError.name = 'AbortError'
        timeoutError.timeout = effectiveTimeout
        logger.error('HttpClient.request', { method, url, error: 'timeout', timeout: effectiveTimeout })
        throw timeoutError
      }
      logger.error('HttpClient.request', { method, url, error: error.message })
      throw error
    } finally {
      clearTimeout(timeoutId)
    }
  }

  async #requestWithRetry(method, path, body = null, requestTimeoutMillis = null) {
    let lastError = null

    for (let attempt = 0; attempt < this.#retryAttempts; attempt++) {
      try {
        const url = this.#getUrl() + path
        return await this.#executeRequest(url, method, body, requestTimeoutMillis)
      } catch (error) {
        lastError = error

        // Don't retry on client errors (4xx)
        if (error.status && error.status >= 400 && error.status < 500) {
          throw error
        }

        // Wait before retry (except on last attempt)
        if (attempt < this.#retryAttempts - 1) {
          const delay = this.#retryDelayMillis * Math.pow(2, attempt)
          logger.warn('HttpClient.retry', { method, path, attempt: attempt + 1, delay, error: error.message })
          await new Promise(resolve => setTimeout(resolve, delay))
        }
      }
    }

    logger.error('HttpClient.retry', { method, path, error: 'Max retries exceeded', attempts: this.#retryAttempts })
    throw lastError
  }

  async #requestWithFailover(method, path, body = null, requestTimeoutMillis = null) {
    if (!this.#loadBalancer || !this.#enableFailover) {
      return this.#requestWithRetry(method, path, body, requestTimeoutMillis)
    }

    const urls = this.#loadBalancer.getAllUrls()
    const attemptedUrls = new Set()
    let lastError = null

    logger.log('HttpClient.failover', { method, path, totalServers: urls.length })

    for (let i = 0; i < urls.length; i++) {
      const url = this.#loadBalancer.getNextUrl()

      if (attemptedUrls.has(url)) {
        continue
      }

      attemptedUrls.add(url)

      try {
        return await this.#executeRequest(url + path, method, body, requestTimeoutMillis)
      } catch (error) {
        lastError = error
        logger.warn('HttpClient.failover', { url, method, path, error: error.message })
        console.warn(`Request failed for ${url}: ${method} ${path} - ${error.message}`)

        // Don't retry on client errors (4xx)
        if (error.status && error.status >= 400 && error.status < 500) {
          throw error
        }

        // Continue to next server for server errors or network issues
      }
    }

    logger.error('HttpClient.failover', { method, path, error: 'All servers failed', attempted: attemptedUrls.size })
    throw lastError || new Error('All servers failed')
  }

  #getUrl() {
    if (this.#loadBalancer) {
      return this.#loadBalancer.getNextUrl()
    }
    return this.#baseUrl
  }

  async get(path, requestTimeoutMillis = null) {
    return this.#requestWithFailover('GET', path, null, requestTimeoutMillis)
  }

  async post(path, body = null, requestTimeoutMillis = null) {
    return this.#requestWithFailover('POST', path, body, requestTimeoutMillis)
  }

  async put(path, body = null, requestTimeoutMillis = null) {
    return this.#requestWithFailover('PUT', path, body, requestTimeoutMillis)
  }

  async delete(path, requestTimeoutMillis = null) {
    return this.#requestWithFailover('DELETE', path, null, requestTimeoutMillis)
  }

  getLoadBalancer() {
    return this.#loadBalancer
  }
}

