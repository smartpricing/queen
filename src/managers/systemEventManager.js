import { generateUUID } from '../utils/uuid.js';

export const SYSTEM_QUEUE = '__system_events__';

export const EventTypes = {
  // Queue events
  QUEUE_CREATED: 'queue.created',
  QUEUE_UPDATED: 'queue.updated',
  QUEUE_DELETED: 'queue.deleted',
  
  // Partition events
  PARTITION_CREATED: 'partition.created',
  PARTITION_DELETED: 'partition.deleted',
  
  // Future events
  CONSUMER_GROUP_CREATED: 'consumer_group.created',
  CONSUMER_GROUP_UPDATED: 'consumer_group.updated'
};

export class SystemEventManager {
  constructor(pool, serverInstanceId) {
    this.pool = pool;
    this.serverInstanceId = serverInstanceId;
    this.handlers = new Map();
    this.eventQueue = [];
    this.batchTimer = null;
  }
  
  // Register event handlers
  on(eventType, handler) {
    if (!this.handlers.has(eventType)) {
      this.handlers.set(eventType, []);
    }
    this.handlers.get(eventType).push(handler);
  }
  
  // Emit event locally and to system queue
  async emit(eventType, data) {
    const event = {
      id: generateUUID(),
      eventType,
      ...data,
      timestamp: Date.now(),
      sourceServer: this.serverInstanceId,
      version: 1
    };
    
    // Handle locally first (immediate consistency for originating server)
    await this.handleEventLocally(event);
    
    // Queue for batch publishing
    this.eventQueue.push(event);
    this.scheduleBatch();
  }
  
  // Process incoming event from system queue
  async processSystemEvent(event) {
    // Skip own events (already processed locally)
    if (event.sourceServer === this.serverInstanceId) {
      return;
    }
    
    await this.handleEventLocally(event);
  }
  
  async handleEventLocally(event) {
    const handlers = this.handlers.get(event.eventType) || [];
    for (const handler of handlers) {
      try {
        await handler(event);
      } catch (error) {
        console.error(`Handler failed for ${event.eventType}:`, error);
      }
    }
  }
  
  scheduleBatch() {
    if (this.batchTimer) return;
    
    this.batchTimer = setTimeout(() => {
      this.publishBatch();
    }, 10); // 10ms batching
  }
  
  async publishBatch() {
    if (this.eventQueue.length === 0) return;
    
    const events = [...this.eventQueue];
    this.eventQueue = [];
    this.batchTimer = null;
    
    try {
      // Direct database insert to avoid circular dependency
      const client = await this.pool.connect();
      try {
        for (const event of events) {
          await this.insertSystemEvent(client, event);
        }
      } finally {
        client.release();
      }
    } catch (error) {
      console.error('Failed to publish system events:', error);
    }
  }
  
  async insertSystemEvent(client, event) {
    // Direct insert bypassing queue manager to avoid circular dependency
    await client.query(`
      INSERT INTO queen.messages (
        id, transaction_id, partition_id, payload, 
        created_at, trace_id
      )
      SELECT 
        $1, $2, p.id, $3, NOW(), $4
      FROM queen.partitions p
      JOIN queen.queues q ON p.queue_id = q.id
      WHERE q.name = $5 AND p.name = 'Default'
    `, [
      generateUUID(),
      generateUUID(),
      JSON.stringify(event),
      event.id,
      SYSTEM_QUEUE
    ]);
  }
}
