import uWS from 'uWebSockets.js';
import { createPool, initDatabase } from './database/connection.js';
import { createQueueManager } from './managers/queueManager.js';
import { createResourceCache } from './managers/resourceCache.js';
import { createEventManager } from './managers/eventManager.js';
import { createPushRoute } from './routes/push.js';
import { createPopRoute } from './routes/pop.js';
import { createAckRoute } from './routes/ack.js';
import { createConfigureRoute } from './routes/configure.js';
import { createAnalyticsRoutes } from './routes/analytics.js';
import { createWebSocketServer } from './websocket/wsServer.js';

const PORT = process.env.PORT || 6632;
const HOST = process.env.HOST || '0.0.0.0';

// Initialize components
const pool = createPool();
const resourceCache = createResourceCache();
const eventManager = createEventManager();
const queueManager = createQueueManager(pool, resourceCache);
const analyticsRoutes = createAnalyticsRoutes(queueManager);

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

// Helper to read JSON body
const readJson = (res, cb) => {
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
  
  res.onAborted(() => {
    console.log('Request aborted');
  });
};

// Configure route
app.post('/api/v1/configure', (res, req) => {
  readJson(res, async (body) => {
    try {
      const result = await createConfigureRoute(queueManager)(body);
      eventManager.emit('queue.created', result);
      res.writeStatus('201').end(JSON.stringify(result));
    } catch (error) {
      console.error('Configure error:', error);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    }
  });
});

// Push route
app.post('/api/v1/push', (res, req) => {
  readJson(res, async (body) => {
    try {
      const result = await createPushRoute(queueManager)(body);
      
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
      
      res.writeStatus('201').end(JSON.stringify(result));
    } catch (error) {
      console.error('Push error:', error);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    }
  });
});

// Pop routes with different scopes
app.get('/api/v1/pop/ns/:ns/task/:task/queue/:queue', (res, req) => {
  const ns = req.getParameter(0);
  const task = req.getParameter(1);
  const queue = req.getParameter(2);
  const query = new URLSearchParams(req.getQuery());
  
  res.onAborted(() => {
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
    // Emit events for processing messages
    for (const msg of result.messages) {
      eventManager.emit('message.processing', {
        queue: msg.queue,
        transactionId: msg.transactionId,
        workerId: `worker-${process.pid}`
      });
    }
    
    res.cork(() => {
      if (result.messages.length > 0) {
        res.writeStatus('200').end(JSON.stringify(result));
      } else {
        res.writeStatus('204').end();
      }
    });
  }).catch(error => {
    console.error('Pop error:', error);
    res.cork(() => {
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/pop/ns/:ns/task/:task', (res, req) => {
  const ns = req.getParameter(0);
  const task = req.getParameter(1);
  const query = new URLSearchParams(req.getQuery());
  
  res.onAborted(() => {
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
    for (const msg of result.messages) {
      eventManager.emit('message.processing', {
        queue: msg.queue,
        transactionId: msg.transactionId,
        workerId: `worker-${process.pid}`
      });
    }
    
    res.cork(() => {
      if (result.messages.length > 0) {
        res.writeStatus('200').end(JSON.stringify(result));
      } else {
        res.writeStatus('204').end();
      }
    });
  }).catch(error => {
    console.error('Pop error:', error);
    res.cork(() => {
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/pop/ns/:ns', (res, req) => {
  const ns = req.getParameter(0);
  const query = new URLSearchParams(req.getQuery());
  
  res.onAborted(() => {
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
    for (const msg of result.messages) {
      eventManager.emit('message.processing', {
        queue: msg.queue,
        transactionId: msg.transactionId,
        workerId: `worker-${process.pid}`
      });
    }
    
    res.cork(() => {
      if (result.messages.length > 0) {
        res.writeStatus('200').end(JSON.stringify(result));
      } else {
        res.writeStatus('204').end();
      }
    });
  }).catch(error => {
    console.error('Pop error:', error);
    res.cork(() => {
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

// ACK route
app.post('/api/v1/ack', (res, req) => {
  readJson(res, async (body) => {
    try {
      const result = await createAckRoute(queueManager)(body);
      
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
      
      res.writeStatus('200').end(JSON.stringify(result));
    } catch (error) {
      console.error('ACK error:', error);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    }
  });
});

// Batch ACK route
app.post('/api/v1/ack/batch', (res, req) => {
  readJson(res, async (body) => {
    try {
      const results = await queueManager.acknowledgeMessages(body.acknowledgments);
      
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
      
      res.writeStatus('200').end(JSON.stringify({
        processed: results.length,
        results
      }));
    } catch (error) {
      console.error('Batch ACK error:', error);
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    }
  });
});

// Analytics routes
app.get('/api/v1/analytics/queues', (res, req) => {
  res.onAborted(() => {
    console.log('Analytics request aborted');
  });
  
  analyticsRoutes.getQueues().then(result => {
    res.cork(() => {
      res.writeStatus('200').end(JSON.stringify(result));
    });
  }).catch(error => {
    console.error('Analytics error:', error);
    res.cork(() => {
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/analytics/ns/:ns', (res, req) => {
  const ns = req.getParameter(0);
  
  res.onAborted(() => {
    console.log('Analytics request aborted');
  });
  
  analyticsRoutes.getNamespaceStats(ns).then(result => {
    res.cork(() => {
      res.writeStatus('200').end(JSON.stringify(result));
    });
  }).catch(error => {
    console.error('Analytics error:', error);
    res.cork(() => {
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/analytics/ns/:ns/task/:task', (res, req) => {
  const ns = req.getParameter(0);
  const task = req.getParameter(1);
  
  res.onAborted(() => {
    console.log('Analytics request aborted');
  });
  
  analyticsRoutes.getTaskStats(ns, task).then(result => {
    res.cork(() => {
      res.writeStatus('200').end(JSON.stringify(result));
    });
  }).catch(error => {
    console.error('Analytics error:', error);
    res.cork(() => {
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/analytics/queue-depths', (res, req) => {
  res.onAborted(() => {
    console.log('Analytics request aborted');
  });
  
  analyticsRoutes.getQueueDepths().then(result => {
    res.cork(() => {
      res.writeStatus('200').end(JSON.stringify(result));
    });
  }).catch(error => {
    console.error('Analytics error:', error);
    res.cork(() => {
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

app.get('/api/v1/analytics/throughput', (res, req) => {
  res.onAborted(() => {
    console.log('Analytics request aborted');
  });
  
  analyticsRoutes.getThroughput(pool).then(result => {
    res.cork(() => {
      res.writeStatus('200').end(JSON.stringify(result));
    });
  }).catch(error => {
    console.error('Analytics error:', error);
    res.cork(() => {
      res.writeStatus('500').end(JSON.stringify({ error: error.message }));
    });
  });
});

// Health check
app.get('/health', (res, req) => {
  res.writeStatus('200').end(JSON.stringify({ 
    status: 'healthy',
    connections: wsServer.getConnectionCount()
  }));
});

// Start server
app.listen(HOST, PORT, (token) => {
  if (token) {
    console.log(`Queen server listening on http://${HOST}:${PORT}`);
    console.log(`WebSocket dashboard available at ws://${HOST}:${PORT}/ws/dashboard`);
  } else {
    console.log(`Failed to start server on port ${PORT}`);
    process.exit(1);
  }
});

// Graceful shutdown
process.on('SIGTERM', async () => {
  console.log('Shutting down gracefully...');
  await pool.end();
  process.exit(0);
});