/**
 * StreamBuilder - Fluent API for defining streams
 */

import type { HttpClient } from '../http/HttpClient'
import type { Queen } from '../Queen'
import type { StreamConfig } from '../types'

export class StreamBuilder {
  private httpClient: HttpClient
  private queen: Queen
  private config: StreamConfig

  constructor(httpClient: HttpClient, queen: Queen, name: string, namespace?: string) {
    this.httpClient = httpClient
    this.queen = queen
    this.config = {
      name,
      namespace,
      source_queue_names: [],
      partitioned: false,
      window_type: 'tumbling',
      window_duration_ms: 60000,
      window_grace_period_ms: 30000,
      window_lease_timeout_ms: 60000
    }
  }

  sources(queueNames: string[] = []): this {
    this.config.source_queue_names = queueNames
    return this
  }

  partitioned(): this {
    this.config.partitioned = true
    return this
  }

  tumblingTime(seconds: number): this {
    this.config.window_type = 'tumbling'
    this.config.window_duration_ms = seconds * 1000
    return this
  }

  gracePeriod(seconds: number): this {
    this.config.window_grace_period_ms = seconds * 1000
    return this
  }

  leaseTimeout(seconds: number): this {
    this.config.window_lease_timeout_ms = seconds * 1000
    return this
  }

  async define(): Promise<unknown> {
    return this.httpClient.post('/api/v1/stream/define', this.config)
  }
}
