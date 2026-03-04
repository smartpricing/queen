/**
 * Load balancer for distributing requests across multiple servers
 * Supports round-robin, session, and affinity-based routing with virtual nodes
 */

import type { LoadBalancingStrategy, LoadBalancerOptions, HealthStatus } from '../types'

interface VirtualNode {
  hash: number
  realServer: string
}

export class LoadBalancer {
  private urls: string[]
  private strategy: LoadBalancingStrategy
  private currentIndex: number = 0
  private sessionMap: Map<string, number> = new Map()
  private sessionId: string
  private healthStatus: Map<string, HealthStatus>
  private virtualNodes: VirtualNode[] | null = null
  private affinityHashRing: number
  private healthRetryAfterMillis: number

  constructor(urls: string[], strategy: LoadBalancingStrategy = 'round-robin', options: LoadBalancerOptions = {}) {
    this.urls = urls.map(url => url.replace(/\/$/, ''))
    this.strategy = strategy
    this.sessionId = `session_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`

    this.affinityHashRing = options.affinityHashRing || 150
    this.healthRetryAfterMillis = options.healthRetryAfterMillis || 30000

    this.healthStatus = new Map(this.urls.map(url => [url, {
      healthy: true,
      failures: 0,
      lastFailure: null
    }]))

    if (strategy === 'affinity') {
      this.buildVirtualNodeRing()
    }
  }

  private buildVirtualNodeRing(): void {
    this.virtualNodes = []

    for (const url of this.urls) {
      for (let i = 0; i < this.affinityHashRing; i++) {
        const vnodeKey = `${url}#vnode${i}`
        const hash = this.hashString(vnodeKey)

        this.virtualNodes.push({
          hash,
          realServer: url
        })
      }
    }

    this.virtualNodes.sort((a, b) => a.hash - b.hash)
  }

  private rebuildVirtualNodeRing(urls: string[] | null = null): void {
    const urlsToInclude = urls || this.urls.filter(url => this.healthStatus.get(url)?.healthy !== false)

    this.virtualNodes = []

    for (const url of urlsToInclude) {
      for (let i = 0; i < this.affinityHashRing; i++) {
        const vnodeKey = `${url}#vnode${i}`
        const hash = this.hashString(vnodeKey)

        this.virtualNodes.push({
          hash,
          realServer: url
        })
      }
    }

    this.virtualNodes.sort((a, b) => a.hash - b.hash)
  }

  getNextUrl(sessionKey: string | null = null): string {
    const key = sessionKey || this.sessionId

    const now = Date.now()
    let needRingRebuild = false

    const healthyUrls = this.urls.filter(url => {
      const status = this.healthStatus.get(url)!
      if (status.healthy) return true

      if (status.lastFailure && (now - status.lastFailure) >= this.healthRetryAfterMillis) {
        needRingRebuild = true
        return true
      }

      return false
    })

    if (needRingRebuild && this.virtualNodes) {
      this.rebuildVirtualNodeRing(healthyUrls)
    }

    if (healthyUrls.length === 0) {
      return this.urls[this.currentIndex % this.urls.length]
    }

    if (this.strategy === 'affinity') {
      return this.getAffinityUrl(key, healthyUrls)
    }

    if (this.strategy === 'session') {
      if (!this.sessionMap.has(key)) {
        const assignedIndex = this.currentIndex
        this.sessionMap.set(key, assignedIndex)
        this.currentIndex = (this.currentIndex + 1) % healthyUrls.length
      }

      const assignedUrl = this.urls[this.sessionMap.get(key)!]
      if (!healthyUrls.includes(assignedUrl)) {
        const newIndex = this.currentIndex % healthyUrls.length
        this.sessionMap.set(key, newIndex)
        this.currentIndex = (this.currentIndex + 1) % healthyUrls.length
        return healthyUrls[newIndex]
      }

      return assignedUrl
    }

    // Round robin
    const url = healthyUrls[this.currentIndex % healthyUrls.length]
    this.currentIndex = (this.currentIndex + 1) % healthyUrls.length
    return url
  }

  private getAffinityUrl(key: string, healthyUrls: string[]): string {
    if (!key || !this.virtualNodes || this.virtualNodes.length === 0) {
      return healthyUrls[this.currentIndex++ % healthyUrls.length]
    }

    const keyHash = this.hashString(key)

    let left = 0
    let right = this.virtualNodes.length - 1
    let result = 0

    while (left <= right) {
      const mid = Math.floor((left + right) / 2)
      if (this.virtualNodes[mid].hash >= keyHash) {
        result = mid
        right = mid - 1
      } else {
        left = mid + 1
      }
    }

    const startIdx = result
    for (let i = 0; i < this.virtualNodes.length; i++) {
      const idx = (startIdx + i) % this.virtualNodes.length
      const vnode = this.virtualNodes[idx]

      if (healthyUrls.includes(vnode.realServer)) {
        return vnode.realServer
      }
    }

    return healthyUrls[0]
  }

  private hashString(str: string): number {
    let hash = 2166136261
    for (let i = 0; i < str.length; i++) {
      hash ^= str.charCodeAt(i)
      hash += (hash << 1) + (hash << 4) + (hash << 7) + (hash << 8) + (hash << 24)
    }
    return hash >>> 0
  }

  markUnhealthy(url: string): void {
    const status = this.healthStatus.get(url)
    if (status) {
      status.healthy = false
      status.failures++
      status.lastFailure = Date.now()

      if (this.virtualNodes) {
        this.rebuildVirtualNodeRing()
      }
    }
  }

  markHealthy(url: string): void {
    const status = this.healthStatus.get(url)
    if (!status) return

    const wasUnhealthy = status.healthy === false

    status.healthy = true
    status.failures = 0
    status.lastFailure = null

    if (this.virtualNodes && wasUnhealthy) {
      this.rebuildVirtualNodeRing()
    }
  }

  getHealthStatus(): Map<string, HealthStatus> {
    return new Map(this.healthStatus)
  }

  getAllUrls(): string[] {
    return [...this.urls]
  }

  getStrategy(): LoadBalancingStrategy {
    return this.strategy
  }

  getVirtualNodeCount(): number {
    return this.virtualNodes ? this.virtualNodes.length : 0
  }

  reset(): void {
    this.currentIndex = 0
    this.sessionMap.clear()
  }
}
