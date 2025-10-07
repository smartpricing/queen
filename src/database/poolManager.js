/**
 * Connection Pool Manager for High Concurrency
 * 
 * This module provides optimized connection pooling for high-throughput scenarios
 */

import pg from 'pg';

const { Pool } = pg;

export class PoolManager {
  constructor(config) {
    this.config = {
      user: process.env.PG_USER || 'postgres',
      host: process.env.PG_HOST || 'localhost',
      database: process.env.PG_DB || 'postgres',
      password: process.env.PG_PASSWORD || 'postgres',
      port: process.env.PG_PORT || 5432,
      max: parseInt(process.env.DB_POOL_SIZE) || 20,
      idleTimeoutMillis: parseInt(process.env.DB_IDLE_TIMEOUT) || 30000,
      connectionTimeoutMillis: parseInt(process.env.DB_CONNECTION_TIMEOUT) || 2000,
      ...config
    };
    
    this.pool = new Pool(this.config);
    this.waitingQueue = [];
    this.activeConnections = 0;
    this.maxRetries = 3;
    
    // Monitor pool events
    this.pool.on('connect', () => {
      this.activeConnections++;
    });
    
    this.pool.on('remove', () => {
      this.activeConnections--;
    });
    
    this.pool.on('error', (err) => {
      console.error('Unexpected pool error:', err);
    });
  }
  
  /**
   * Execute a query with automatic retry and connection management
   */
  async query(text, params, retries = this.maxRetries) {
    for (let attempt = 0; attempt < retries; attempt++) {
      try {
        return await this.pool.query(text, params);
      } catch (error) {
        if (error.message.includes('timeout exceeded when trying to connect')) {
          // Connection timeout - retry with exponential backoff
          if (attempt < retries - 1) {
            const delay = Math.min(100 * Math.pow(2, attempt), 1000);
            await new Promise(resolve => setTimeout(resolve, delay));
            continue;
          }
        }
        throw error;
      }
    }
  }
  
  /**
   * Get a client with retry logic
   */
  async getClient(retries = this.maxRetries) {
    for (let attempt = 0; attempt < retries; attempt++) {
      try {
        const client = await this.pool.connect();
        
        // Wrap release to ensure proper cleanup
        const originalRelease = client.release.bind(client);
        client.release = (err) => {
          originalRelease(err);
          this.processWaitingQueue();
        };
        
        return client;
      } catch (error) {
        if (error.message.includes('timeout exceeded when trying to connect')) {
          if (attempt < retries - 1) {
            const delay = Math.min(100 * Math.pow(2, attempt), 1000);
            await new Promise(resolve => setTimeout(resolve, delay));
            continue;
          }
        }
        throw error;
      }
    }
  }
  
  /**
   * Execute a function with a client, ensuring proper cleanup
   */
  async withClient(fn) {
    const client = await this.getClient();
    try {
      return await fn(client);
    } finally {
      client.release();
    }
  }
  
  /**
   * Execute multiple queries in parallel with connection management
   */
  async parallelQueries(queries) {
    const promises = queries.map(({ text, params }) => 
      this.query(text, params)
    );
    return Promise.all(promises);
  }
  
  /**
   * Batch execute with optimal connection usage
   */
  async batchExecute(items, batchSize, executeFn) {
    const results = [];
    const batches = [];
    
    // Split items into batches
    for (let i = 0; i < items.length; i += batchSize) {
      batches.push(items.slice(i, i + batchSize));
    }
    
    // Process batches with controlled concurrency
    const concurrency = Math.min(Math.floor(this.config.max / 2), 10);
    
    for (let i = 0; i < batches.length; i += concurrency) {
      const batchPromises = batches
        .slice(i, i + concurrency)
        .map(batch => this.withClient(client => executeFn(client, batch)));
      
      const batchResults = await Promise.all(batchPromises);
      results.push(...batchResults.flat());
    }
    
    return results;
  }
  
  /**
   * Process any waiting queue operations
   */
  processWaitingQueue() {
    if (this.waitingQueue.length > 0 && this.pool.idleCount > 0) {
      const { resolve } = this.waitingQueue.shift();
      this.getClient().then(resolve).catch(() => {
        // Re-queue if failed
        this.waitingQueue.push({ resolve });
      });
    }
  }
  
  /**
   * Get pool statistics
   */
  getStats() {
    return {
      total: this.pool.totalCount,
      idle: this.pool.idleCount,
      waiting: this.pool.waitingCount,
      active: this.activeConnections,
      queueLength: this.waitingQueue.length,
      config: {
        max: this.config.max,
        connectionTimeout: this.config.connectionTimeoutMillis
      }
    };
  }
  
  /**
   * Gracefully close the pool
   */
  async close() {
    await this.pool.end();
  }
}

// Create a singleton instance
let poolManager;

export const getPoolManager = (config) => {
  if (!poolManager) {
    poolManager = new PoolManager(config);
  }
  return poolManager;
};

export default PoolManager;
