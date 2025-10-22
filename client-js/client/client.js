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
    
    // Build request body
    const requestBody = { items: formattedItems };
    
    // Add buffer options if specified (QoS 0)
    if (options.bufferMs !== undefined || options.bufferMax !== undefined) {
      requestBody.bufferMs = options.bufferMs !== undefined ? options.bufferMs : 100;
      requestBody.bufferMax = options.bufferMax !== undefined ? options.bufferMax : 100;
    }
    
    const result = await withRetry(
      () => this.#http.post('/api/v1/push', requestBody),
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
      
      // Add auto-ack option if specified
      if (options.autoAck) params.append('autoAck', 'true');
      
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
   * Take a single batch of messages (convenience method)
   * @param {string} address - Queue address
   * @param {Object} options - Options for taking messages
   * @returns {Promise<Array>} Array of messages
   */
  async takeSingleBatch(address, options = {}) {
    const messages = [];
    for await (const batch of this.takeBatch(address, options)) {
      messages.push(...batch);
      break; // Take only one batch
    }
    return messages;
  }
  
  /**
   * Renew lease for a message or batch of messages
   * @param {Object|Array|string} messageOrLeaseId - Message object(s) or lease ID(s)
   * @returns {Promise<Object>} Renewal result
   */
  async renewLease(messageOrLeaseId) {
    await this.#ensureConnected();
    
    // Handle different input types
    let leaseIds = [];
    
    if (typeof messageOrLeaseId === 'string') {
      // Direct lease ID
      leaseIds = [messageOrLeaseId];
    } else if (Array.isArray(messageOrLeaseId)) {
      // Array of messages or lease IDs
      leaseIds = messageOrLeaseId.map(item => 
        typeof item === 'string' ? item : item.leaseId
      ).filter(Boolean);
    } else if (messageOrLeaseId && typeof messageOrLeaseId === 'object') {
      // Single message object
      if (messageOrLeaseId.leaseId) {
        leaseIds = [messageOrLeaseId.leaseId];
      }
    }
    
    if (leaseIds.length === 0) {
      throw new Error('No valid lease IDs found for renewal');
    }
    
    // Renew all leases
    const results = [];
    for (const leaseId of leaseIds) {
      try {
        const result = await withRetry(
          () => this.#http.post(`/api/v1/lease/${leaseId}/extend`, {}),
          this.#config.retryAttempts,
          this.#config.retryDelay
        );
        results.push({ 
          leaseId, 
          success: true, 
          newExpiresAt: result.leaseId ? result.newExpiresAt : result.lease_expires_at 
        });
      } catch (error) {
        results.push({ leaseId, success: false, error: error.message });
      }
    }
    
    // Return single result for single input, array for array input
    return Array.isArray(messageOrLeaseId) ? results : results[0];
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
          // Extract transaction ID and lease ID
          let transactionId;
          let leaseId = null;
          
          if (typeof msg === 'string') {
            transactionId = msg;
          } else if (typeof msg === 'object' && msg !== null) {
            transactionId = msg.transactionId || msg.id;
            leaseId = msg.leaseId || null;
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
          
          const ack = {
            transactionId,
            status: statusStr,
            error: msg._error || context.error || null
          };
          
          // Include lease ID if available
          if (leaseId) {
            ack.leaseId = leaseId;
          }
          
          return ack;
        });
      } else {
        // Option A: Same status for all messages
        const statusStr = typeof status === 'boolean' 
          ? (status ? 'completed' : 'failed')
          : status;
        
        acknowledgments = message.map(msg => {
          // Extract transaction ID and lease ID
          let transactionId;
          let leaseId = null;
          
          if (typeof msg === 'string') {
            transactionId = msg;
          } else if (typeof msg === 'object' && msg !== null) {
            transactionId = msg.transactionId || msg.id;
            leaseId = msg.leaseId || null;
            if (!transactionId) {
              throw new Error('Message object must have transactionId or id property');
            }
          } else {
            throw new Error('Invalid message in batch');
          }
          
          const ack = {
            transactionId,
            status: statusStr,
            error: context.error || null
          };
          
          // Include lease ID if available
          if (leaseId) {
            ack.leaseId = leaseId;
          }
          
          return ack;
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
    // Extract transaction ID and lease ID
    let transactionId;
    let leaseId = null;
    
    if (typeof message === 'string') {
      transactionId = message;
    } else if (typeof message === 'object' && message !== null) {
      transactionId = message.transactionId || message.id;
      leaseId = message.leaseId || null;  // Extract lease ID if present
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
    
    // Include lease ID if available for lease validation
    if (leaseId) {
      body.leaseId = leaseId;
    }
    
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
  
  /**
   * Create a transaction builder for atomic operations
   * @returns {TransactionBuilder} Transaction builder instance
   */
  transaction() {
    return new TransactionBuilder(this);
  }
  
  /**
   * Create a pipeline for message processing workflows
   * @param {string} queue - Source queue name
   * @returns {PipelineBuilder} Pipeline builder instance
   */
  pipeline(queue) {
    return new PipelineBuilder(this, queue);
  }
  
  // Internal helper methods for Transaction and Pipeline
  async _ensureConnected() {
    return this.#ensureConnected();
  }
  
  get _http() {
    return this.#http;
  }
}

/**
 * Transaction Builder for atomic operations
 */
class TransactionBuilder {
  #client;
  #operations = [];
  #requiredLeases = [];
  
  constructor(client) {
    this.#client = client;
  }
  
  /**
   * Add ACK operation to transaction
   * @param {Array|Object} messages - Messages to acknowledge
   * @param {string} status - Status ('completed' or 'failed')
   * @returns {TransactionBuilder} this for chaining
   */
  ack(messages, status = 'completed') {
    const msgs = Array.isArray(messages) ? messages : [messages];
    
    msgs.forEach(msg => {
      const transactionId = msg.transactionId || msg.id || msg;
      const leaseId = msg.leaseId || null;
      
      this.#operations.push({
        type: 'ack',
        transactionId,
        status
      });
      
      if (leaseId) {
        this.#requiredLeases.push(leaseId);
      }
    });
    
    return this;
  }
  
  /**
   * Add PUSH operation to transaction
   * @param {string} queue - Target queue
   * @param {Array|Object} items - Items to push
   * @returns {TransactionBuilder} this for chaining
   */
  push(queue, items) {
    const itemArray = Array.isArray(items) ? items : [items];
    
    this.#operations.push({
      type: 'push',
      items: itemArray.map(item => ({
        queue,
        payload: item
      }))
    });
    
    return this;
  }
  
  /**
   * Add lease extension to transaction
   * @param {string} leaseId - Lease ID to extend
   * @returns {TransactionBuilder} this for chaining
   */
  extend(leaseId) {
    this.#operations.push({
      type: 'extend',
      leaseId
    });
    
    this.#requiredLeases.push(leaseId);
    return this;
  }
  
  /**
   * Execute the transaction
   * @returns {Promise<Object>} Transaction result
   */
  async commit() {
    await this.#client._ensureConnected();
    
    const result = await this.#client._http.post('/api/v1/transaction', {
      operations: this.#operations,
      requiredLeases: [...new Set(this.#requiredLeases)] // Unique leases
    });
    
    if (!result.success) {
      throw new Error(result.error || 'Transaction failed');
    }
    
    return result;
  }
}

/**
 * Pipeline Builder for message processing workflows
 */
class PipelineBuilder {
  #client;
  #queue;
  #options = {};
  #processor = null;
  #errorHandler = null;
  #atomicOps = null;
  #leaseRenewal = null;
  #repeatConfig = null;
  #concurrency = 1;
  
  constructor(client, queue) {
    this.#client = client;
    this.#queue = queue;
  }
  
  /**
   * Take messages from the queue
   * @param {number} count - Number of messages to take
   * @param {Object} options - Take options
   * @returns {PipelineBuilder} this for chaining
   */
  take(count, options = {}) {
    this.#options = { ...options, batch: count, limit: count };
    return this;
  }
  
  /**
   * Process messages individually (one at a time)
   * @param {Function} handler - Async function to process a single message
   * @returns {PipelineBuilder} this for chaining
   */
  process(handler) {
    // Wrap the single-message handler to process messages one by one
    this.#processor = async (messages) => {
      const results = [];
      for (const message of messages) {
        const result = await handler(message);
        results.push(result);
      }
      return results;
    };
    return this;
  }
  
  /**
   * Process messages as a batch
   * @param {Function} handler - Async function to process a batch of messages
   * @returns {PipelineBuilder} this for chaining
   */
  processBatch(handler) {
    this.#processor = handler;
    return this;
  }
  
  /**
   * Define atomic operations to execute after processing
   * @param {Function} txBuilder - Function that receives a TransactionBuilder
   * @returns {PipelineBuilder} this for chaining
   */
  atomically(txBuilder) {
    this.#atomicOps = txBuilder;
    return this;
  }
  
  /**
   * Handle errors
   * @param {Function} handler - Error handler function
   * @returns {PipelineBuilder} this for chaining
   */
  onError(handler) {
    this.#errorHandler = handler;
    return this;
  }
  
  /**
   * Enable automatic lease renewal
   * @param {Object} options - Renewal options
   * @returns {PipelineBuilder} this for chaining
   */
  withAutoRenewal(options = {}) {
    this.#leaseRenewal = {
      interval: options.interval || 30000, // Renew every 30s by default
      enabled: true
    };
    return this;
  }
  
  /**
   * Set concurrency level for parallel batch processing
   * @param {number} level - Number of concurrent batches to process
   * @returns {PipelineBuilder} this for chaining
   */
  withConcurrency(level) {
    this.#concurrency = Math.max(1, level);
    return this;
  }
  
  /**
   * Configure repeated execution
   * @param {Object} options - Repeat options
   * @returns {PipelineBuilder} this for chaining
   */
  repeat(options = {}) {
    this.#repeatConfig = {
      maxIterations: options.maxIterations || Infinity,
      delay: options.delay || 0,
      continuous: options.continuous !== undefined ? options.continuous : true,  // Default to continuous mode
      waitOnEmpty: options.waitOnEmpty || 1000  // Wait time when no messages
    };
    return this;
  }
  
  /**
   * Execute the pipeline (once or repeatedly based on configuration)
   * @param {Object} options - Execution options
   * @param {boolean} options.returnGenerator - For repeat mode, return generator instead of running loop
   * @returns {Promise<Object>} Result or summary of all iterations
   */
  async execute(options = {}) {
    // If repeat is configured
    if (this.#repeatConfig) {
      // Option to return generator for advanced users
      if (options.returnGenerator) {
        return this.#executeRepeatedly();
      }
      
      // Default: run the loop internally and return summary
      return this.#executeRepeatedlyWithSummary();
    }
    
    // Otherwise execute once
    return this.#executeSingle();
  }
  
  /**
   * Execute the pipeline once (internal)
   * @private
   * @returns {Promise<Object>} Pipeline execution result
   */
  async #executeSingle() {
    // Use local variables to avoid race conditions in parallel execution
    let messages = null;
    let processedMessages = null;
    
    try {
      // 1. Take messages
      messages = await this.#client.takeSingleBatch(this.#queue, this.#options);
      
      if (!messages || messages.length === 0) {
        return { processed: 0, messages: [] };
      }
      
      // 2. Set up auto-renewal if enabled
      let renewalTimer = null;
      if (this.#leaseRenewal?.enabled) {
        renewalTimer = this.#setupAutoRenewal(messages);
      }
      
      try {
        // 3. Process messages
        if (this.#processor) {
          processedMessages = await this.#processor(messages);
        } else {
          processedMessages = messages;
        }
        
        // 4. Execute atomic operations
        if (this.#atomicOps) {
          const tx = new TransactionBuilder(this.#client);
          
          // Apply the atomic operations
          this.#atomicOps(tx, messages, processedMessages);
          
          await tx.commit();
        } else {
          // Default: just ACK the messages
          await this.#client.ack(messages, true);
        }
        
        return {
          processed: messages.length,
          messages: processedMessages
        };
        
      } finally {
        // Clean up renewal timer
        if (renewalTimer) {
          clearInterval(renewalTimer);
        }
      }
      
    } catch (error) {
      if (this.#errorHandler) {
        await this.#errorHandler(error, messages || []);
        return { processed: 0, messages: [], error: error.message };
      } else {
        throw error;
      }
    }
  }
  
  /**
   * Execute the pipeline repeatedly (internal)
   * @private
   * @returns {AsyncGenerator} Async generator of results
   */
  async *#executeRepeatedly() {
    const { maxIterations, delay } = this.#repeatConfig;
    let iteration = 0;
    
    while (iteration < maxIterations) {
      const result = await this.#executeSingle();
      yield result;
      
      iteration++;
      
      if (delay > 0) {
        await new Promise(resolve => setTimeout(resolve, delay));
      }
    }
  }
  
  /**
   * Execute the pipeline repeatedly and return summary
   * @private
   * @returns {Promise<Object>} Summary of all iterations
   */
  async #executeRepeatedlyWithSummary() {
    const { maxIterations, delay, continuous, waitOnEmpty } = this.#repeatConfig;
    
    // If concurrency > 1, use parallel workers
    if (this.#concurrency > 1) {
      return this.#executeRepeatedlyParallel();
    }
    
    // Sequential processing
    let iteration = 0;
    let totalProcessed = 0;
    const results = [];
    let consecutiveEmpty = 0;
    const maxConsecutiveEmpty = continuous ? Infinity : 3; // In continuous mode, never stop on empty
    
    while (iteration < maxIterations) {
      const result = await this.#executeSingle();
      
      if (result.processed === 0) {
        consecutiveEmpty++;
        
        // In continuous mode, wait and retry
        if (continuous || consecutiveEmpty < maxConsecutiveEmpty) {
          await new Promise(resolve => setTimeout(resolve, waitOnEmpty));
          continue; // Don't increment iteration for empty results
        } else {
          // Not continuous and too many empty results
          break;
        }
      } else {
        // Reset empty counter on successful batch
        consecutiveEmpty = 0;
        results.push(result);
        totalProcessed += result.processed;
        iteration++;
      }
      
      if (delay > 0 && iteration < maxIterations) {
        await new Promise(resolve => setTimeout(resolve, delay));
      }
    }
    
    return {
      iterations: iteration,
      totalProcessed,
      results,
      summary: {
        averagePerBatch: iteration > 0 ? totalProcessed / iteration : 0,
        completed: iteration >= maxIterations ? 'maxIterations' : 'noMessages',
        concurrency: this.#concurrency,
        continuous
      }
    };
  }
  
  /**
   * Execute pipeline with parallel processing
   * Each worker independently calls take(), allowing them to work on different partitions
   * @private
   * @returns {Promise<Object>} Summary of all parallel executions
   */
  async #executeRepeatedlyParallel() {
    const { maxIterations = Infinity, delay, continuous, waitOnEmpty } = this.#repeatConfig || {};
    const workers = [];
    const results = [];
    
    // Shared state for coordination
    const sharedState = {
      shouldStop: false,
      emptyWorkers: new Set(),
      lock: Promise.resolve()
    };
    
    // Worker function - each worker independently takes and processes messages
    const worker = async (workerId) => {
      let workerIterations = 0;
      let workerProcessed = 0;
      let consecutiveEmpty = 0;
      const maxConsecutiveEmpty = continuous ? Infinity : 10;
      
      while (!sharedState.shouldStop && workerIterations < maxIterations) {
        try {
          // Each worker independently calls executeSingle (which calls take)
          const result = await this.#executeSingle();
          
          if (result.processed === 0) {
            consecutiveEmpty++;
            
            // Add this worker to empty set
            sharedState.emptyWorkers.add(workerId);
            
            // If all workers are empty, consider stopping
            if (!continuous && sharedState.emptyWorkers.size === this.#concurrency) {
              // Double-check with a small delay to avoid race conditions
              await new Promise(resolve => setTimeout(resolve, 200));
              
              // Re-check after delay
              if (sharedState.emptyWorkers.size === this.#concurrency) {
                sharedState.shouldStop = true;
                break;
              }
            }
            
            // Individual worker timeout
            if (!continuous && consecutiveEmpty >= maxConsecutiveEmpty) {
              break;
            }
            
            // Wait before retry
            await new Promise(resolve => setTimeout(resolve, waitOnEmpty || 1000));
            continue; // Don't count empty iterations
          }
          
          // Reset empty counter on successful batch
          consecutiveEmpty = 0;
          sharedState.emptyWorkers.delete(workerId);
          
          // Store result with worker info
          results.push({ 
            ...result, 
            workerId, 
            timestamp: new Date().toISOString() 
          });
          
          workerProcessed += result.processed;
          workerIterations++;
          
          if (delay > 0) {
            await new Promise(resolve => setTimeout(resolve, delay));
          }
        } catch (error) {
          console.error(`Worker ${workerId} error:`, error);
          if (this.#errorHandler) {
            await this.#errorHandler(error, []);
          } else {
            sharedState.shouldStop = true;
            throw error;
          }
        }
      }
      
      return { 
        workerId, 
        iterations: workerIterations, 
        processed: workerProcessed,
        consecutiveEmpty 
      };
    };
    
    // Start parallel workers
    console.log(`🚀 Starting ${this.#concurrency} parallel workers...`);
    for (let i = 0; i < this.#concurrency; i++) {
      workers.push(worker(i));
    }
    
    // Wait for all workers
    const workerResults = await Promise.all(workers);
    
    // Aggregate results
    let totalProcessed = 0;
    let totalIterations = 0;
    
    workerResults.forEach(wr => {
      totalProcessed += wr.processed;
      totalIterations += wr.iterations;
      if (wr.iterations > 0) {
        console.log(`Worker ${wr.workerId}: Processed ${wr.processed} messages in ${wr.iterations} batches`);
      }
    });
    
    return {
      iterations: totalIterations,
      totalProcessed,
      results,
      workers: workerResults,
      summary: {
        averagePerBatch: totalIterations > 0 ? totalProcessed / totalIterations : 0,
        completed: totalIterations >= (maxIterations * this.#concurrency) ? 'maxIterations' : 'noMessages',
        concurrency: this.#concurrency,
        workersUtilized: workerResults.filter(w => w.iterations > 0).length,
        continuous
      }
    };
  }
  
  /**
   * Set up automatic lease renewal
   * @private
   */
  #setupAutoRenewal(messages) {
    const leaseIds = messages
      .map(m => m.leaseId)
      .filter(id => id != null);
    
    if (leaseIds.length === 0) return null;
    
    return setInterval(async () => {
      try {
        // Extend all leases using the client's renewLease method
        for (const leaseId of leaseIds) {
          const result = await this.#client.renewLease(leaseId);
          if (!result.success) {
            console.error('Lease renewal failed:', result.error);
          }
        }
      } catch (error) {
        console.error('Failed to renew lease:', error);
      }
    }, this.#leaseRenewal.interval);
  }
}

// Export the class as default as well for convenience
export default Queen;