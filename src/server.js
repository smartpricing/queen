import uWS from 'uWebSockets.js';
import pg from 'pg';
import { initDatabase } from './database/connection.js';
import { createOptimizedQueueManager } from './managers/queueManagerOptimized.js';
import { createResourceCache } from './managers/resourceCache.js';
import { createEventManager } from './managers/eventManager.js';
import { createPushRoute } from './routes/push.js';
import { createPopRoute } from './routes/pop.js';
import { createAckRoute } from './routes/ack.js';
import { createConfigureRoute } from './routes/configure.js';
import { createAnalyticsRoutes } from './routes/analytics.js';
import { createMessagesRoutes } from './routes/messages.js';
import { createWebSocketServer } from './websocket/wsServer.js';

const PORT = process.env.PORT || 6632;
const HOST = process.env.HOST || '0.0.0.0';

// Performance monitoring
let requestCount = 0;
let messageCount = 0;
const startTime = Date.now();

// Create optimized connection pool
const { Pool } = pg;
const createOptimizedPool = () => {
  const poolSize = parseInt(process.env.DB_POOL_SIZE) || 20;
  const idleTimeout = parseInt(process.env.DB_IDLE_TIMEOUT) || 30000;
  const connectionTimeout = parseInt(process.env.DB_CONNECTION_TIMEOUT) || 2000;
  
  if (poolSize >= 50) {
    console.log('🚀 High-performance mode enabled');
    console.log(`   Pool Size: ${poolSize}`);
    console.log(`   Connection Timeout: ${connectionTimeout}ms`);
  }
  
  return new Pool({
    user: process.env.PG_USER || 'postgres',
    host: process.env.PG_HOST || 'localhost',
    database: process.env.PG_DB || 'postgres',
    password: process.env.PG_PASSWORD || 'postgres',
    port: process.env.PG_PORT || 5432,
    max: poolSize,
    idleTimeoutMillis: idleTimeout,
    connectionTimeoutMillis: connectionTimeout,
    statement_timeout: 30000,
    query_timeout: 30000,
    application_name: 'queen-uws'
  });
};

// Initialize components
const pool = createOptimizedPool();
const resourceCache = createResourceCache();
const eventManager = createEventManager();

// Always use optimized queue manager - it's better for all scenarios
const queueManager = createOptimizedQueueManager(pool, resourceCache, eventManager);
const analyticsRoutes = createAnalyticsRoutes(queueManager);
const messagesRoutes = createMessagesRoutes(pool, queueManager);

// Initialize database
await initDatabase(pool);

// Background job for lease reclamation
setInterval(async () => {
  try {
    const reclaimed = await queueManager.reclaimExpiredLeases();
    if (reclaimed > 0) {
      console.log(`Reclaimed ${reclaimed} expired leases`);
    }
  } catch (error) {
    console.error('Error reclaiming leases:', error);
  }
}, 5000); // Check every 5 seconds

// Create server
const app = uWS.App();

// Initialize WebSocket server
const wsServer = createWebSocketServer(app, eventManager);

// Background job for queue depth updates
setInterval(async () => {
  await wsServer.updateQueueDepths(queueManager);
}, 5000);

// CORS headers helper
const setCorsHeaders = (res) => {
  res.writeHeader('Access-Control-Allow-Origin', '*');
  res.writeHeader('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
  res.writeHeader('Access-Control-Allow-Headers', 'Content-Type, Authorization');
  res.writeHeader('Access-Control-Max-Age', '86400');
};

// Helper to read JSON body
const readJson = (res, cb, abortedRef) => {
  let buffer;
  res.onData((ab, isLast) => {
    const chunk = Buffer.from(ab);
    if (isLast) {
      let json;
      try {
        if (buffer) {
          json = JSON.parse(Buffer.concat([buffer, chunk]).toString());
        } else {
          json = JSON.parse(chunk.toString());
        }
      } catch (e) {
        if (abortedRef && abortedRef.aborted) return;
        res.writeStatus('400').end(JSON.stringify({ error: 'Invalid JSON' }));
        return;
      }
      cb(json);
    } else {
      if (buffer) {
        buffer = Buffer.concat([buffer, chunk]);
      } else {
        buffer = chunk;
      }
    }
  });
};

// Handle OPTIONS requests for CORS preflight
app.options('/*', (res, req) => {
  res.onAborted(() => {
    console.log('OPTIONS request aborted');
  });
  
  res.cork(() => {
    setCorsHeaders(res);
    res.writeStatus('204').end();
  });
});

// Configure route
app.post('/api/v1/configure', (res, req) => {
  const abortedRef = { aborted: false };
  res.onAborted(() => {
    abortedRef.aborted = true;
    console.log('Configure request aborted');
  });
  
  readJson(res, async (body) => {
    if (abortedRef.aborted) return;
    try {
      const result = await createConfigureRoute(queueManager)(body);
      if (abortedRef.aborted) return;
      eventManager.emit('queue.created', result);
      res.cork(() => {
        setCorsHeaders(res);
        res.writeStatus('201').end(JSON.stringify(result));
      });
    } catch (error) {
      if (abortedRef.aborted) return;
      console.error('Configure error:', error);
      res.cork(() => {
        setCorsHeaders(res);
        res.writeStatus('500').end(JSON.stringify({ error: error.message }));
      });
    }
  }, abortedRef);
});

// Push route
app.post('/api/v1/push', (res, req) => {
  const abortedRef = { aborted: false };
  res.onAborted(() => {
    abortedRef.aborted = true;
    console.log('Push request aborted');
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
            const queuePath = `${item.ns}/${item.task}/${item.queue}`;
            eventManager.emit('message.pushed', {
              queue: queuePath,
              transactionId: msg.transactionId
            });
            eventManager.notifyMessageAvailable(queuePath);
          }
        }
      }
      
      res.cork(() => {
        setCorsHeaders(res);
        res.writeStatus('201').end(JSON.stringify(result));
      });
    } catch (error) {
      if (abortedRef.aborted) return;
      console.error('Push error:', error);
      res.cork(() => {
        setCorsHeaders(res);
        res.writeStatus('500').end(JSON.stringify({ error: error.message }));
      });
    }
  }, abortedRef);
});

// Pop routes with different scopes
app.get('/api/v1/pop/ns/:ns/task/:task/queue/:queue', (res, req) => {
  const ns = req.getParameter(0);
  const task = req.getParameter(1);
  const queue = req.getParameter(2);
  const query = new URLSearchParams(req.getQuery());
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Pop request aborted');
  });
  
  createPopRoute(queueManager, eventManager)(
    { ns, task, queue },
    {
      wait: query.get('wait') === 'true',
      timeout: parseInt(query.get('timeout') || '30000'),
      batch: parseInt(query.get('batch') || '1')
    }
  ).then(result => {
    if (aborted) return;
    
    // Emit events for processing messages
    for (const msg of result.messages) {
      eventManager.emit('message.processing', {
        queue: msg.queue,
        transactionId: msg.transactionId,
        workerId: `worker-${process.pid}`
      });
    }
    
    res.cork(() => {
      setCorsHeaders(res);
      if (result.messages.length > 0) {
        res.writeStatus('200').end(JSON.stringify(result));
      } else {
        res.writeHeader('Content-Length', '0').writeStatus('204').end();
      }
    });
  }).catch(error => {
    if (aborted) return;
    console.error('Pop error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/pop/ns/:ns/task/:task', (res, req) => {
  const ns = req.getParameter(0);
  const task = req.getParameter(1);
  const query = new URLSearchParams(req.getQuery());
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Pop request aborted');
  });
  
  createPopRoute(queueManager, eventManager)(
    { ns, task },
    {
      wait: query.get('wait') === 'true',
      timeout: parseInt(query.get('timeout') || '30000'),
      batch: parseInt(query.get('batch') || '1')
    }
  ).then(result => {
    if (aborted) return;
    
    for (const msg of result.messages) {
      eventManager.emit('message.processing', {
        queue: msg.queue,
        transactionId: msg.transactionId,
        workerId: `worker-${process.pid}`
      });
    }
    
    res.cork(() => {
      setCorsHeaders(res);
      if (result.messages.length > 0) {
        res.writeStatus('200').end(JSON.stringify(result));
      } else {
        res.writeHeader('Content-Length', '0').writeStatus('204').end();
      }
    });
  }).catch(error => {
    if (aborted) return;
    console.error('Pop error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/pop/ns/:ns', (res, req) => {
  const ns = req.getParameter(0);
  const query = new URLSearchParams(req.getQuery());
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Pop request aborted');
  });
  
  createPopRoute(queueManager, eventManager)(
    { ns },
    {
      wait: query.get('wait') === 'true',
      timeout: parseInt(query.get('timeout') || '30000'),
      batch: parseInt(query.get('batch') || '1')
    }
  ).then(result => {
    if (aborted) return;
    
    for (const msg of result.messages) {
      eventManager.emit('message.processing', {
        queue: msg.queue,
        transactionId: msg.transactionId,
        workerId: `worker-${process.pid}`
      });
    }
    
    res.cork(() => {
      setCorsHeaders(res);
      if (result.messages.length > 0) {
        res.writeStatus('200').end(JSON.stringify(result));
      } else {
        res.writeHeader('Content-Length', '0').writeStatus('204').end();
      }
    });
  }).catch(error => {
    if (aborted) return;
    console.error('Pop error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

// ACK route
app.post('/api/v1/ack', (res, req) => {
  const abortedRef = { aborted: false };
  res.onAborted(() => {
    abortedRef.aborted = true;
    console.log('ACK request aborted');
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
        res.writeStatus('200').end(JSON.stringify(result));
      });
    } catch (error) {
      if (abortedRef.aborted) return;
      console.error('ACK error:', error);
      res.cork(() => {
        setCorsHeaders(res);
        res.writeStatus('500').end(JSON.stringify({ error: error.message }));
      });
    }
  }, abortedRef);
});

// Batch ACK route
app.post('/api/v1/ack/batch', (res, req) => {
  const abortedRef = { aborted: false };
  res.onAborted(() => {
    abortedRef.aborted = true;
    console.log('Batch ACK request aborted');
  });
  
  readJson(res, async (body) => {
    if (abortedRef.aborted) return;
    try {
      const results = await queueManager.acknowledgeMessages(body.acknowledgments);
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
        res.writeStatus('200').end(JSON.stringify({
          processed: results.length,
          results
        }));
      });
    } catch (error) {
      if (abortedRef.aborted) return;
      console.error('Batch ACK error:', error);
      res.cork(() => {
        setCorsHeaders(res);
        res.writeStatus('500').end(JSON.stringify({ error: error.message }));
      });
    }
  }, abortedRef);
});

// Analytics routes
app.get('/api/v1/analytics/queues', (res, req) => {
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Analytics request aborted');
  });
  
  analyticsRoutes.getQueues().then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('200').end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    console.error('Analytics error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/analytics/ns/:ns', (res, req) => {
  const ns = req.getParameter(0);
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Analytics request aborted');
  });
  
  analyticsRoutes.getNamespaceStats(ns).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('200').end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    console.error('Analytics error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/analytics/ns/:ns/task/:task', (res, req) => {
  const ns = req.getParameter(0);
  const task = req.getParameter(1);
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Analytics request aborted');
  });
  
  analyticsRoutes.getTaskStats(ns, task).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('200').end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    console.error('Analytics error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/analytics/queue-depths', (res, req) => {
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Analytics request aborted');
  });
  
  analyticsRoutes.getQueueDepths().then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('200').end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    console.error('Analytics error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/analytics/throughput', (res, req) => {
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Analytics request aborted');
  });
  
  analyticsRoutes.getThroughput(pool).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('200').end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    console.error('Analytics error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

// Queue stats route
app.get('/api/v1/analytics/queue-stats', (res, req) => {
  const query = new URLSearchParams(req.getQuery());
  const ns = query.get('ns');
  const task = query.get('task');
  const queue = query.get('queue');
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Queue stats request aborted');
  });
  
  queueManager.getQueueStats({ ns, task, queue }).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('200').end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    console.error('Queue stats error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

// Messages routes
app.get('/api/v1/messages', (res, req) => {
  const query = new URLSearchParams(req.getQuery());
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Messages list request aborted');
  });
  
  const filters = {
    ns: query.get('ns'),
    task: query.get('task'),
    queue: query.get('queue'),
    status: query.get('status'),
    limit: parseInt(query.get('limit') || '100'),
    offset: parseInt(query.get('offset') || '0')
  };
  
  messagesRoutes.listMessages(filters).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('200').end(JSON.stringify({ messages: result }));
    });
  }).catch(error => {
    if (aborted) return;
    console.error('List messages error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/messages/:transactionId', (res, req) => {
  const transactionId = req.getParameter(0);
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Get message request aborted');
  });
  
  messagesRoutes.getMessage(transactionId).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('200').end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    console.error('Get message error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(error.message === 'Message not found' ? '404' : '500')
        .end(JSON.stringify({ error: error.message }));
    });
  });
});

app.del('/api/v1/messages/:transactionId', (res, req) => {
  const transactionId = req.getParameter(0);
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Delete message request aborted');
  });
  
  messagesRoutes.deleteMessage(transactionId).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('200').end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    console.error('Delete message error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus(error.message === 'Message not found' ? '404' : '500')
        .end(JSON.stringify({ error: error.message }));
    });
  });
});

app.post('/api/v1/messages/:transactionId/retry', (res, req) => {
  const transactionId = req.getParameter(0);
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Retry message request aborted');
  });
  
  messagesRoutes.retryMessage(transactionId).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('200').end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    console.error('Retry message error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

app.post('/api/v1/messages/:transactionId/dlq', (res, req) => {
  const transactionId = req.getParameter(0);
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Move to DLQ request aborted');
  });
  
  messagesRoutes.moveToDLQ(transactionId).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('200').end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    console.error('Move to DLQ error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/messages/:transactionId/related', (res, req) => {
  const transactionId = req.getParameter(0);
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Get related messages request aborted');
  });
  
  messagesRoutes.getRelatedMessages(transactionId).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('200').end(JSON.stringify({ messages: result }));
    });
  }).catch(error => {
    if (aborted) return;
    console.error('Get related messages error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

app.del('/api/v1/queues/:ns/:task/:queue/clear', (res, req) => {
  const ns = req.getParameter(0);
  const task = req.getParameter(1);
  const queue = req.getParameter(2);
  
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Clear queue request aborted');
  });
  
  messagesRoutes.clearQueue(ns, task, queue).then(result => {
    if (aborted) return;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('200').end(JSON.stringify(result));
    });
  }).catch(error => {
    if (aborted) return;
    console.error('Clear queue error:', error);
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

// Health check with performance stats
app.get('/health', async (res, req) => {
  let aborted = false;
  res.onAborted(() => {
    aborted = true;
    console.log('Health check aborted');
  });
  
  try {
    await pool.query('SELECT 1');
    if (aborted) return;
    
    const uptime = (Date.now() - startTime) / 1000;
    res.cork(() => {
      setCorsHeaders(res);
      res.writeStatus('200').end(JSON.stringify({ 
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
      res.writeStatus('503').end(JSON.stringify({ 
        status: 'unhealthy',
        error: error.message
      }));
    });
  }
});

// Metrics endpoint for performance monitoring
app.get('/metrics', (res, req) => {
  res.onAborted(() => {
    console.log('Metrics request aborted');
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
    res.writeStatus('200').end(JSON.stringify(metrics));
  });
});

// Start server
app.listen(HOST, PORT, (token) => {
  if (token) {
    console.log(`Queen server listening on http://${HOST}:${PORT}`);
    console.log(`WebSocket dashboard available at ws://${HOST}:${PORT}/ws/dashboard`);
    console.log('⚡ Optimized queue manager enabled');
    console.log('📊 Monitor performance at /metrics');
    
    const poolSize = parseInt(process.env.DB_POOL_SIZE || '20');
    if (poolSize >= 50) {
      console.log('🚀 High-performance mode: Pool size =', poolSize);
      console.log('📈 Target: 10,000+ messages/second');
    }
  } else {
    console.log(`Failed to start server on port ${PORT}`);
    process.exit(1);
  }
});

// Performance monitoring log (only when processing messages)
setInterval(() => {
  if (messageCount > 0) {
    const uptime = (Date.now() - startTime) / 1000;
    const rps = requestCount / uptime;
    const mps = messageCount / uptime;
    console.log(`📊 Performance: ${requestCount} requests, ${messageCount} messages, ${rps.toFixed(1)} req/s, ${mps.toFixed(1)} msg/s`);
  }
}, 30000); // Log every 30 seconds

// Graceful shutdown
const shutdown = async (signal) => {
  console.log(`\n🛑 Received ${signal}, shutting down gracefully...`);
  const uptime = (Date.now() - startTime) / 1000;
  if (messageCount > 0) {
    console.log(`📊 Final stats: ${messageCount} messages processed in ${uptime.toFixed(1)}s`);
    console.log(`   Average: ${(messageCount / uptime).toFixed(2)} messages/second`);
  }
  await pool.end();
  process.exit(0);
};

process.on('SIGTERM', () => shutdown('SIGTERM'));
process.on('SIGINT', () => shutdown('SIGINT'));