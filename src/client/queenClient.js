import { httpRequest, createHttpClient } from './utils/http.js';
import { withRetry } from './utils/retry.js';

export const createQueenClient = (options = {}) => {
  const {
    baseUrl = 'http://localhost:6632',
    timeout = 30000,
    retryAttempts = 3,
    retryDelay = 1000
  } = options;
  
  const http = createHttpClient({ baseUrl, timeout });
  
  // Configure a queue partition
  const configure = async ({ queue, partition, namespace, task, options = {} }) => {
    return withRetry(
      () => http.post('/api/v1/configure', { queue, partition, namespace, task, options }),
      retryAttempts,
      retryDelay
    );
  };
  
  // Push messages to queue
  const push = async ({ items, config = {} }) => {
    if (!Array.isArray(items)) {
      items = [items];
    }
    
    // Ensure each item has the correct structure for V2
    const v2Items = items.map(item => ({
      queue: item.queue,
      partition: item.partition ?? 'Default',
      payload: item.payload || item.data,
      transactionId: item.transactionId
    }));
    
    return withRetry(
      () => http.post('/api/v1/push', { items: v2Items, config }),
      retryAttempts,
      retryDelay
    );
  };
  
  // Pop messages from queue
  const pop = async (options = {}) => {
    const { 
      queue, 
      partition, 
      namespace,
      task,
      wait = false, 
      timeout = 30000, 
      batch = 1 
    } = options;
    
    let path;
    const params = new URLSearchParams({
      wait: wait.toString(),
      timeout: timeout.toString(),
      batch: batch.toString()
    });
    
    // Determine the appropriate endpoint based on parameters
    if (queue && partition) {
      // Pop from specific partition
      path = `/api/v1/pop/queue/${queue}/partition/${partition}`;
    } else if (queue) {
      // Pop from any partition in queue
      path = `/api/v1/pop/queue/${queue}`;
    } else if (namespace || task) {
      // Pop with filters
      path = '/api/v1/pop';
      if (namespace) params.append('namespace', namespace);
      if (task) params.append('task', task);
    } else {
      throw new Error('Must specify either queue, namespace, or task for pop operation');
    }
    
    // For long polling, use a slightly longer client timeout than server timeout
    const clientTimeout = wait ? timeout + 5000 : timeout;
    
    const result = await http.get(`${path}?${params}`, clientTimeout);
    
    // Return empty array if no messages (204 response)
    if (!result) {
      return { messages: [] };
    }
    
    return result;
  };
  
  // Acknowledge a message
  const ack = async (transactionId, status = 'completed', error = null) => {
    return withRetry(
      () => http.post('/api/v1/ack', { transactionId, status, error }),
      retryAttempts,
      retryDelay
    );
  };
  
  // Batch acknowledge messages
  const ackBatch = async (acknowledgments) => {
    return withRetry(
      () => http.post('/api/v1/ack/batch', { acknowledgments }),
      retryAttempts,
      retryDelay
    );
  };
  
  // Message management
  const messages = {
    // List messages with filters
    list: async (filters = {}) => {
      const params = new URLSearchParams();
      if (filters.queue) params.append('queue', filters.queue);
      if (filters.partition) params.append('partition', filters.partition);
      if (filters.namespace) params.append('namespace', filters.namespace);
      if (filters.task) params.append('task', filters.task);
      if (filters.status) params.append('status', filters.status);
      if (filters.limit) params.append('limit', filters.limit);
      if (filters.offset) params.append('offset', filters.offset);
      
      return http.get(`/api/v1/messages?${params}`);
    },
    
    // Get single message
    get: async (transactionId) => {
      return http.get(`/api/v1/messages/${transactionId}`);
    },
    
    // Delete message
    delete: async (transactionId) => {
      return http.delete(`/api/v1/messages/${transactionId}`);
    },
    
    // Retry failed message
    retry: async (transactionId) => {
      return http.post(`/api/v1/messages/${transactionId}/retry`);
    },
    
    // Move to DLQ
    moveToDLQ: async (transactionId) => {
      return http.post(`/api/v1/messages/${transactionId}/dlq`);
    },
    
    // Get related messages
    getRelated: async (transactionId) => {
      return http.get(`/api/v1/messages/${transactionId}/related`);
    }
  };
  
  // Queue management
  const queues = {
    // Clear queue or partition
    clear: async (queue, partition = null) => {
      const params = partition ? `?partition=${partition}` : '';
      return http.delete(`/api/v1/queues/${queue}/clear${params}`);
    }
  };
  
  // Analytics
  const analytics = {
    // Get all queues overview
    queues: async (filters = {}) => {
      const params = new URLSearchParams();
      if (filters.namespace) params.append('namespace', filters.namespace);
      if (filters.task) params.append('task', filters.task);
      const query = params.toString();
      return http.get(`/api/v1/analytics/queues${query ? '?' + query : ''}`);
    },
    
    // Get specific queue statistics
    queue: async (queue) => {
      return http.get(`/api/v1/analytics/queue/${queue}`);
    },
    
    // Get stats by namespace
    namespace: async (namespace) => {
      return http.get(`/api/v1/analytics?namespace=${namespace}`);
    },
    
    // Get stats by task
    task: async (task) => {
      return http.get(`/api/v1/analytics?task=${task}`);
    },
    
    // Get queue depths
    queueDepths: async (filters = {}) => {
      const params = new URLSearchParams();
      if (filters.namespace) params.append('namespace', filters.namespace);
      if (filters.task) params.append('task', filters.task);
      const query = params.toString();
      return http.get(`/api/v1/analytics/queue-depths${query ? '?' + query : ''}`);
    },
    
    // Get throughput metrics
    throughput: async () => {
      return http.get('/api/v1/analytics/throughput');
    },
    
    // Get queue stats with filters
    queueStats: async (filters = {}) => {
      const params = new URLSearchParams();
      if (filters.queue) params.append('queue', filters.queue);
      if (filters.namespace) params.append('namespace', filters.namespace);
      if (filters.task) params.append('task', filters.task);
      return http.get(`/api/v1/analytics/queue-stats?${params}`);
    },

    // Get queue lag metrics
    queueLag: async (filters = {}) => {
      const params = new URLSearchParams();
      if (filters.queue) params.append('queue', filters.queue);
      if (filters.namespace) params.append('namespace', filters.namespace);
      if (filters.task) params.append('task', filters.task);
      const query = params.toString();
      return http.get(`/api/v1/analytics/queue-lag${query ? '?' + query : ''}`);
    }
  };
  
  // System health
  const health = {
    check: async () => http.get('/health'),
    metrics: async () => http.get('/metrics')
  };
  
  // Consumer helper - continuously pop and process messages
  const consume = ({ queue, partition, namespace, task, handler, handlerBatch, options = {} }) => {
    const {
      batch = 1,
      wait = true,
      timeout = 30000,
      stopOnError = false
    } = options;
    
    // Validate that only one handler type is provided
    if (handler && handlerBatch) {
      throw new Error('Cannot specify both handler and handlerBatch. Choose one processing mode.');
    }
    
    if (!handler && !handlerBatch) {
      throw new Error('Must specify either handler (for individual processing) or handlerBatch (for batch processing).');
    }
    
    const isBatchMode = !!handlerBatch;
    const messageHandler = handler || handlerBatch;
    
    let running = true;
    
    const stop = () => {
      running = false;
    };
    
    // Start the consume loop asynchronously
    (async () => {
      while (running) {
        try {
          const result = await pop({ 
            queue, 
            partition, 
            namespace, 
            task, 
            wait, 
            timeout, 
            batch 
          });
          
          if (result.messages && result.messages.length > 0) {
            if (isBatchMode) {
              // Batch processing mode
              if (!running) return;
              
              try {
                await messageHandler(result.messages);
                
                // Batch acknowledge all messages as completed
                const acknowledgments = result.messages.map(msg => ({
                  transactionId: msg.transactionId,
                  status: 'completed'
                }));
                
                await ackBatch(acknowledgments);
              } catch (error) {
                console.error('Error processing message batch:', error);
                
                // Batch acknowledge all messages as failed
                const acknowledgments = result.messages.map(msg => ({
                  transactionId: msg.transactionId,
                  status: 'failed',
                  error: error.message
                }));
                
                await ackBatch(acknowledgments);
                
                if (stopOnError) {
                  running = false;
                }
              }
            } else {
              // Individual processing mode (original behavior)
              for (const message of result.messages) {
                if (!running) break; // Check if stopped
                
                try {
                  await messageHandler(message);
                  await ack(message.transactionId, 'completed');
                } catch (error) {
                  console.error('Error processing message:', error);
                  await ack(message.transactionId, 'failed', error.message);
                  
                  if (stopOnError) {
                    running = false;
                    break;
                  }
                }
              }
            }
          }
          // If no messages received (timeout on server side), immediately continue
          // No delay needed - this is expected behavior for long polling
        } catch (error) {
          // Check if this is a timeout/abort error from the HTTP client
          const isTimeoutError = error.name === 'AbortError' || 
                                error.message?.includes('abort') ||
                                error.message?.includes('timeout');
          
          if (isTimeoutError) {
            // For timeout errors, immediately retry without delay
            console.debug('Long poll timeout, immediately retrying...');
            continue;
          }
          
          console.error('Error in consume loop:', error);
          if (stopOnError) {
            running = false;
          } else {
            // For other errors (network issues, server errors), wait before retrying
            await new Promise(resolve => setTimeout(resolve, retryDelay));
          }
        }
      }
    })();
    
    // Return the stop function immediately
    return stop;
  };
  
  // WebSocket support for real-time updates
  const createWebSocketConnection = (path = '/ws/dashboard') => {
    const wsUrl = baseUrl.replace('http://', 'ws://').replace('https://', 'wss://');
    const ws = new WebSocket(`${wsUrl}${path}`);
    
    return {
      ws,
      onMessage: (handler) => {
        ws.addEventListener('message', (event) => {
          try {
            const data = JSON.parse(event.data);
            handler(data);
          } catch (error) {
            console.error('Error parsing WebSocket message:', error);
          }
        });
      },
      onError: (handler) => ws.addEventListener('error', handler),
      onClose: (handler) => ws.addEventListener('close', handler),
      onOpen: (handler) => ws.addEventListener('open', handler),
      close: () => ws.close()
    };
  };
  
  return {
    configure,
    push,
    pop,
    ack,
    ackBatch,
    messages,
    queues,
    analytics,
    health,
    consume,
    createWebSocketConnection
  };
};

// Export helper for creating a simple consumer
export const createConsumer = (options) => {
  const client = createQueenClient(options);
  return client.consume;
};

// Export helper for creating a producer
export const createProducer = (options) => {
  const client = createQueenClient(options);
  return {
    push: client.push,
    configure: client.configure
  };
};
