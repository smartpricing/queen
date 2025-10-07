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
  
  // Configure a queue
  const configure = async ({ ns, task, queue, options = {} }) => {
    return withRetry(
      () => http.post('/api/v1/configure', { ns, task, queue, options }),
      retryAttempts,
      retryDelay
    );
  };
  
  // Push messages to queue
  const push = async ({ items, config = {} }) => {
    if (!Array.isArray(items)) {
      items = [items];
    }
    
    return withRetry(
      () => http.post('/api/v1/push', { items, config }),
      retryAttempts,
      retryDelay
    );
  };
  
  // Pop messages from queue
  const pop = async ({ ns, task, queue, wait = false, timeout = 30000, batch = 1 }) => {
    const path = queue 
      ? `/api/v1/pop/ns/${ns}/task/${task}/queue/${queue}`
      : task
      ? `/api/v1/pop/ns/${ns}/task/${task}`
      : `/api/v1/pop/ns/${ns}`;
    
    const params = new URLSearchParams({
      wait: wait.toString(),
      timeout: timeout.toString(),
      batch: batch.toString()
    });
    
    const result = await http.get(`${path}?${params}`);
    
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
  
  // Get analytics
  const analytics = {
    queues: async () => http.get('/api/v1/analytics/queues'),
    namespace: async (ns) => http.get(`/api/v1/analytics/ns/${ns}`),
    task: async (ns, task) => http.get(`/api/v1/analytics/ns/${ns}/task/${task}`),
    queueDepths: async () => http.get('/api/v1/analytics/queue-depths'),
    throughput: async () => http.get('/api/v1/analytics/throughput')
  };
  
  // Consumer helper - continuously pop and process messages
  const consume = ({ ns, task, queue, handler, options = {} }) => {
    const {
      batch = 1,
      wait = true,
      timeout = 30000,
      stopOnError = false
    } = options;
    
    let running = true;
    
    const stop = () => {
      running = false;
    };
    
    // Start the consume loop asynchronously
    (async () => {
      while (running) {
        try {
          const result = await pop({ ns, task, queue, wait, timeout, batch });
          
          if (result.messages && result.messages.length > 0) {
            for (const message of result.messages) {
              if (!running) break; // Check if stopped
              
              try {
                await handler(message);
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
        } catch (error) {
          console.error('Error in consume loop:', error);
          if (stopOnError) {
            running = false;
          } else {
            // Wait before retrying
            await new Promise(resolve => setTimeout(resolve, retryDelay));
          }
        }
      }
    })();
    
    // Return the stop function immediately
    return stop;
  };
  
  return {
    configure,
    push,
    pop,
    ack,
    ackBatch,
    analytics,
    consume
  };
};
