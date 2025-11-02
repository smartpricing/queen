/**
 * StreamBuilder - Fluent API for defining streams
 */
export class StreamBuilder {
  constructor(httpClient, queen, name, namespace) {
    this.httpClient = httpClient;
    this.queen = queen;
    this.config = {
      name,
      namespace,
      source_queue_names: [],
      partitioned: false,
      window_type: 'tumbling',
      window_duration_ms: 60000,       // 1 minute default
      window_grace_period_ms: 30000,   // 30 seconds default
      window_lease_timeout_ms: 60000   // 1 minute default
    };
  }

  /**
   * Set the source queues for this stream
   * @param {string[]} queueNames - Array of queue names
   */
  sources(queueNames = []) {
    this.config.source_queue_names = queueNames;
    return this;
  }
  
  /**
   * Enable partitioned processing (group by partition_id)
   */
  partitioned() {
    this.config.partitioned = true;
    return this;
  }

  /**
   * Configure tumbling time window
   * @param {number} seconds - Window duration in seconds
   */
  tumblingTime(seconds) {
    this.config.window_type = 'tumbling';
    this.config.window_duration_ms = seconds * 1000;
    return this;
  }
  
  /**
   * Configure grace period for late-arriving messages
   * @param {number} seconds - Grace period in seconds
   */
  gracePeriod(seconds) {
    this.config.window_grace_period_ms = seconds * 1000;
    return this;
  }
  
  /**
   * Configure lease timeout
   * @param {number} seconds - Lease timeout in seconds
   */
  leaseTimeout(seconds) {
    this.config.window_lease_timeout_ms = seconds * 1000;
    return this;
  }

  /**
   * Define/create the stream on the server
   */
  async define() {
    return this.httpClient.post('/api/v1/stream/define', this.config);
  }
}

