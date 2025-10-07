import { generateUUID } from '../utils/uuid.js';

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
    compression: 0,
    maxPayloadLength: 16 * 1024,
    idleTimeout: 60,
    
    open: (ws) => {
      const connectionId = generateUUID();
      connections.set(connectionId, ws);
      ws.connectionId = connectionId;
      
      console.log(`WebSocket client connected: ${connectionId}`);
      
      // Send welcome message
      ws.send(JSON.stringify({
        event: 'connected',
        data: { connectionId },
        timestamp: new Date().toISOString()
      }));
      
      broadcast('worker.connected', { workerId: connectionId });
    },
    
    message: (ws, message, isBinary) => {
      // Handle ping/pong
      const msg = Buffer.from(message).toString();
      if (msg === 'ping') {
        ws.send('pong');
      }
    },
    
    close: (ws, code, message) => {
      const connectionId = ws.connectionId;
      connections.delete(connectionId);
      console.log(`WebSocket client disconnected: ${connectionId}`);
      broadcast('worker.disconnected', { workerId: connectionId });
    }
  });
  
  // Subscribe to queue events
  eventManager.on('message.pushed', (data) => broadcast('message.pushed', data));
  eventManager.on('message.processing', (data) => broadcast('message.processing', data));
  eventManager.on('message.completed', (data) => broadcast('message.completed', data));
  eventManager.on('message.failed', (data) => broadcast('message.failed', data));
  eventManager.on('queue.created', (data) => broadcast('queue.created', data));
  eventManager.on('queue.depth', (data) => broadcast('queue.depth', data));
  
  // Periodic queue depth updates
  const updateQueueDepths = async (queueManager) => {
    try {
      const stats = await queueManager.getQueueStats();
      for (const queue of stats) {
        broadcast('queue.depth', {
          queue: queue.queue,
          depth: queue.stats.pending,
          processing: queue.stats.processing
        });
      }
    } catch (error) {
      console.error('Error updating queue depths:', error);
    }
  };
  
  return {
    broadcast,
    updateQueueDepths,
    getConnectionCount: () => connections.size
  };
};
