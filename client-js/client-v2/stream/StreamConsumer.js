import { Window } from './Window.js';

/**
 * StreamConsumer - Manages consuming windows from a stream
 */
export class StreamConsumer {
  constructor(httpClient, queen, streamName, consumerGroup) {
    this.httpClient = httpClient;
    this.queen = queen;
    this.streamName = streamName;
    this.consumerGroup = consumerGroup;
    this.pollTimeout = 30000;        // 30s long poll
    this.leaseRenewInterval = 20000; // 20s renewal (before 60s timeout)
    this.running = false;
  }

  /**
   * Start processing windows with the provided callback
   * Runs in an infinite loop until stopped
   * @param {Function} callback - async function(window) to process each window
   */
  async process(callback) {
    this.running = true;
    
    console.log(`[StreamConsumer] Starting consumer: stream=${this.streamName}, group=${this.consumerGroup}`);
    
    while (this.running) {
      let window = null;
      
      try {
        console.log(`[StreamConsumer] Polling for window...`);
        window = await this.pollWindow();

        if (!window) {
          // 204 No Content - no window available
          console.log(`[StreamConsumer] No window available (204)`);
          continue;
        }

        console.log(`[StreamConsumer] Received window: ${window.id}, messages=${window.allMessages.length}`);
        await this.executeCallback(window, callback);

      } catch (err) {
        console.error('[StreamConsumer] Processing error:', err);
        // Backoff on error
        await new Promise(r => setTimeout(r, 1000));
      }
    }
    
    console.log(`[StreamConsumer] Consumer stopped`);
  }

  /**
   * Stop the processing loop
   */
  stop() {
    this.running = false;
  }

  /**
   * Poll for a window (blocking call)
   * @returns {Promise<Window|null>} Window or null if no content
   */
  async pollWindow() {
    try {
      const response = await this.httpClient.post('/api/v1/stream/poll', {
        streamName: this.streamName,
        consumerGroup: this.consumerGroup,
        timeout: this.pollTimeout
      });
      
      // Handle 204 No Content (HttpClient returns null for 204)
      if (!response) {
        return null;
      }
      
      // HttpClient returns the JSON body directly (e.g., {"window": {...}})
      if (!response.window) {
        return null;
      }
      
      return new Window(response.window);
      
    } catch (err) {
      // HttpClient throws on errors, returns null on 204
      // So any error here is a real network/server error
      throw err;
    }
  }

  /**
   * Execute the callback with lease renewal
   * @param {Window} window - The window to process
   * @param {Function} callback - User callback
   */
  async executeCallback(window, callback) {
    let leaseTimer = null;
    let leaseExpired = false;
    
    try {
      // Start lease renewal timer
      leaseTimer = setInterval(async () => {
        try {
          await this.httpClient.post('/api/v1/stream/renew-lease', { 
            leaseId: window.leaseId,
            extend_ms: this.leaseRenewInterval + 10000  // Extend by renewal interval + buffer
          });
        } catch (e) {
          leaseExpired = true;
          console.warn(`[StreamConsumer] Lease ${window.leaseId} failed to renew:`, e.message);
        }
      }, this.leaseRenewInterval);

      // Execute user callback
      await callback(window);
      
      // ACK if lease hasn't expired
      if (!leaseExpired) {
        await this.httpClient.post('/api/v1/stream/ack', {
          windowId: window.id,
          leaseId: window.leaseId,
          success: true
        });
      } else {
        console.warn(`[StreamConsumer] Window ${window.id} processed but lease expired, not ACKing`);
      }

    } catch (err) {
      console.error(`[StreamConsumer] Failed to process window ${window.id}:`, err);
      
      // NACK if lease hasn't expired
      if (!leaseExpired) {
        try {
          await this.httpClient.post('/api/v1/stream/ack', {
            windowId: window.id,
            leaseId: window.leaseId,
            success: false  // NACK
          });
        } catch (nackErr) {
          console.error(`[StreamConsumer] Failed to NACK window ${window.id}:`, nackErr);
        }
      }
      
      // Re-throw to trigger backoff
      throw err;
      
    } finally {
      // Stop lease renewal
      if (leaseTimer) {
        clearInterval(leaseTimer);
      }
    }
  }

  /**
   * Seek to a specific timestamp
   * @param {string} timestamp - ISO timestamp to seek to
   */
  async seek(timestamp) {
    return this.httpClient.post('/api/v1/stream/seek', {
      streamName: this.streamName,
      consumerGroup: this.consumerGroup,
      timestamp
    });
  }
}

