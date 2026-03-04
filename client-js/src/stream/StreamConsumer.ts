import { Window } from './Window'
import type { HttpClient } from '../http/HttpClient'
import type { Queen } from '../Queen'

/**
 * StreamConsumer - Manages consuming windows from a stream
 */
export class StreamConsumer {
  private httpClient: HttpClient
  private queen: Queen
  private streamName: string
  private consumerGroup: string
  private pollTimeout: number = 30000
  private leaseRenewInterval: number = 20000
  private running: boolean = false

  constructor(httpClient: HttpClient, queen: Queen, streamName: string, consumerGroup: string) {
    this.httpClient = httpClient
    this.queen = queen
    this.streamName = streamName
    this.consumerGroup = consumerGroup
  }

  async process(callback: (window: Window) => Promise<void>): Promise<void> {
    this.running = true

    while (this.running) {
      try {
        const window = await this.pollWindow()

        if (!window) {
          continue
        }

        await this.executeCallback(window, callback)

      } catch (_err) {
        await new Promise(r => setTimeout(r, 1000))
      }
    }
  }

  stop(): void {
    this.running = false
  }

  async pollWindow(): Promise<Window | null> {
    try {
      const response = await this.httpClient.post('/api/v1/stream/poll', {
        streamName: this.streamName,
        consumerGroup: this.consumerGroup,
        timeout: this.pollTimeout
      }) as { window?: Record<string, unknown> } | null

      if (!response) {
        return null
      }

      if (!response.window) {
        return null
      }

      return new Window(response.window as import('../types').RawWindow)

    } catch (err) {
      throw err
    }
  }

  async executeCallback(window: Window, callback: (window: Window) => Promise<void>): Promise<void> {
    let leaseTimer: ReturnType<typeof setInterval> | null = null
    let leaseExpired = false

    try {
      leaseTimer = setInterval(async () => {
        try {
          await this.httpClient.post('/api/v1/stream/renew-lease', {
            leaseId: window.leaseId,
            extend_ms: this.leaseRenewInterval + 10000
          })
        } catch (_e) {
          leaseExpired = true
        }
      }, this.leaseRenewInterval)

      await callback(window)

      if (!leaseExpired) {
        await this.httpClient.post('/api/v1/stream/ack', {
          windowId: window.id,
          leaseId: window.leaseId,
          success: true
        })
      }

    } catch (err) {
      if (!leaseExpired) {
        try {
          await this.httpClient.post('/api/v1/stream/ack', {
            windowId: window.id,
            leaseId: window.leaseId,
            success: false
          })
        } catch (_nackErr) {
          // ignore
        }
      }

      throw err

    } finally {
      if (leaseTimer) {
        clearInterval(leaseTimer)
      }
    }
  }

  async seek(timestamp: string): Promise<unknown> {
    return this.httpClient.post('/api/v1/stream/seek', {
      streamName: this.streamName,
      consumerGroup: this.consumerGroup,
      timestamp
    })
  }
}
