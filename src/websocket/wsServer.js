import { generateUUID } from '../utils/uuid.js';
import config from '../config.js';

export const createWebSocketServer = (app, eventManager) => {
  const connections = new Map();
  
  // Broadcast event to all connected clients
  const broadcast = (event, data) => {
    const message = JSON.stringify({ event, data, timestamp: new Date().toISOString() });
    
    for (const [id, ws] of connections) {
      try {
        ws.send(message);
      } catch (error) {
        console.error(`Failed to send to client ${id}:`, error);
        connections.delete(id);
      }
    }
  };
  
  // Setup WebSocket route
  app.ws('/ws/dashboard', {
    compression: config.WEBSOCKET.COMPRESSION,
    maxPayloadLength: config.WEBSOCKET.MAX_PAYLOAD_LENGTH,
    idleTimeout: config.WEBSOCKET.IDLE_TIMEOUT,
    
    open: (ws) => {
      const connectionId = generateUUID();
      connections.set(connectionId, ws);
      ws.connectionId = connectionId;
      
      console.log(`WebSocket client connected: ${connectionId}`);
      
      // Send welcome message
      ws.send(JSON.stringify({
        event: 'connected',
        data: { 
          connectionId,
          version: 'v2'
        },
        timestamp: new Date().toISOString()
      }));
      
      broadcast('client.connected', { clientId: connectionId });
    },
    
    message: (ws, message, isBinary) => {
      // Handle ping/pong
      const msg = Buffer.from(message).toString();
      if (msg === 'ping') {
        ws.send('pong');
      } else {
        // Handle subscription requests
        try {
          const data = JSON.parse(msg);
          if (data.type === 'subscribe') {
            // Store subscription preferences (could be used for filtering)
            ws.subscriptions = data.queues || [];
            ws.send(JSON.stringify({
              event: 'subscribed',
              data: { queues: ws.subscriptions },
              timestamp: new Date().toISOString()
            }));
          }
        } catch (error) {
          // Invalid JSON, ignore
        }
      }
    },
    
    close: (ws, code, message) => {
      const connectionId = ws.connectionId;
      connections.delete(connectionId);
      console.log(`WebSocket client disconnected: ${connectionId}`);
      broadcast('client.disconnected', { clientId: connectionId });
    }
  });
  
  // Subscribe to queue events - Updated for V2 structure
  eventManager.on('message.pushed', (data) => {
    broadcast('message.pushed', {
      queue: data.queue,
      partition: data.partition,
      transactionId: data.transactionId
    });
  });
  
  eventManager.on('message.processing', (data) => {
    broadcast('message.processing', {
      queue: data.queue,
      partition: data.partition,
      transactionId: data.transactionId,
      workerId: data.workerId
    });
  });
  
  eventManager.on('message.completed', (data) => {
    broadcast('message.completed', {
      transactionId: data.transactionId
    });
  });
  
  eventManager.on('message.failed', (data) => {
    broadcast('message.failed', {
      transactionId: data.transactionId,
      error: data.error
    });
  });
  
  eventManager.on('queue.created', (data) => {
    broadcast('queue.created', {
      queue: data.queue,
      partition: data.partition ?? 'Default'
    });
  });
  
  // Periodic queue depth updates - Updated for V2
  const updateQueueDepths = async (queueManager) => {
    try {
      const stats = await queueManager.getQueueStats();
      
      // Group by queue for aggregated depths
      const queueDepths = {};
      
      for (const stat of stats) {
        if (!queueDepths[stat.queue]) {
          queueDepths[stat.queue] = {
            queue: stat.queue,
            namespace: stat.namespace,
            task: stat.task,
            totalDepth: 0,
            totalProcessing: 0,
            partitions: {}
          };
        }
        
        // Add partition stats
        queueDepths[stat.queue].partitions[stat.partition] = {
          depth: stat.stats.pending,
          processing: stat.stats.processing,
          completed: stat.stats.completed,
          failed: stat.stats.failed
        };
        
        // Update totals
        queueDepths[stat.queue].totalDepth += stat.stats.pending;
        queueDepths[stat.queue].totalProcessing += stat.stats.processing;
      }
      
      // Broadcast aggregated queue depths
      for (const queueData of Object.values(queueDepths)) {
        broadcast('queue.depth', queueData);
      }
      
      // Also broadcast individual partition depths for detailed monitoring
      for (const stat of stats) {
        broadcast('partition.depth', {
          queue: stat.queue,
          partition: stat.partition,
          depth: stat.stats.pending,
          processing: stat.stats.processing,
          completed: stat.stats.completed,
          failed: stat.stats.failed,
          total: stat.stats.total
        });
      }
      
    } catch (error) {
      console.error('Error updating queue depths:', error);
    }
  };
  
  // Send system stats periodically
  const sendSystemStats = async (pool) => {
    try {
      const result = await pool.query(`
        SELECT
          (SELECT COUNT(*) FROM queen.messages WHERE status = 'pending') as pending,
          (SELECT COUNT(*) FROM queen.messages WHERE status = 'processing') as processing,
          (SELECT COUNT(*) FROM queen.messages WHERE created_at > NOW() - INTERVAL '1 minute') as recent_created,
          (SELECT COUNT(*) FROM queen.messages WHERE completed_at > NOW() - INTERVAL '1 minute') as recent_completed
      `);
      
      const stats = result.rows[0];
      
      broadcast('system.stats', {
        pending: parseInt(stats.pending),
        processing: parseInt(stats.processing),
        recentCreated: parseInt(stats.recent_created),
        recentCompleted: parseInt(stats.recent_completed),
        connections: connections.size
      });
    } catch (error) {
      console.error('Error sending system stats:', error);
    }
  };
  
  return {
    broadcast,
    updateQueueDepths,
    sendSystemStats,
    getConnectionCount: () => connections.size,
    getConnections: () => Array.from(connections.keys())
  };
};