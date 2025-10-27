/**
 * Load balancer for distributing requests across multiple servers
 */

export class LoadBalancer {
  #urls
  #strategy
  #currentIndex = 0
  #sessionMap = new Map()
  #sessionId

  constructor(urls, strategy = 'round-robin') {
    this.#urls = urls.map(url => url.replace(/\/$/, '')) // Remove trailing slashes
    this.#strategy = strategy
    this.#sessionId = `session_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`
  }

  getNextUrl(sessionKey = null) {
    const key = sessionKey || this.#sessionId

    if (this.#strategy === 'session') {
      // Session affinity: stick to the same server per session
      if (!this.#sessionMap.has(key)) {
        const assignedIndex = this.#currentIndex
        this.#sessionMap.set(key, assignedIndex)
        this.#currentIndex = (this.#currentIndex + 1) % this.#urls.length
      }
      return this.#urls[this.#sessionMap.get(key)]
    }

    // Round robin: cycle through URLs
    const url = this.#urls[this.#currentIndex]
    this.#currentIndex = (this.#currentIndex + 1) % this.#urls.length
    return url
  }

  getAllUrls() {
    return [...this.#urls]
  }

  getStrategy() {
    return this.#strategy
  }

  reset() {
    this.#currentIndex = 0
    this.#sessionMap.clear()
  }
}

