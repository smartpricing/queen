import { createHttpClient, createLoadBalancedHttpClient } from './utils/http.js';
import { withRetry } from './utils/retry.js';
import { createLoadBalancer, LoadBalancingStrategy } from './utils/loadBalancer.js';

/**
 * Minimalist Queen Message Queue Client
 * 
 * Simple, powerful API with just 4 methods:
 * - queue: Configure a queue
 * - push: Send messages
 * - take: Receive messages (async iterator)
 * - ack: Acknowledge messages
 */
export class Queen {
  #config;
  #http;
  #loadBalancer;
  #connected = false;
  
  constructor(config = {}) {
    this.#config = {
      baseUrls: null,
      loadBalancingStrategy: LoadBalancingStrategy.ROUND_ROBIN,
      enableFailover: true,
      timeout: 30000,
      retryAttempts: 3,
      retryDelay: 1000,
      ...config
    };
  }
  
  /**
   * Ensure HTTP client is connected
   */
  async #ensureConnected() {
    if (this.#connected) return;
    
    const { baseUrl, baseUrls, loadBalancingStrategy, enableFailover, timeout } = this.#config;
    
    if (baseUrls && Array.isArray(baseUrls) && baseUrls.length > 0) {
      // Multiple servers with load balancing
      this.#loadBalancer = createLoadBalancer(baseUrls, loadBalancingStrategy);
      this.#http = createLoadBalancedHttpClient({ 
        baseUrls, 
        loadBalancer: this.#loadBalancer, 
        timeout,
        enableFailover 
      });
    } else {
      // Single server
      const singleUrl = baseUrls && baseUrls.length === 1 ? baseUrls[0] : baseUrl;
      this.#http = createHttpClient({ baseUrl: singleUrl, timeout });
    }
    
    this.#connected = true;
  }
  
  /**
   * Validate if a string is a valid UUID v4
   */
  #isValidUUID(str) {
    const uuidRegex = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;
    return uuidRegex.test(str);
  }
  
  /**
   * Parse address string into components
   * Examples:
   * - "myqueue" -> { queue: "myqueue" }
   * - "myqueue/urgent" -> { queue: "myqueue", partition: "urgent" }
   * - "myqueue@workers" -> { queue: "myqueue", consumerGroup: "workers" }
   * - "myqueue/urgent@workers" -> { queue: "myqueue", partition: "urgent", consumerGroup: "workers" }
   * - "namespace:billing" -> { namespace: "billing" }
   * - "task:process" -> { task: "process" }
   * - "namespace:billing/task:process" -> { namespace: "billing", task: "process" }
   */
  #parseAddress(address) {
    // Check for namespace/task pattern
    if (address.includes('namespace:') || address.includes('task:')) {
      const parts = {};
      
      // Extract consumer group if present (after @)
      let workingAddress = address;
      const atIndex = address.lastIndexOf('@');
      if (atIndex > 0) {
        parts.consumerGroup = address.substring(atIndex + 1);
        workingAddress = address.substring(0, atIndex);
      }
      
      const segments = workingAddress.split('/');
      
      for (const segment of segments) {
        if (segment.startsWith('namespace:')) {
          parts.namespace = segment.substring(10);
        } else if (segment.startsWith('task:')) {
          parts.task = segment.substring(5);
        }
      }
      
      return parts;
    }
    
    // Standard queue[/partition][@group] pattern
    const match = address.match(/^([^/@]+)(?:\/([^@]+))?(?:@(.+))?$/);
    if (!match) {
      throw new Error(`Invalid address format: ${address}`);
    }
    
    return {
      queue: match[1],
      partition: match[2] || 'Default',
      consumerGroup: match[3] || null
    };
  }
  
  /**
   * Configure a queue with options
   * @param {string} name - Queue name
   * @param {Object} options - Queue configuration options
   * @param {Object} metadata - Optional metadata { namespace, task }
   */
  async queue(name, options = {}, metadata = {}) {
    await this.#ensureConnected();
    
    const { namespace, task } = metadata;
    
    const result = await withRetry(
      () => this.#http.post('/api/v1/configure', { 
        queue: name,
        namespace: namespace || null,
        task: task || null,
        options 
      }),
      this.#config.retryAttempts,
      this.#config.retryDelay
    );
    
    if (result && result.error) {
      throw new Error(result.error);
    }
    
    return result;
  }
  
  /**
   * Push messages to a queue
   * @param {string} address - Queue address (e.g., "myqueue" or "myqueue/partition")
   * @param {Object|Array} payload - Single message or array of messages
   * @param {Object} options - Optional message properties { transactionId, traceId }
   */
  async push(address, payload, options = {}) {
    await this.#ensureConnected();
    
    const { queue, partition } = this.#parseAddress(address);
    
    // Handle both single and array inputs
    const items = Array.isArray(payload) ? payload : [payload];
    
    // Format items for the API
    const formattedItems = items.map(item => {
      // If item is an object with special properties, extract them
      const isMessageObject = typeof item === 'object' && item !== null &&
        (item._payload || item._transactionId || item._traceId);
      
      if (isMessageObject) {
        const result = {
          queue,
          partition,
          payload: item._payload || item,
          transactionId: item._transactionId || item.transactionId
        };
        
        // Include traceId if provided and valid UUID
        const traceId = item._traceId || item.traceId;
        if (traceId && this.#isValidUUID(traceId)) {
          result.traceId = traceId;
        }
        
        return result;
      }
      
      // Otherwise use the item as payload and apply options
      const result = {
        queue,
        partition,
        payload: item,
        transactionId: options.transactionId || (item && typeof item === 'object' ? item.transactionId : undefined)
      };
      
      // Include traceId if provided and valid UUID
      const traceId = options.traceId || (item && typeof item === 'object' ? item.traceId : undefined);
      if (traceId && this.#isValidUUID(traceId)) {
        result.traceId = traceId;
      }
      
      return result;
    });
    
    const result = await withRetry(
      () => this.#http.post('/api/v1/push', { 
        items: formattedItems 
      }),
      this.#config.retryAttempts,
      this.#config.retryDelay
    );
    
    if (result && result.error) {
      throw new Error(result.error);
    }
    
    return result;
  }
  
  /**
   * Internal method that yields batches of messages (arrays)
   * @private
   * @param {string} address - Queue address
   * @param {Object} options - Options for taking messages
   * @yields {Array} Arrays of message objects
   */
  async *#takeInternal(address, options = {}) {
    await this.#ensureConnected();
    
    const { queue, partition, consumerGroup, namespace, task } = this.#parseAddress(address);
    const { 
      limit = null,
      batch = 1, 
      wait = false, 
      timeout = 30000,
      subscriptionMode = null,
      subscriptionFrom = null,
      idleTimeout = null
    } = options;
    
    let totalCount = 0;
    let consecutiveEmptyResponses = 0;
    let consecutiveNetworkErrors = 0;
    const maxConsecutiveEmpty = 3;
    let lastMessageTime = idleTimeout ? Date.now() : null;
    
    while (true) {
      // Check if we've reached the limit
      if (limit && totalCount >= limit) break;
      
      // Check idle timeout
      if (idleTimeout && lastMessageTime) {
        const idleTime = Date.now() - lastMessageTime;
        if (idleTime >= idleTimeout) {
          break; // Exit if idle time exceeded
        }
      }
      
      // Calculate batch size respecting the limit
      const effectiveBatch = limit ? Math.min(batch, limit - totalCount) : batch;
      
      // Build the request path and parameters
      let path;
      const params = new URLSearchParams({
        wait: wait.toString(),
        timeout: timeout.toString(),
        batch: effectiveBatch.toString()
      });
      
      // Add consumer group parameters if provided
      if (consumerGroup) params.append('consumerGroup', consumerGroup);
      if (subscriptionMode) params.append('subscriptionMode', subscriptionMode);
      if (subscriptionFrom) params.append('subscriptionFrom', subscriptionFrom);
      
      // Determine the endpoint based on parameters
      if (queue) {
        if (partition && partition !== 'Default') {
          path = `/api/v1/pop/queue/${queue}/partition/${partition}`;
        } else {
          path = `/api/v1/pop/queue/${queue}`;
        }
      } else if (namespace || task) {
        // Pop by namespace/task
        path = '/api/v1/pop';
        if (namespace) params.append('namespace', namespace);
        if (task) params.append('task', task);
      } else {
        throw new Error('Must specify either queue, namespace, or task');
      }
      
      try {
        // For long polling, use a slightly longer client timeout
        const clientTimeout = wait ? timeout + 5000 : timeout;
        
        const result = await this.#http.get(`${path}?${params}`, clientTimeout);
        
        // Handle empty response
        if (!result || !result.messages || result.messages.length === 0) {
          if (wait) {
            // For long polling, immediately retry
            continue;
          } else {
            // For non-waiting mode, stop after several empty responses
            consecutiveEmptyResponses++;
            if (consecutiveEmptyResponses >= maxConsecutiveEmpty) {
              break;
            }
            // Small delay before retry
            await new Promise(resolve => setTimeout(resolve, 100));
            continue;
          }
        }
        
        // Reset counters on successful fetch
        consecutiveEmptyResponses = 0;
        consecutiveNetworkErrors = 0;
        
        // Update last message time if tracking idle timeout
        if (idleTimeout) {
          lastMessageTime = Date.now();
        }
        
        // Filter out null/undefined messages
        const messages = result.messages.filter(msg => msg != null);
        
        if (messages.length > 0) {
          totalCount += messages.length;
          yield messages;
          
          // If we've hit the limit, stop
          if (limit && totalCount >= limit) {
            break;
          }
        }
        
      } catch (error) {
        // Check if this is a timeout error (expected for long polling)
        const isTimeoutError = error.name === 'AbortError' || 
                              error.message?.includes('abort') ||
                              error.message?.includes('timeout');
        
        if (isTimeoutError && wait) {
          // For long polling timeout, immediately retry
          continue;
        }
        
        // Check if this is a network error (connection refused, socket closed, etc.)
        const isNetworkError = error.message?.includes('fetch failed') ||
                              error.message?.includes('ECONNREFUSED') ||
                              error.message?.includes('ECONNRESET') ||
                              error.message?.includes('closed') ||
                              error.cause?.code === 'UND_ERR_SOCKET' ||
                              error.code === 'ECONNREFUSED' ||
                              error.code === 'ECONNRESET';
        
        if (isNetworkError) {
          // Log the network error
          console.warn(`Network error while polling queue: ${error.message}`);
          
          // Retry with exponential backoff (capped at 30 seconds)
          const retryDelay = Math.min(this.#config.retryDelay * Math.pow(2, Math.min(consecutiveNetworkErrors, 10)), 30000);
          console.warn(`Retrying in ${retryDelay}ms... (attempt ${consecutiveNetworkErrors + 1})`);
          await new Promise(resolve => setTimeout(resolve, retryDelay));
          
          consecutiveNetworkErrors++;
          
          // Continue retrying indefinitely
          continue;
        }
        
        // For other errors, throw
        throw error;
      }
    }
  }
  
  /**
   * Take messages from a queue one at a time (async iterator)
   * @param {string} address - Queue address (e.g., "myqueue", "myqueue/partition", "myqueue@group")
   * @param {Object} options - Options for taking messages
   * @yields {Object} Individual message objects
   */
  async *take(address, options = {}) {
    let count = 0;
    const { limit = null } = options;
    
    for await (const messages of this.#takeInternal(address, options)) {
      for (const message of messages) {
        yield message;
        count++;
        
        // Double-check limit (internal method also checks, but this ensures exact limit)
        if (limit && count >= limit) {
          return;
        }
      }
    }
  }
  
  /**
   * Take messages from a queue in batches (async iterator)
   * @param {string} address - Queue address (e.g., "myqueue", "myqueue/partition", "myqueue@group")
   * @param {Object} options - Options for taking messages
   * @yields {Array} Arrays of message objects
   */
  async *takeBatch(address, options = {}) {
    for await (const messages of this.#takeInternal(address, options)) {
      // Only yield non-empty batches
      if (messages && messages.length > 0) {
        yield messages;
      }
    }
  }
  
  /**
   * Acknowledge a message or batch of messages
   * @param {Object|string|Array} message - Message object, transaction ID, or array of messages
   * @param {boolean|string} status - true for success, false for failure, or 'retry'
   * @param {Object} context - Optional context (e.g., { group: 'workers', error: 'reason' })
   */
  async ack(message, status = true, context = {}) {
    await this.#ensureConnected();
    
    // Handle batch acknowledgment
    if (Array.isArray(message)) {
      if (message.length === 0) {
        return { processed: 0, results: [] };
      }
      
      // Check if messages have individual status (Option B pattern)
      const hasIndividualStatus = message.some(msg => 
        typeof msg === 'object' && msg !== null && ('_status' in msg || '_error' in msg)
      );
      
      let acknowledgments;
      
      if (hasIndividualStatus) {
        // Option B: Each message has its own status
        acknowledgments = message.map(msg => {
          // Extract transaction ID
          let transactionId;
          if (typeof msg === 'string') {
            transactionId = msg;
          } else if (typeof msg === 'object' && msg !== null) {
            transactionId = msg.transactionId || msg.id;
            if (!transactionId) {
              throw new Error('Message object must have transactionId or id property');
            }
          } else {
            throw new Error('Invalid message in batch');
          }
          
          // Get individual status or fall back to parameter
          let msgStatus = msg._status !== undefined ? msg._status : status;
          let statusStr;
          if (typeof msgStatus === 'boolean') {
            statusStr = msgStatus ? 'completed' : 'failed';
          } else {
            statusStr = msgStatus;
          }
          
          return {
            transactionId,
            status: statusStr,
            error: msg._error || context.error || null
          };
        });
      } else {
        // Option A: Same status for all messages
        const statusStr = typeof status === 'boolean' 
          ? (status ? 'completed' : 'failed')
          : status;
        
        acknowledgments = message.map(msg => {
          // Extract transaction ID
          let transactionId;
          if (typeof msg === 'string') {
            transactionId = msg;
          } else if (typeof msg === 'object' && msg !== null) {
            transactionId = msg.transactionId || msg.id;
            if (!transactionId) {
              throw new Error('Message object must have transactionId or id property');
            }
          } else {
            throw new Error('Invalid message in batch');
          }
          
          return {
            transactionId,
            status: statusStr,
            error: context.error || null
          };
        });
      }
      
      // Call batch ack endpoint
      const result = await withRetry(
        () => this.#http.post('/api/v1/ack/batch', { 
          acknowledgments,
          consumerGroup: context.group || null
        }),
        this.#config.retryAttempts,
        this.#config.retryDelay
      );
      
      if (result && result.error) {
        throw new Error(result.error);
      }
      
      return result;
    }
    
    // Handle single message acknowledgment
    // Extract transaction ID
    let transactionId;
    if (typeof message === 'string') {
      transactionId = message;
    } else if (typeof message === 'object' && message !== null) {
      transactionId = message.transactionId || message.id;
      if (!transactionId) {
        throw new Error('Message object must have transactionId or id property');
      }
    } else {
      throw new Error('Message must be a string (transaction ID) or object with transactionId');
    }
    
    // Determine status string
    let statusStr;
    if (typeof status === 'boolean') {
      statusStr = status ? 'completed' : 'failed';
    } else {
      statusStr = status; // Allow custom status like 'retry'
    }
    
    // Build request body
    const body = {
      transactionId,
      status: statusStr,
      error: context.error || null,
      consumerGroup: context.group || null
    };
    
    const result = await withRetry(
      () => this.#http.post('/api/v1/ack', body),
      this.#config.retryAttempts,
      this.#config.retryDelay
    );
    
    if (result && result.error) {
      throw new Error(result.error);
    }
    
    return result;
  }
  
  /**
   * Delete a queue (removes queue and all its messages/partitions)
   * @param {string} name - Queue name to delete
   * @returns {Promise<Object>} Deletion result
   */
  async queueDelete(name) {
    await this.#ensureConnected();
    
    if (typeof name !== 'string' || !name) {
      throw new Error('Queue name must be a non-empty string');
    }
    
    const result = await withRetry(
      () => this.#http.delete(`/api/v1/resources/queues/${encodeURIComponent(name)}`),
      this.#config.retryAttempts,
      this.#config.retryDelay
    );
    
    if (result && result.error) {
      throw new Error(result.error);
    }
    
    return result;
  }
  
  /**
   * Close the client connection
   */
  async close() {
    this.#connected = false;
    this.#http = null;
    this.#loadBalancer = null;
  }
}

// Export the class as default as well for convenience
export default Queen;