/**
 * Utility function to get nested property value from object using dot notation
 */

import type { AggregateConfig, AggregateResult, RawWindow } from '../types'

const getPath = (obj: unknown, path: string): unknown =>
  path.split('.').reduce((o: unknown, k: string) => (o && typeof o === 'object' && k in (o as Record<string, unknown>) ? (o as Record<string, unknown>)[k] : null), obj)

/**
 * Window - Represents a time window of messages with utility methods
 */
export class Window {
  allMessages: readonly unknown[]
  messages: unknown[]
  id?: string
  leaseId?: string;
  [key: string]: unknown

  constructor(rawWindow: RawWindow) {
    Object.assign(this, rawWindow)

    this.allMessages = Object.freeze(rawWindow.messages || [])
    this.messages = [...this.allMessages]
  }

  filter(filterFn: (msg: unknown) => boolean): this {
    this.messages = this.messages.filter(filterFn)
    return this
  }

  groupBy(keyPath: string): Record<string, unknown[]> {
    const groups: Record<string, unknown[]> = {}
    for (const msg of this.messages) {
      const key = (getPath(msg, keyPath) as string) || 'null_key'
      if (!groups[key]) {
        groups[key] = []
      }
      groups[key].push(msg)
    }
    return groups
  }

  aggregate(config: AggregateConfig = {}): AggregateResult {
    const results: AggregateResult = {}

    if (config.count) {
      results.count = this.messages.length
    }

    if (config.sum) {
      results.sum = {}
      for (const path of config.sum) {
        results.sum[path] = this.messages.reduce((total: number, msg: unknown) => {
          const val = getPath(msg, path)
          return total + (typeof val === 'number' ? val : 0)
        }, 0)
      }
    }

    if (config.avg) {
      results.avg = {}
      for (const path of config.avg) {
        const sum = this.messages.reduce((total: number, msg: unknown) => {
          const val = getPath(msg, path)
          return total + (typeof val === 'number' ? val : 0)
        }, 0)
        results.avg[path] = this.messages.length > 0 ? sum / this.messages.length : 0
      }
    }

    if (config.min) {
      results.min = {}
      for (const path of config.min) {
        const values = this.messages
          .map(msg => getPath(msg, path))
          .filter((val): val is number => typeof val === 'number')
        results.min[path] = values.length > 0 ? Math.min(...values) : null
      }
    }

    if (config.max) {
      results.max = {}
      for (const path of config.max) {
        const values = this.messages
          .map(msg => getPath(msg, path))
          .filter((val): val is number => typeof val === 'number')
        results.max[path] = values.length > 0 ? Math.max(...values) : null
      }
    }

    return results
  }

  reset(): this {
    this.messages = [...this.allMessages]
    return this
  }

  size(): number {
    return this.messages.length
  }

  originalSize(): number {
    return this.allMessages.length
  }
}
