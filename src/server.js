import uWS from 'uWebSockets.js';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import config from './config.js';
import { createPool, initDatabase } from './database/connection.js';
import { createOptimizedQueueManager } from './managers/queueManagerOptimized.js';
import { createResourceCache } from './managers/resourceCache.js';
import { createEventManager } from './managers/eventManager.js';
import { SystemEventManager, SYSTEM_QUEUE } from './managers/systemEventManager.js';
import { syncSystemEvents } from './services/startupSync.js';
import { createQueenClient } from './client/queenClient.js';
import { createPushRoute } from './routes/push.js';
import { createPopRoute } from './routes/pop.js';
import { createAckRoute } from './routes/ack.js';
import { createConfigureRoute } from './routes/configure.js';
import { createAnalyticsRoutes } from './routes/analytics.js';
import { createMessagesRoutes } from './routes/messages.js';
import { createResourcesRoutes } from './routes/resources.js';
import { createWebSocketServer } from './websocket/wsServer.js';
import { initEncryption } from './services/encryptionService.js';
import { startRetentionJob } from './services/retentionService.js';
import { startEvictionJob } from './services/evictionService.js';
import { log } from './utils/logger.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const PORT = config.SERVER.PORT;
const HOST = config.SERVER.HOST;

// Use WORKER_ID from config as the server instance ID for consumer groups
// This ensures the same server (identified by WORKER_ID) maintains its position in the event stream
const SERVER_INSTANCE_ID = config.SERVER.WORKER_ID;

// Performance monitoring
let requestCount = 0;
let messageCount = 0;
const startTime = Date.now();

// Initialize components
const pool = createPool();
const resourceCache = createResourceCache();
const eventManager = createEventManager();

// Initialize system event manager
const systemEventManager = new SystemEventManager(pool, SERVER_INSTANCE_ID);

// Register cache event handlers
resourceCache.registerEventHandlers(systemEventManager);

// Always use optimized queue manager - it's better for all scenarios
const queueManager = createOptimizedQueueManager(pool, resourceCache, eventManager, systemEventManager);
const analyticsRoutes = createAnalyticsRoutes(queueManager);
const messagesRoutes = createMessagesRoutes(pool, queueManager);
const resourcesRoutes = createResourcesRoutes(pool, systemEventManager);

// Initialize database with migrations
const initDatabaseWithMigrations = async () => {
  // Run base schema
  await initDatabase(pool);
  
  // Run extension migrations
  const migrationPath = path.join(__dirname, '..', 'migrations', '001-extend-features.sql');
  if (fs.existsSync(migrationPath)) {
    const migration = fs.readFileSync(migrationPath, 'utf8');
    try {
      await pool.query(migration);
      log('✅ Extension features migration applied');
    } catch (error) {
      if (!error.message.includes('already exists')) {
        log('Migration error:', error);
      }
    }
  }
};

await initDatabaseWithMigrations();

// Initialize system queue
async function initializeSystemQueue() {
  try {
    // Direct database operation to create system queue
    await pool.query(`
      INSERT INTO queen.queues (name, ttl, priority, max_queue_size)
      VALUES ($1, 300, 100, 10000)
      ON CONFLICT (name) DO UPDATE SET
        ttl = EXCLUDED.ttl,
        priority = EXCLUDED.priority,
        max_queue_size = EXCLUDED.max_queue_size
    `, [SYSTEM_QUEUE]);
    
    await pool.query(`
      INSERT INTO queen.partitions (queue_id, name)
      SELECT id, 'Default' FROM queen.queues WHERE name = $1
      ON CONFLICT (queue_id, name) DO NOTHING
    `, [SYSTEM_QUEUE]);
    
    log('✅ System event queue initialized');
  } catch (error) {
    console.error('Failed to initialize system queue:', error);
    throw error;
  }
}

await initializeSystemQueue();

// Initialize extended features
log('🚀 Initializing Queen Services...');

// 1. Encryption service
const encryptionEnabled = initEncryption();

// 2. Retention service
const stopRetention = startRetentionJob(pool);

// 3. Eviction service
const stopEviction = startEvictionJob(pool, eventManager);

// Background job for lease reclamation
setInterval(async () => {
  try {
    const reclaimed = await queueManager.reclaimExpiredLeases();
    if (reclaimed > 0) {
      log(`Reclaimed ${reclaimed} expired leases`);
    }
  } catch (error) {
    log('Error reclaiming leases:', error);
  }
}, config.JOBS.LEASE_RECLAIM_INTERVAL);

// Create server
const app = uWS.App();

// Initialize WebSocket server
const wsServer = createWebSocketServer(app, eventManager);

// Background job for queue depth updates
setInterval(async () => {
  await wsServer.updateQueueDepths(queueManager);
}, config.JOBS.QUEUE_DEPTH_UPDATE_INTERVAL);

// Background job for system stats
setInterval(async () => {
  await wsServer.sendSystemStats(pool);
}, config.JOBS.SYSTEM_STATS_UPDATE_INTERVAL);

// CORS headers helper
const setCorsHeaders = (res) => {
  res.writeHeader('Access-Control-Allow-Origin', config.API.CORS_ALLOWED_ORIGINS);
  res.writeHeader('Access-Control-Allow-Methods', config.API.CORS_ALLOWED_METHODS);
  res.writeHeader('Access-Control-Allow-Headers', config.API.CORS_ALLOWED_HEADERS);
  res.writeHeader('Access-Control-Max-Age', config.API.CORS_MAX_AGE.toString());
};

// Helper to read JSON body
const readJson = (res, cb, abortedRef) => {
  let buffer;
  const maxBodySize = config.API.MAX_BODY_SIZE; // 10MB max body size
  
  res.onData((ab, isLast) => {
    const chunk = Buffer.from(ab);
    
    // Check size limit
    const currentSize = (buffer ? buffer.length : 0) + chunk.length;
    if (currentSize > maxBodySize) {
      if (abortedRef && abortedRef.aborted) return;
      res.writeStatus(config.HTTP_STATUS.BAD_REQUEST.toString()).end(JSON.stringify({ error: 'Request body too large' }));
      return;
    }
    
    if (isLast) {
      let json;
      try {
        let fullData;
        if (buffer) {
          fullData = Buffer.concat([buffer, chunk]);
        } else {
          fullData = chunk;
        }
        
        const jsonString = fullData.toString('utf8');
        
        // Debug large payloads
        if (jsonString.length > 1000000) {
          log(`Parsing large JSON: ${jsonString.length} bytes, starts with: ${jsonString.substring(0, 100)}`);
        }
        
        json = JSON.parse(jsonString);
      } catch (e) {
        if (abortedRef && abortedRef.aborted) return;
        // Log the actual error for debugging  
        log(`JSON parse error: ${e.message}`);
        
        // Log what we're trying to parse for debugging
        let sample = '';
        if (buffer) {
          const fullData = Buffer.concat([buffer, chunk]);
          sample = fullData.toString('utf8').substring(0, 200);
        } else {
          sample = chunk.toString('utf8').substring(0, 200);
        }
        log(`Failed to parse data starting with: ${sample}`);
        
        res.writeStatus(config.HTTP_STATUS.BAD_REQUEST.toString()).end(JSON.stringify({ error: 'Invalid JSON' }));
        return;
      }
      cb(json);
    } else {
      if (buffer) {
        buffer = Buffer.concat([buffer, chunk]);
      } else {
        buffer = Buffer.from(chunk);  // Ensure it's a Buffer
      }
    }
  });
};

// Handle OPTIONS requests for CORS preflight
app.options('/*', (res, req) => {
  res.onAborted(() => {
    log('OPTIONS request aborted');
  });
  
  res.cork(() => {
    setCorsHeaders(res);
    res.writeStatus(config.HTTP_STATUS.NO_CONTENT.toString()).end();
  });
});

// Configure route
app.post('/api/v1/configure', (res, req) => {
  const abortedRef = { aborted: false };
  res.onAborted(() => {
    abortedRef.aborted = true;
    log('Configure request aborted');
  });
  
  readJson(res, async (body) => {
    if (abortedRef.aborted) return;
    try {
      const result = await createConfigureRoute(queueManager)(body);
      if (abortedRef.aborted) return;
      eventManager.emit('queue.created', result);
      res.cork(() => {
        setCorsHeaders(res);
        res.writeStatus(config.HTTP_STATUS.CREATED.toString()).end(JSON.stringify(result));
      });
    } catch (error) {
      if (abortedRef.aborted) return;
      log('Configure error:', error);
      res.cork(() => {
        setCorsHeaders(res);
        res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
      });
    }
  }, abortedRef);
});

// Push route
app.post('/api/v1/push', (res, req) => {
  const abortedRef = { aborted: false };
  res.onAborted(() => {
    abortedRef.aborted = true;
    log('Push request aborted');
  });
  
  readJson(res, async (body) => {
    if (abortedRef.aborted) return;
    try {
      const result = await createPushRoute(queueManager)(body);
      messageCount += result.messages.length;
      requestCount++;
      if (abortedRef.aborted) return;
      
      // Emit events for pushed messages
      for (const msg of result.messages) {
        if (msg.status === 'queued') {
          const item = body.items.find(i => 
            i.transactionId === msg.transactionId || !i.transactionId
          );
          if (item) {
            const partition = item.partition ?? 'Default';
            const queuePath = `${item.queue}/${partition}`;
            eventManager.emit('message.pushed', {
              queue: item.queue,
              partition: partition,
              transactionId: msg.transactionId
            });
            eventManager.notifyMessageAvailable(queuePath);
          }
        }
      }
      
      res.cork(() => {
        setCorsHeaders(res);
        res.writeStatus(config.HTTP_STATUS.CREATED.toString()).end(JSON.stringify(result));
      });
    } catch (error) {
      if (abortedRef.aborted) return;
      log('Push error:', error.message || error);
      res.cork(() => {
        setCorsHeaders(res);
        res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
      });
    }
  }, abortedRef);
});

// Pop routes with new structure
app.get('/api/v1/pop/queue/:queue/partition/:partition', (res, req) => {
  const queue = req.getParameter(0);
  const partition = req.getParameter(1);
  const query = new URLSearchParams(req.getQuery());
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Pop request aborted');
  });
  
  createPopRoute(queueManager, eventManager)(
    { 
      queue, 
      partition,
      consumerGroup: query.get('consumerGroup') || query.get('consumer_group')
    },
    {
      wait: query.get('wait') === 'true',
      timeout: parseInt(query.get('timeout') || config.QUEUE.DEFAULT_TIMEOUT.toString()),
      batch: parseInt(query.get('batch') || config.QUEUE.DEFAULT_BATCH_SIZE.toString()),
      subscriptionMode: query.get('subscriptionMode'),
      subscriptionFrom: query.get('subscriptionFrom')
    }
  ).then(result => {
    if (aborted) return;
    
    // Emit events for processing messages
    for (const msg of result.messages) {
      eventManager.emit('message.processing', {
        queue: msg.queue,
        partition: msg.partition,
        transactionId: msg.transactionId,
        workerId: `worker-${process.pid}`
      });
    }
    
    res.cork(() => {
      setCorsHeaders(res);
      if (result.messages.length > 0) {
        res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
      } else {
        res.writeStatus(config.HTTP_STATUS.NO_CONTENT.toString()).end();
      }
    });
  }).catch(error => {
    if (aborted) return;
    log('Pop error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/pop/queue/:queue', (res, req) => {
  const queue = req.getParameter(0);
  const query = new URLSearchParams(req.getQuery());
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Pop request aborted');
  });
  
  createPopRoute(queueManager, eventManager)(
    { 
      queue,
      consumerGroup: query.get('consumerGroup') || query.get('consumer_group')
    },
    {
      wait: query.get('wait') === 'true',
      timeout: parseInt(query.get('timeout') || config.QUEUE.DEFAULT_TIMEOUT.toString()),
      batch: parseInt(query.get('batch') || config.QUEUE.DEFAULT_BATCH_SIZE.toString()),
      subscriptionMode: query.get('subscriptionMode'),
      subscriptionFrom: query.get('subscriptionFrom')
    }
  ).then(result => {
    if (aborted) return;
    
    for (const msg of result.messages) {
      eventManager.emit('message.processing', {
        queue: msg.queue,
        partition: msg.partition,
        transactionId: msg.transactionId,
        workerId: `worker-${process.pid}`
      });
    }
    
    res.cork(() => {
      setCorsHeaders(res);
      if (result.messages.length > 0) {
        res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
      } else {
        res.writeStatus(config.HTTP_STATUS.NO_CONTENT.toString()).end();
      }
    });
  }).catch(error => {
    if (aborted) return;
    log('Pop error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

// Pop with filters (namespace or task)
app.get('/api/v1/pop', (res, req) => {
  const query = new URLSearchParams(req.getQuery());
  const namespace = query.get('namespace');
  const task = query.get('task');
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Pop request aborted');
  });
  
  createPopRoute(queueManager, eventManager)(
    { 
      namespace, 
      task,
      consumerGroup: query.get('consumerGroup') || query.get('consumer_group')
    },
    {
      wait: query.get('wait') === 'true',
      timeout: parseInt(query.get('timeout') || config.QUEUE.DEFAULT_TIMEOUT.toString()),
      batch: parseInt(query.get('batch') || config.QUEUE.DEFAULT_BATCH_SIZE.toString()),
      subscriptionMode: query.get('subscriptionMode'),
      subscriptionFrom: query.get('subscriptionFrom')
    }
  ).then(result => {
    if (aborted) return;
    
    for (const msg of result.messages) {
      eventManager.emit('message.processing', {
        queue: msg.queue,
        partition: msg.partition,
        transactionId: msg.transactionId,
        workerId: `worker-${process.pid}`
      });
    }
    
    res.cork(() => {
      setCorsHeaders(res);
      if (result.messages.length > 0) {
        res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
      } else {
        res.writeStatus(config.HTTP_STATUS.NO_CONTENT.toString()).end();
      }
    });
  }).catch(error => {
    if (aborted) return;
    log('Pop error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

// ACK route
app.post('/api/v1/ack', (res, req) => {
  const abortedRef = { aborted: false };
  res.onAborted(() => {
    abortedRef.aborted = true;
    log('ACK request aborted');
  });
  
  readJson(res, async (body) => {
    if (abortedRef.aborted) return;
    try {
      const result = await createAckRoute(queueManager)(body);
      if (abortedRef.aborted) return;
      
      // Emit completion or failure event
      if (body.status === 'completed') {
        eventManager.emit('message.completed', {
          transactionId: body.transactionId
        });
      } else {
        eventManager.emit('message.failed', {
          transactionId: body.transactionId,
          error: body.error
        });
      }
      
      res.cork(() => {
        setCorsHeaders(res);
        res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
      });
    } catch (error) {
      if (abortedRef.aborted) return;
      log('ACK error:', error);
      res.cork(() => {
        setCorsHeaders(res);
        res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
      });
    }
  }, abortedRef);
});

// Batch ACK route
app.post('/api/v1/ack/batch', (res, req) => {
  const abortedRef = { aborted: false };
  res.onAborted(() => {
    abortedRef.aborted = true;
    log('Batch ACK request aborted');
  });
  
  readJson(res, async (body) => {
    if (abortedRef.aborted) return;
    try {
      const results = await queueManager.acknowledgeMessages(body.acknowledgments, body.consumerGroup);
      if (abortedRef.aborted) return;
      
      // Emit events for each acknowledgment
      for (const ack of body.acknowledgments) {
        if (ack.status === 'completed') {
          eventManager.emit('message.completed', {
            transactionId: ack.transactionId
          });
        } else {
          eventManager.emit('message.failed', {
            transactionId: ack.transactionId,
            error: ack.error
          });
        }
      }
      
      res.cork(() => {
        setCorsHeaders(res);
        res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify({
          processed: results.length,
          results
        }));
      });
    } catch (error) {
      if (abortedRef.aborted) return;
      log('Batch ACK error:', error);
      res.cork(() => {
        setCorsHeaders(res);
        res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
      });
    }
  }, abortedRef);
});

// Analytics routes
app.get('/api/v1/analytics/queues', (res, req) => {
  const query = new URLSearchParams(req.getQuery());
  const fromDateTime = query.get('fromDateTime');
  const toDateTime = query.get('toDateTime');
  const queue = query.get('queue');
  const namespace = query.get('namespace');
  const task = query.get('task');
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Analytics request aborted');
  });
  
  analyticsRoutes.getQueues({ fromDateTime, toDateTime, queue, namespace, task }).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    log('Analytics error:', error.message || error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/analytics/queue/:queue', (res, req) => {
  const queue = req.getParameter(0);
  const query = new URLSearchParams(req.getQuery());
  const fromDateTime = query.get('fromDateTime');
  const toDateTime = query.get('toDateTime');
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Analytics request aborted');
  });
  
  analyticsRoutes.getQueueStats(queue, { fromDateTime, toDateTime }).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    log('Analytics error:', error.message || error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

// Analytics with filters
app.get('/api/v1/analytics', (res, req) => {
  const query = new URLSearchParams(req.getQuery());
  const namespace = query.get('namespace');
  const task = query.get('task');
  const fromDateTime = query.get('fromDateTime');
  const toDateTime = query.get('toDateTime');
  const queue = query.get('queue');
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Analytics request aborted');
  });
  
  const filters = { fromDateTime, toDateTime };
  const getStats = namespace 
    ? analyticsRoutes.getNamespaceStats(namespace, filters)
    : task 
    ? analyticsRoutes.getTaskStats(task, filters)
    : analyticsRoutes.getQueues({ ...filters, queue, namespace, task });
    
  getStats.then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    log('Analytics error:', error.message || error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/analytics/queue-depths', (res, req) => {
  const query = new URLSearchParams(req.getQuery());
  const fromDateTime = query.get('fromDateTime');
  const toDateTime = query.get('toDateTime');
  const queue = query.get('queue');
  const namespace = query.get('namespace');
  const task = query.get('task');
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Analytics request aborted');
  });
  
  analyticsRoutes.getQueueDepths({ fromDateTime, toDateTime, queue, namespace, task }).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    log('Analytics error:', error.message || error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/analytics/throughput', (res, req) => {
  const query = new URLSearchParams(req.getQuery());
  const fromDateTime = query.get('fromDateTime');
  const toDateTime = query.get('toDateTime');
  const queue = query.get('queue');
  const namespace = query.get('namespace');
  const task = query.get('task');
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Analytics request aborted');
  });
  
  analyticsRoutes.getThroughput(pool, { fromDateTime, toDateTime, queue, namespace, task }).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    log('Analytics error:', error.message || error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/analytics/queue-lag', (res, req) => {
  const query = new URLSearchParams(req.getQuery());
  const queue = query.get('queue');
  const namespace = query.get('namespace');
  const task = query.get('task');
  const fromDateTime = query.get('fromDateTime');
  const toDateTime = query.get('toDateTime');
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Queue lag request aborted');
  });
  
  analyticsRoutes.getQueueLag({ queue, namespace, task, fromDateTime, toDateTime }).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    log('Queue lag error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

// Queue stats route
app.get('/api/v1/analytics/queue-stats', (res, req) => {
  const query = new URLSearchParams(req.getQuery());
  const queue = query.get('queue');
  const namespace = query.get('namespace');
  const task = query.get('task');
  const fromDateTime = query.get('fromDateTime');
  const toDateTime = query.get('toDateTime');
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Queue stats request aborted');
  });
  
  queueManager.getQueueStats({ queue, namespace, task, fromDateTime, toDateTime }).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    log('Queue stats error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

// Messages routes
app.get('/api/v1/messages', (res, req) => {
  const query = new URLSearchParams(req.getQuery());
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Messages list request aborted');
  });
  
  const filters = {
    ns: query.get('ns'),
    task: query.get('task'),
    queue: query.get('queue'),
    status: query.get('status'),
    limit: parseInt(query.get('limit') || config.API.DEFAULT_LIMIT.toString()),
    offset: parseInt(query.get('offset') || config.API.DEFAULT_OFFSET.toString())
  };
  
  messagesRoutes.listMessages(filters).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify({ messages: result }));
    });
  }).catch(error => {
    if (aborted) return;
    log('List messages error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/messages/:transactionId', (res, req) => {
  const transactionId = req.getParameter(0);
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Get message request aborted');
  });
  
  messagesRoutes.getMessage(transactionId).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    log('Get message error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(error.message === 'Message not found' ? config.HTTP_STATUS.NOT_FOUND.toString() : config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString())
        .end(JSON.stringify({ error: error.message }));
    });
  });
});

app.del('/api/v1/messages/:transactionId', (res, req) => {
  const transactionId = req.getParameter(0);
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Delete message request aborted');
  });
  
  messagesRoutes.deleteMessage(transactionId).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    log('Delete message error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(error.message === 'Message not found' ? config.HTTP_STATUS.NOT_FOUND.toString() : config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString())
        .end(JSON.stringify({ error: error.message }));
    });
  });
});

app.post('/api/v1/messages/:transactionId/retry', (res, req) => {
  const transactionId = req.getParameter(0);
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Retry message request aborted');
  });
  
  messagesRoutes.retryMessage(transactionId).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    log('Retry message error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

app.post('/api/v1/messages/:transactionId/dlq', (res, req) => {
  const transactionId = req.getParameter(0);
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Move to DLQ request aborted');
  });
  
  messagesRoutes.moveToDLQ(transactionId).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    log('Move to DLQ error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/messages/:transactionId/related', (res, req) => {
  const transactionId = req.getParameter(0);
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Get related messages request aborted');
  });
  
  messagesRoutes.getRelatedMessages(transactionId).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify({ messages: result }));
    });
  }).catch(error => {
    if (aborted) return;
    log('Get related messages error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

app.del('/api/v1/queues/:queue/clear', (res, req) => {
  const queue = req.getParameter(0);
  const query = new URLSearchParams(req.getQuery());
  const partition = query.get('partition');
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Clear queue request aborted');
  });
  
  messagesRoutes.clearQueue(queue, partition).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    log('Clear queue error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

// Resources routes
app.get('/api/v1/resources/queues', (res, req) => {
  const query = new URLSearchParams(req.getQuery());
  const namespace = query.get('namespace');
  const task = query.get('task');
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Get queues request aborted');
  });
  
  resourcesRoutes.getQueues({ namespace, task }).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify({ queues: result }));
    });
  }).catch(error => {
    if (aborted) return;
    log('Get queues error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/resources/queues/:queue', (res, req) => {
  const queue = req.getParameter(0);
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Get queue request aborted');
  });
  
  resourcesRoutes.getQueue(queue).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    log('Get queue error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(error.message === 'Queue not found' ? config.HTTP_STATUS.NOT_FOUND.toString() : config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString())
        .end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/resources/partitions', (res, req) => {
  const query = new URLSearchParams(req.getQuery());
  const queue = query.get('queue');
  const minDepth = query.get('minDepth');
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Get partitions request aborted');
  });
  
  resourcesRoutes.getPartitions({ queue, minDepth: minDepth ? parseInt(minDepth) : null }).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify({ partitions: result }));
    });
  }).catch(error => {
    if (aborted) return;
    log('Get partitions error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/resources/namespaces', (res, req) => {
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Get namespaces request aborted');
  });
  
  resourcesRoutes.getNamespaces().then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify({ namespaces: result }));
    });
  }).catch(error => {
    if (aborted) return;
    log('Get namespaces error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/resources/tasks', (res, req) => {
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Get tasks request aborted');
  });
  
  resourcesRoutes.getTasks().then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify({ tasks: result }));
    });
  }).catch(error => {
    if (aborted) return;
    log('Get tasks error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/resources/overview', (res, req) => {
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Get overview request aborted');
  });
  
  resourcesRoutes.getSystemOverview().then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    log('Get overview error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

// Delete queue
app.del('/api/v1/resources/queues/:queue', (res, req) => {
  const queue = req.getParameter(0);
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Delete queue request aborted');
  });
  
  resourcesRoutes.deleteQueue(queue).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    log('Delete queue error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.INTERNAL_SERVER_ERROR.toString()).end(JSON.stringify({ error: error.message }));
    });
  });
});

// Health check with performance stats
app.get('/health', async (res, req) => {
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    log('Health check aborted');
  });
  
  try {
    await pool.query('SELECT 1');
    if (aborted) return;
    
    const uptime = (Date.now() - startTime) / 1000;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify({ 
        status: 'healthy',
        uptime: `${Math.floor(uptime)}s`,
        connections: wsServer.getConnectionCount(),
        stats: {
          requests: requestCount,
          messages: messageCount,
          requestsPerSecond: (requestCount / uptime).toFixed(2),
          messagesPerSecond: (messageCount / uptime).toFixed(2),
          pool: {
            total: pool.totalCount,
            idle: pool.idleCount,
            waiting: pool.waitingCount
          }
        }
      }));
    });
  } catch (error) {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(config.HTTP_STATUS.SERVICE_UNAVAILABLE.toString()).end(JSON.stringify({ 
        status: 'unhealthy',
        error: error.message
      }));
    });
  }
});

// Metrics endpoint for performance monitoring
app.get('/metrics', (res, req) => {
  res.onAborted(() => {
    log('Metrics request aborted');
  });
  
  const uptime = (Date.now() - startTime) / 1000;
  const metrics = {
    uptime: uptime,
    requests: {
      total: requestCount,
      rate: requestCount / uptime
    },
    messages: {
      total: messageCount,
      rate: messageCount / uptime
    },
    database: {
      poolSize: pool.totalCount,
      idleConnections: pool.idleCount,
      waitingRequests: pool.waitingCount
    },
    memory: process.memoryUsage(),
    cpu: process.cpuUsage()
  };
  
  res.cork(() => {
    setCorsHeaders(res);
    res.writeStatus(config.HTTP_STATUS.OK.toString()).end(JSON.stringify(metrics));
  });
});

// Variable to hold system event consumer stop function
let stopSystemEventConsumer = null;

// Start server
app.listen(HOST, PORT, async (token) => {
  if (token) {
    log(`✅ Queen Server running at http://${HOST}:${PORT}`);
    log(`📊 Features enabled:`);
    log(`   - Encryption: ${encryptionEnabled ? '✅' : '❌ (set QUEEN_ENCRYPTION_KEY)'}`);
    log(`   - Retention: ✅`);
    log(`   - Eviction: ✅`);
    log(`   - WebSocket Dashboard: ws://${HOST}:${PORT}/ws/dashboard`);
    log(`   - Server Instance ID: ${SERVER_INSTANCE_ID}`);
    
    // Start system event synchronization and consumption
    try {
      // Create temporary client for synchronization
      const syncClient = createQueenClient({ 
        baseUrl: `http://localhost:${PORT}` 
      });
      
      // Synchronize system events (catch up on missed events)
      await syncSystemEvents(syncClient, systemEventManager, SERVER_INSTANCE_ID);
      
      // Start consuming system events
      // Note: consume() returns the stop function directly
      stopSystemEventConsumer = syncClient.consume({
        queue: SYSTEM_QUEUE,
        consumerGroup: SERVER_INSTANCE_ID,
        handler: async (message) => {
          await systemEventManager.processSystemEvent(message.payload);
        },
        options: {
          batch: 10,
          wait: true
        }
      });
      
      log(`✅ System event consumer started`);
    } catch (error) {
      log(`⚠️ Failed to start system event consumer: ${error.message}`);
    }
    
    log(`🎯 Ready to process messages with high performance!`);
  } else {
    log(`Failed to start server on port ${PORT}`);
    process.exit(1);
  }
});

// Graceful shutdown
const shutdown = async (signal) => {
  log(`\n🛑 Received ${signal}, shutting down gracefully...`);
  
  // Stop system event consumer
  if (stopSystemEventConsumer) {
    stopSystemEventConsumer();
    log('✅ System event consumer stopped');
  }
  
  // Stop background jobs
  if (stopRetention) stopRetention();
  if (stopEviction) stopEviction();
  
  const uptime = (Date.now() - startTime) / 1000;
  if (messageCount > 0) {
    log(`📊 Final stats: ${messageCount} messages processed in ${uptime.toFixed(1)}s`);
    log(`   Average: ${(messageCount / uptime).toFixed(2)} messages/second`);
  }
  
  await pool.end();
  process.exit(0);
};

process.on('SIGTERM', () => shutdown('SIGTERM'));
process.on('SIGINT', () => shutdown('SIGINT'));