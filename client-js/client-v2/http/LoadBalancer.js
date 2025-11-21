/**
 * Load balancer for distributing requests across multiple servers
 * Supports round-robin, session, and affinity-based routing with virtual nodes
 */

export class LoadBalancer {
  #urls
  #strategy
  #currentIndex = 0
  #sessionMap = new Map()
  #sessionId
  #healthStatus
  #virtualNodes = null
  #affinityHashRing
  #healthRetryAfterMillis

  constructor(urls, strategy = 'round-robin', options = {}) {
    this.#urls = urls.map(url => url.replace(/\/$/, '')) // Remove trailing slashes
    this.#strategy = strategy
    this.#sessionId = `session_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`
    
    // Virtual node replicas for affinity strategy (default: 150)
    this.#affinityHashRing = options.affinityHashRing || 150
    
    // Health retry interval (default: 30 seconds)
    this.#healthRetryAfterMillis = options.healthRetryAfterMillis || 30000
    
    // Initialize health status with last failure time
    this.#healthStatus = new Map(this.#urls.map(url => [url, { 
      healthy: true, 
      failures: 0,
      lastFailure: null
    }]))
    
    // Build virtual node ring for affinity strategy
    if (strategy === 'affinity') {
      this.#buildVirtualNodeRing()
    }
  }

  /**
   * Build virtual node ring for consistent hashing
   * Each real server gets multiple virtual positions in the hash space
   */
  #buildVirtualNodeRing() {
    this.#virtualNodes = []
    
    for (const url of this.#urls) {
      for (let i = 0; i < this.#affinityHashRing; i++) {
        // Create virtual node identifier
        const vnodeKey = `${url}#vnode${i}`
        const hash = this.#hashString(vnodeKey)
        
        this.#virtualNodes.push({
          hash,
          realServer: url
        })
      }
    }
    
    // Sort by hash value to create the ring
    this.#virtualNodes.sort((a, b) => a.hash - b.hash)
  }

  /**
   * Rebuild virtual node ring with specified URLs
   * @param {Array<string>} urls - URLs to include in the ring (if not provided, uses all healthy URLs)
   */
  #rebuildVirtualNodeRing(urls = null) {
    const urlsToInclude = urls || this.#urls.filter(url => this.#healthStatus.get(url)?.healthy !== false)
    
    this.#virtualNodes = []
    
    for (const url of urlsToInclude) {
      for (let i = 0; i < this.#affinityHashRing; i++) {
        const vnodeKey = `${url}#vnode${i}`
        const hash = this.#hashString(vnodeKey)
        
        this.#virtualNodes.push({
          hash,
          realServer: url
        })
      }
    }
    
    this.#virtualNodes.sort((a, b) => a.hash - b.hash)
  }

  getNextUrl(sessionKey = null) {
    const key = sessionKey || this.#sessionId
    
    // Get healthy URLs + unhealthy URLs that are ready for retry
    const now = Date.now()
    let needRingRebuild = false
    
    const healthyUrls = this.#urls.filter(url => {
      const status = this.#healthStatus.get(url)
      if (status.healthy) return true
      
      // Allow retry of unhealthy backends after configured interval
      if (status.lastFailure && (now - status.lastFailure) >= this.#healthRetryAfterMillis) {
        needRingRebuild = true  // Need to rebuild ring to include this server's vnodes
        return true
      }
      
      return false
    })
    
    // Rebuild vnode ring if we have servers eligible for retry
    // Pass healthyUrls so it includes servers ready for retry
    if (needRingRebuild && this.#virtualNodes) {
      this.#rebuildVirtualNodeRing(healthyUrls)
    }
    
    if (healthyUrls.length === 0) {
      // All backends unhealthy, try any backend as fallback
      return this.#urls[this.#currentIndex % this.#urls.length]
    }

    if (this.#strategy === 'affinity') {
      // Virtual node consistent hashing
      return this.#getAffinityUrl(key, healthyUrls)
    }
    
    if (this.#strategy === 'session') {
      // Session affinity: stick to the same server per session
      if (!this.#sessionMap.has(key)) {
        const assignedIndex = this.#currentIndex
        this.#sessionMap.set(key, assignedIndex)
        this.#currentIndex = (this.#currentIndex + 1) % healthyUrls.length
      }
      
      const assignedUrl = this.#urls[this.#sessionMap.get(key)]
      // If assigned URL is unhealthy, reassign
      if (!healthyUrls.includes(assignedUrl)) {
        const newIndex = this.#currentIndex % healthyUrls.length
        this.#sessionMap.set(key, newIndex)
        this.#currentIndex = (this.#currentIndex + 1) % healthyUrls.length
        return healthyUrls[newIndex]
      }
      
      return assignedUrl
    }

    // Round robin: cycle through healthy URLs only
    const url = healthyUrls[this.#currentIndex % healthyUrls.length]
    this.#currentIndex = (this.#currentIndex + 1) % healthyUrls.length
    return url
  }

  /**
   * Affinity-based routing using consistent hashing with virtual nodes
   * Same key always routes to same backend (unless unhealthy)
   */
  #getAffinityUrl(key, healthyUrls) {
    if (!key || !this.#virtualNodes || this.#virtualNodes.length === 0) {
      // No key or no vnodes, fall back to round-robin
      return healthyUrls[this.#currentIndex++ % healthyUrls.length]
    }
    
    // Hash the consumer group key
    const keyHash = this.#hashString(key)
    
    // Binary search to find the first vnode >= keyHash (clockwise on ring)
    let left = 0
    let right = this.#virtualNodes.length - 1
    let result = 0
    
    while (left <= right) {
      const mid = Math.floor((left + right) / 2)
      if (this.#virtualNodes[mid].hash >= keyHash) {
        result = mid
        right = mid - 1
      } else {
        left = mid + 1
      }
    }
    
    // Walk forward on the ring to find first healthy server
    const startIdx = result
    for (let i = 0; i < this.#virtualNodes.length; i++) {
      const idx = (startIdx + i) % this.#virtualNodes.length
      const vnode = this.#virtualNodes[idx]
      
      if (healthyUrls.includes(vnode.realServer)) {
        return vnode.realServer
      }
    }
    
    // Fallback (should never happen if healthyUrls is not empty)
    return healthyUrls[0]
  }

  /**
   * FNV-1a hash function for consistent hashing
   */
  #hashString(str) {
    let hash = 2166136261 // FNV offset basis
    for (let i = 0; i < str.length; i++) {
      hash ^= str.charCodeAt(i)
      hash += (hash << 1) + (hash << 4) + (hash << 7) + (hash << 8) + (hash << 24)
    }
    return hash >>> 0 // Convert to unsigned 32-bit integer
  }

  /**
   * Mark a backend as unhealthy after failure
   */
  markUnhealthy(url) {
    const status = this.#healthStatus.get(url)
    if (status) {
      status.healthy = false
      status.failures++
      status.lastFailure = Date.now()
      
      const retryInSeconds = (this.#healthRetryAfterMillis / 1000).toFixed(0)
      
      // Rebuild vnode ring to exclude unhealthy server
      if (this.#virtualNodes) {
        this.#rebuildVirtualNodeRing()
      }
    }
  }

  /**
   * Mark a backend as healthy after success
   */
  markHealthy(url) {
    const status = this.#healthStatus.get(url)
    if (!status) return
    
    const wasUnhealthy = status.healthy === false
    
    status.healthy = true
    status.failures = 0
    status.lastFailure = null
    
    // Rebuild vnode ring to include recovered server
    if (this.#virtualNodes && wasUnhealthy) {
      this.#rebuildVirtualNodeRing()
    }
  }

  /**
   * Get health status of all backends
   */
  getHealthStatus() {
    return new Map(this.#healthStatus)
  }

  getAllUrls() {
    return [...this.#urls]
  }

  getStrategy() {
    return this.#strategy
  }
  
  /**
   * Get number of virtual nodes in the ring
   */
  getVirtualNodeCount() {
    return this.#virtualNodes ? this.#virtualNodes.length : 0
  }

  reset() {
    this.#currentIndex = 0
    this.#sessionMap.clear()
  }
}

