import { generateUUID } from '../utils/uuid.js';
import { withTransaction } from '../database/connection.js';
import { PoolManager } from '../database/poolManager.js';
import * as encryption from '../services/encryptionService.js';
import { evictOnPop } from '../services/evictionService.js';
import { log, LogTypes } from '../utils/logger.js';
import config from '../config.js';

export const createOptimizedQueueManager = (pool, resourceCache, eventManager) => {
  
  // Batch size for database operations
  const BATCH_INSERT_SIZE = config.QUEUE.BATCH_INSERT_SIZE;
  
  // Create pool manager for better connection handling
  const poolManager = new PoolManager({
    max: pool.options?.max || config.DATABASE.POOL_SIZE
  });
  
  // Ensure queue and partition exist (with caching)
  const ensureResources = async (client, queueName, partitionName = 'Default', namespace = null, task = null) => {
    // Check cache first, but skip cache if we're updating namespace/task
    const cacheKey = `${queueName}:${partitionName}`;
    const cached = resourceCache.checkResource(queueName, partitionName);
    if (cached && namespace === null && task === null) return cached;
    
    // Insert or get queue - handle null values properly
    const queueResult = await client.query(
      `INSERT INTO queen.queues (name, namespace, task) VALUES ($1, $2, $3) 
       ON CONFLICT (name) DO UPDATE SET 
         namespace = CASE WHEN EXCLUDED.namespace IS NOT NULL THEN EXCLUDED.namespace ELSE queen.queues.namespace END,
         task = CASE WHEN EXCLUDED.task IS NOT NULL THEN EXCLUDED.task ELSE queen.queues.task END
       RETURNING id, name, namespace, task, encryption_enabled, max_wait_time_seconds,
                lease_time, retry_limit, retry_delay, max_size, ttl, dead_letter_queue,
                dlq_after_max_retries, delayed_processing, window_buffer, retention_seconds,
                completed_retention_seconds, retention_enabled, priority`,
      [queueName, namespace || null, task || null]
    );
    const queue = queueResult.rows[0];
    const queueId = queue.id;
    
    // Insert or get partition (no more options column)
    const partitionResult = await client.query(
      `INSERT INTO queen.partitions (queue_id, name) VALUES ($1, $2) 
       ON CONFLICT (queue_id, name) DO UPDATE SET name = EXCLUDED.name 
       RETURNING id`,
      [queueId, partitionName]
    );
    
    const result = {
      queueId,
      queueName: queue.name,
      partitionId: partitionResult.rows[0].id,
      // All configuration now comes from queue level
      queueConfig: {
        leaseTime: queue.lease_time,
        retryLimit: queue.retry_limit,
        retryDelay: queue.retry_delay,
        maxSize: queue.max_size,
        ttl: queue.ttl,
        deadLetterQueue: queue.dead_letter_queue,
        dlqAfterMaxRetries: queue.dlq_after_max_retries,
        delayedProcessing: queue.delayed_processing,
        windowBuffer: queue.window_buffer,
        retentionSeconds: queue.retention_seconds,
        completedRetentionSeconds: queue.completed_retention_seconds,
        retentionEnabled: queue.retention_enabled,
        priority: queue.priority
      },
      encryptionEnabled: queue.encryption_enabled,
      maxWaitTimeSeconds: queue.max_wait_time_seconds
    };
    
    // Cache the result
    resourceCache.cacheResource(queueName, partitionName, result);
    
    return result;
  };
  
  // Optimized batch insert for high throughput
  const pushMessagesBatch = async (items) => {
    // Group items by queue and partition for efficient resource lookup
    const partitionGroups = {};
    for (const item of items) {
      const partition = item.partition ?? 'Default';
      const key = `${item.queue}:${partition}`;
      if (!partitionGroups[key]) {
        partitionGroups[key] = [];
      }
      partitionGroups[key].push(item);
    }
    
    const allResults = [];
    
    // Process each partition group
    for (const [partitionKey, partitionItems] of Object.entries(partitionGroups)) {
      const [queueName, partitionName] = partitionKey.split(':');
      
      // Use pool manager for better connection handling
      const results = await poolManager.withClient(async (client) => {
        // Ensure resources exist (cached after first call)
        const resources = await ensureResources(client, queueName, partitionName);
        const batchResults = [];
        
        // Process in batches to avoid overwhelming the database
        for (let i = 0; i < partitionItems.length; i += BATCH_INSERT_SIZE) {
          const batch = partitionItems.slice(i, i + BATCH_INSERT_SIZE);
          
          // Generate transaction IDs
          const transactionIds = batch.map(item => item.transactionId || generateUUID());
          
          // Check for duplicates (quick check, non-blocking)
          const existingCheck = await client.query(
            `SELECT transaction_id FROM queen.messages 
             WHERE transaction_id = ANY($1::uuid[])`,
            [transactionIds]
          );
          
          const existingIds = new Set(existingCheck.rows.map(r => r.transaction_id));
          
          // Prepare batch insert data
          const newItems = [];
          const duplicates = [];
          
          batch.forEach((item, index) => {
            const txId = transactionIds[index];
            if (existingIds.has(txId)) {
              duplicates.push({
                transactionId: txId,
                status: 'duplicate'
              });
            } else {
              newItems.push({
                ...item,
                transactionId: txId,
                partitionId: resources.partitionId
              });
            }
          });
          
          // Perform batch insert if there are new items
          if (newItems.length > 0) {
            // Build VALUES clause for batch insert
            const values = [];
            const params = [];
            let paramIndex = 1;
            
            for (const item of newItems) {
              // Encrypt payload if enabled
              let finalPayload = item.payload;
              let isEncrypted = false;
              
              if (resources.encryptionEnabled && encryption.isEncryptionEnabled()) {
                try {
                  finalPayload = await encryption.encryptPayload(item.payload);
                  isEncrypted = true;
                } catch (error) {
                  log('ERROR: Encryption failed:', error);
                  // Continue with unencrypted payload
                }
              }
              
              values.push(`($${paramIndex}, $${paramIndex + 1}, $${paramIndex + 2}, $${paramIndex + 3}, 'pending')`);
              params.push(item.transactionId, item.partitionId, JSON.stringify(finalPayload), isEncrypted);
              paramIndex += 4;
            }
            
            const insertQuery = `
              INSERT INTO queen.messages (transaction_id, partition_id, payload, is_encrypted, status)
              VALUES ${values.join(', ')}
              ON CONFLICT (transaction_id) DO NOTHING
              RETURNING id, transaction_id
            `;
            
            const insertResult = await client.query(insertQuery, params);
            
          insertResult.rows.forEach(row => {
            batchResults.push({
              id: row.id,
              transactionId: row.transaction_id,
              status: 'queued'
            });
          });
          
          // Log successful push operations
          if (insertResult.rows.length > 0) {
            log(`${LogTypes.PUSH} | Queue: ${queueName} | Partition: ${partitionName} | Messages: ${insertResult.rows.length} | TransactionIds: [${insertResult.rows.map(r => r.transaction_id).join(', ')}]`);
          }
            
            // Add any items that weren't inserted due to conflict
            const insertedTxIds = new Set(insertResult.rows.map(r => r.transaction_id));
            newItems.forEach(item => {
              if (!insertedTxIds.has(item.transactionId)) {
                batchResults.push({
                  transactionId: item.transactionId,
                  status: 'duplicate'
                });
              }
            });
          }
          
          // Add duplicates to results
          batchResults.push(...duplicates);
        }
        
        // Notify event manager about new messages
        const queuePath = `${queueName}/${partitionName}`;
        eventManager.notifyMessageAvailable(queuePath);
        
        return batchResults;
      });
      
      allResults.push(...results);
    }
    
    return allResults;
  };
  
  // Original push method for backward compatibility
  const pushMessages = async (items) => {
    // Use batch method for better performance
    return pushMessagesBatch(items);
  };
  
  // Pop messages from queue with optional partition
  const popMessages = async (scope, options = {}) => {
    const { queue, partition } = scope;
    const { wait = false, timeout = config.QUEUE.DEFAULT_TIMEOUT, batch = config.QUEUE.DEFAULT_BATCH_SIZE } = options;
    
    return withTransaction(pool, async (client) => {
      // First, evict any expired messages
      await evictOnPop(client, queue);
      
      let result;
      
      if (partition) {
        // Specific partition: use a simpler, more efficient query with delayed processing and windowBuffer support
        result = await client.query(`
          WITH partition_info AS (
            SELECT p.id, p.name as partition_name,
                   q.name as queue_name, q.max_wait_time_seconds,
                   q.delayed_processing, q.window_buffer, q.priority,
                   q.lease_time, q.retry_limit, q.retry_delay, q.max_size,
                   q.ttl, q.dead_letter_queue, q.dlq_after_max_retries,
                   q.retention_seconds, q.completed_retention_seconds, q.retention_enabled
            FROM queen.partitions p
            JOIN queen.queues q ON p.queue_id = q.id
            WHERE q.name = $1 AND p.name = $2
            LIMIT 1
          )
          SELECT m.*, m.is_encrypted, pi.partition_name, pi.queue_name, pi.priority,
                 pi.lease_time, pi.retry_limit, pi.retry_delay, pi.max_size, pi.ttl,
                 pi.dead_letter_queue, pi.dlq_after_max_retries, pi.delayed_processing,
                 pi.window_buffer, pi.retention_seconds, pi.completed_retention_seconds,
                 pi.retention_enabled
          FROM queen.messages m
          JOIN partition_info pi ON m.partition_id = pi.id
          WHERE m.status = 'pending'
            AND m.created_at <= NOW() - INTERVAL '1 second' * pi.delayed_processing
            AND (pi.max_wait_time_seconds = 0 OR 
                 m.created_at > NOW() - INTERVAL '1 second' * pi.max_wait_time_seconds)
            AND (pi.window_buffer = 0 OR NOT EXISTS (
              SELECT 1 FROM queen.messages m2 
              WHERE m2.partition_id = pi.id 
                AND m2.status = 'pending'
                AND m2.created_at > NOW() - INTERVAL '1 second' * pi.window_buffer
            ))
          ORDER BY m.created_at ASC
          LIMIT $3
          FOR UPDATE OF m SKIP LOCKED
        `, [queue, partition, batch]);
      } else {
        // Queue level: get from any partition in the queue with priority ordering, then FIFO within partition
        result = await client.query(`
          SELECT m.*, m.is_encrypted, p.name as partition_name, q.name as queue_name,
                 q.priority, q.max_wait_time_seconds, q.lease_time, q.retry_limit,
                 q.retry_delay, q.max_size, q.ttl, q.dead_letter_queue,
                 q.dlq_after_max_retries, q.delayed_processing, q.window_buffer,
                 q.retention_seconds, q.completed_retention_seconds, q.retention_enabled
          FROM queen.messages m
          JOIN queen.partitions p ON m.partition_id = p.id
          JOIN queen.queues q ON p.queue_id = q.id
          WHERE q.name = $1
            AND m.status = 'pending'
            AND m.created_at <= NOW() - INTERVAL '1 second' * q.delayed_processing
            AND (q.max_wait_time_seconds = 0 OR 
                 m.created_at > NOW() - INTERVAL '1 second' * q.max_wait_time_seconds)
            AND (q.window_buffer = 0 OR NOT EXISTS (
              SELECT 1 FROM queen.messages m2 
              WHERE m2.partition_id = p.id 
                AND m2.status = 'pending'
                AND m2.created_at > NOW() - INTERVAL '1 second' * q.window_buffer
            ))
          ORDER BY q.priority DESC, m.created_at ASC
          LIMIT $2
          FOR UPDATE OF m SKIP LOCKED
        `, [queue, batch]);
      }
      
      if (result.rows.length === 0 && wait) {
        // Release the transaction and wait for messages
        await client.query('COMMIT');
        
        const queuePath = partition ? `${queue}/${partition}` : queue;
        const notification = await eventManager.waitForMessage(queuePath, timeout);
        
        if (!notification) {
          return { messages: [] }; // Timeout
        }
        
        // Try again after notification
        await client.query('BEGIN');
        if (partition) {
          result = await client.query(`
            WITH partition_info AS (
              SELECT p.id, p.name as partition_name,
                     q.name as queue_name, q.priority, q.lease_time
              FROM queen.partitions p
              JOIN queen.queues q ON p.queue_id = q.id
              WHERE q.name = $1 AND p.name = $2
              LIMIT 1
            )
            SELECT m.*, pi.partition_name, pi.queue_name, pi.priority, pi.lease_time
            FROM queen.messages m
            JOIN partition_info pi ON m.partition_id = pi.id
            WHERE m.status = 'pending'
            ORDER BY m.created_at ASC
            LIMIT $3
            FOR UPDATE OF m SKIP LOCKED
          `, [queue, partition, batch]);
        } else {
          result = await client.query(`
            SELECT m.*, p.name as partition_name, q.name as queue_name,
                   q.priority, q.lease_time
            FROM queen.messages m
            JOIN queen.partitions p ON m.partition_id = p.id
            JOIN queen.queues q ON p.queue_id = q.id
            WHERE q.name = $1
              AND m.status = 'pending'
            ORDER BY q.priority DESC, m.created_at ASC
            LIMIT $2
            FOR UPDATE OF m SKIP LOCKED
          `, [queue, batch]);
        }
      }
      
      if (result.rows.length === 0) {
        return { messages: [] };
      }
      
      // Update status to processing
      const messageIds = result.rows.map(r => r.id);
      // Use lease time from queue configuration
      const leaseTime = result.rows[0]?.lease_time || config.QUEUE.DEFAULT_LEASE_TIME;
      
      await client.query(
        `UPDATE queen.messages 
         SET status = 'processing', 
             locked_at = NOW(),
             processing_at = NOW(),
             lease_expires_at = NOW() + INTERVAL '${leaseTime} seconds',
             retry_count = retry_count + 1
         WHERE id = ANY($1::uuid[])`,
        [messageIds]
      );
      
      // Format messages and decrypt if needed
      const messages = await Promise.all(
        result.rows.map(async (row) => {
          let decryptedPayload = row.payload;
          
          // Decrypt if encrypted
          if (row.is_encrypted && encryption.isEncryptionEnabled()) {
            try {
              decryptedPayload = await encryption.decryptPayload(row.payload);
            } catch (error) {
              log('ERROR: Decryption failed:', error);
              // Return encrypted payload if decryption fails
            }
          }
          
          return {
            id: row.id,
            transactionId: row.transaction_id,
            queue: row.queue_name,
            partition: row.partition_name,
            data: decryptedPayload,
            payload: decryptedPayload, // Also include as payload for compatibility
            retryCount: row.retry_count || 0,
            priority: row.priority || 0, // queue priority
            createdAt: row.created_at,
            lockedAt: row.locked_at,
            // Queue configuration is not sent to client, only used internally
          };
        })
      );
      
      // Log pop operations when messages are returned to client
      if (messages.length > 0) {
        const logInfo = messages.map(m => ({
          transactionId: m.transactionId,
          queue: m.queue,
          partition: m.partition,
          retryCount: m.retryCount
        }));
        log(`${LogTypes.POP} | Queue: ${scope.queue} | Partition: ${scope.partition || 'any'} | Count: ${messages.length} | Messages:`, logInfo);
      }
      
      return { messages };
    });
  };
  
  // Pop with filters (namespace, task)
  const popMessagesWithFilters = async (filters, options = {}) => {
    const { namespace, task } = filters;
    const { wait = false, timeout = config.QUEUE.DEFAULT_TIMEOUT, batch = config.QUEUE.DEFAULT_BATCH_SIZE } = options;
    
    return withTransaction(pool, async (client) => {
      let query = `
        SELECT m.*, m.is_encrypted, p.name as partition_name, q.name as queue_name,
               q.namespace, q.task, q.priority, q.lease_time, q.retry_limit,
               q.retry_delay, q.max_size, q.ttl, q.dead_letter_queue,
               q.dlq_after_max_retries, q.delayed_processing, q.window_buffer,
               q.retention_seconds, q.completed_retention_seconds, q.retention_enabled
        FROM queen.messages m
        JOIN queen.partitions p ON m.partition_id = p.id
        JOIN queen.queues q ON p.queue_id = q.id
        WHERE m.status = 'pending'
          AND m.created_at <= NOW() - INTERVAL '1 second' * q.delayed_processing
          AND (q.window_buffer = 0 OR NOT EXISTS (
            SELECT 1 FROM queen.messages m2 
            WHERE m2.partition_id = p.id 
              AND m2.status = 'pending'
              AND m2.created_at > NOW() - INTERVAL '1 second' * q.window_buffer
          ))
      `;
      
      const params = [];
      if (namespace) {
        params.push(namespace);
        query += ` AND q.namespace = $${params.length}`;
      }
      if (task) {
        params.push(task);
        query += ` AND q.task = $${params.length}`;
      }
      
      params.push(batch);
      // Priority-based ordering: highest queue priority first, then FIFO within partition
      query += ` ORDER BY q.priority DESC, m.created_at ASC LIMIT $${params.length} FOR UPDATE OF m SKIP LOCKED`;
      
      let result = await client.query(query, params);
      
      if (result.rows.length === 0 && wait) {
        // For filtered pops, we don't have a specific path to wait on
        // Just do simple timeout-based polling
        await client.query('COMMIT');
        await new Promise(resolve => setTimeout(resolve, Math.min(config.QUEUE.POLL_INTERVAL_FILTERED, timeout)));
        
        await client.query('BEGIN');
        result = await client.query(query, params);
      }
      
      if (result.rows.length === 0) {
        return { messages: [] };
      }
      
      // Update status to processing
      const messageIds = result.rows.map(r => r.id);
      // Use lease time from queue configuration
      const leaseTime = result.rows[0]?.lease_time || config.QUEUE.DEFAULT_LEASE_TIME;
      
      await client.query(
        `UPDATE queen.messages 
         SET status = 'processing',
             locked_at = NOW(),
             processing_at = NOW(),
             lease_expires_at = NOW() + INTERVAL '${leaseTime} seconds',
             retry_count = retry_count + 1
         WHERE id = ANY($1::uuid[])`,
        [messageIds]
      );
      
      // Format messages and decrypt if needed
      const messages = await Promise.all(
        result.rows.map(async (row) => {
          let decryptedPayload = row.payload;
          
          // Decrypt if encrypted
          if (row.is_encrypted && encryption.isEncryptionEnabled()) {
            try {
              decryptedPayload = await encryption.decryptPayload(row.payload);
            } catch (error) {
              log('ERROR: Decryption failed:', error);
              // Return encrypted payload if decryption fails
            }
          }
          
          return {
            id: row.id,
            transactionId: row.transaction_id,
            queue: row.queue_name,
            partition: row.partition_name,
            namespace: row.namespace,
            task: row.task,
            data: decryptedPayload,
            retryCount: row.retry_count || 0,
            priority: row.priority || 0, // queue priority
            createdAt: row.created_at,
            lockedAt: row.locked_at
          };
        })
      );
      
      // Log pop operations with filters
      if (messages.length > 0) {
        const logInfo = messages.map(m => ({
          transactionId: m.transactionId,
          queue: m.queue,
          partition: m.partition,
          retryCount: m.retryCount
        }));
        log(`${LogTypes.POP_FILTERED} | Namespace: ${filters.namespace || 'any'} | Task: ${filters.task || 'any'} | Count: ${messages.length} | Messages:`, logInfo);
      }
      
      return { messages };
    });
  };
  
  // Acknowledge message completion
  const acknowledgeMessage = async (transactionId, status = 'completed', error = null) => {
    const validStatuses = ['completed', 'failed'];
    if (!validStatuses.includes(status)) {
      throw new Error(`Invalid status: ${status}`);
    }
    
    return withTransaction(pool, async (client) => {
      const updateQuery = status === 'completed' 
        ? `UPDATE queen.messages 
           SET status = $1, 
               completed_at = NOW(),
               error_message = $2
           WHERE transaction_id = $3
           RETURNING id, partition_id, retry_count`
        : `UPDATE queen.messages 
           SET status = $1, 
               failed_at = NOW(),
               error_message = $2
           WHERE transaction_id = $3
           RETURNING id, partition_id, retry_count`;
           
      const result = await client.query(updateQuery, [status, error, transactionId]);
      
      if (result.rows.length === 0) {
        throw new Error(`Message not found: ${transactionId}`);
      }
      
      // Log acknowledge operation
      log(`${LogTypes.ACK} | TransactionId: ${transactionId} | Status: ${status} | Error: ${error || 'none'}`);
      
      // Handle retry logic for failed messages
      if (status === 'failed') {
        const message = await client.query(
          `SELECT m.*, q.retry_limit, q.dlq_after_max_retries 
           FROM queen.messages m
           JOIN queen.partitions p ON m.partition_id = p.id
           JOIN queen.queues q ON p.queue_id = q.id
           WHERE m.id = $1`,
          [result.rows[0].id]
        );
        
        const maxRetries = message.rows[0].retry_limit || config.QUEUE.DEFAULT_RETRY_LIMIT;
        const currentRetryCount = result.rows[0].retry_count;
        
        if (currentRetryCount < maxRetries) {
          // Reset to pending for retry
          await client.query(
            `UPDATE queen.messages 
             SET status = 'pending',
                 locked_at = NULL,
                 lease_expires_at = NULL,
                 error_message = NULL
             WHERE id = $1`,
            [result.rows[0].id]
          );
          
          log(`${LogTypes.RETRY_SCHEDULED} | TransactionId: ${transactionId} | RetryCount: ${currentRetryCount}/${maxRetries}`);
          return { status: 'retry_scheduled', retryCount: currentRetryCount };
        } else if (message.rows[0].dlq_after_max_retries) {
          // Move to DLQ
          await client.query(
            `UPDATE queen.messages 
             SET status = 'dead_letter'
             WHERE id = $1`,
            [result.rows[0].id]
          );
          
          log(`${LogTypes.MOVED_TO_DLQ} | TransactionId: ${transactionId} | MaxRetries: ${maxRetries} exceeded`);
          return { status: 'moved_to_dlq' };
        }
      }
      
      return { status, transaction_id: transactionId };
    });
  };
  
  // Batch acknowledge messages
  const acknowledgeMessages = async (acknowledgments) => {
    const results = [];
    
    // Group by status for efficient processing
    const grouped = {
      completed: [],
      failed: []
    };
    
    acknowledgments.forEach(ack => {
      if (grouped[ack.status]) {
        grouped[ack.status].push(ack);
      }
    });
    
    await withTransaction(pool, async (client) => {
      // Process completed messages
      if (grouped.completed.length > 0) {
        const ids = grouped.completed.map(a => a.transactionId);
        await client.query(
          `UPDATE queen.messages 
           SET status = 'completed',
               completed_at = NOW()
           WHERE transaction_id = ANY($1::uuid[])`,
          [ids]
        );
        
        // Log batch acknowledge operations
        log(`${LogTypes.ACK_BATCH} | Status: completed | Count: ${ids.length} | TransactionIds: [${ids.join(', ')}]`);
        
        grouped.completed.forEach(ack => {
          results.push({ transactionId: ack.transactionId, status: 'completed' });
        });
      }
      
      // Process failed messages (needs individual handling for retry logic)
      for (const ack of grouped.failed) {
        const result = await acknowledgeMessage(ack.transactionId, 'failed', ack.error);
        results.push({ transactionId: ack.transactionId, ...result });
      }
    });
    
    return results;
  };
  
  // Configure queue settings (queue-level only, partitions are just FIFO containers now)
  const configureQueue = async (queueName, options = {}, namespace = null, task = null) => {
    return withTransaction(pool, async (client) => {
      // First create/update the queue with namespace and task
      const queueResult = await client.query(
        `INSERT INTO queen.queues (name, namespace, task) VALUES ($1, $2, $3) 
         ON CONFLICT (name) DO UPDATE SET 
           namespace = CASE WHEN EXCLUDED.namespace IS NOT NULL THEN EXCLUDED.namespace ELSE queen.queues.namespace END,
           task = CASE WHEN EXCLUDED.task IS NOT NULL THEN EXCLUDED.task ELSE queen.queues.task END
         RETURNING id, name, namespace, task`,
        [queueName, namespace || null, task || null]
      );
      
      // Build update query for all configuration options
      const updates = [];
      const params = [];
      let paramIndex = 1;
      
      // Map options to database columns
      const optionMappings = {
        leaseTime: 'lease_time',
        retryLimit: 'retry_limit',
        retryDelay: 'retry_delay',
        maxSize: 'max_size',
        ttl: 'ttl',
        deadLetterQueue: 'dead_letter_queue',
        dlqAfterMaxRetries: 'dlq_after_max_retries',
        delayedProcessing: 'delayed_processing',
        windowBuffer: 'window_buffer',
        retentionSeconds: 'retention_seconds',
        completedRetentionSeconds: 'completed_retention_seconds',
        retentionEnabled: 'retention_enabled',
        priority: 'priority',
        encryptionEnabled: 'encryption_enabled',
        maxWaitTimeSeconds: 'max_wait_time_seconds'
      };
      
      // Process each option
      for (const [optionKey, columnName] of Object.entries(optionMappings)) {
        if (options[optionKey] !== undefined) {
          updates.push(`${columnName} = $${paramIndex}`);
          
          // Handle different data types
          if (typeof options[optionKey] === 'boolean') {
            params.push(!!options[optionKey]);
          } else if (typeof options[optionKey] === 'number' || !isNaN(options[optionKey])) {
            params.push(parseInt(options[optionKey]) || 0);
          } else {
            params.push(options[optionKey]);
          }
          
          paramIndex++;
        }
      }
      
      // Apply updates if any
      if (updates.length > 0) {
        params.push(queueName);
        const updateQuery = `UPDATE queen.queues SET ${updates.join(', ')} WHERE name = $${paramIndex}`;
        await client.query(updateQuery, params);
      }
      
      // Invalidate cache for all partitions of this queue
      resourceCache.invalidateQueue(queueName);
      
      return { 
        queue: queueName, 
        namespace: queueResult.rows[0].namespace, 
        task: queueResult.rows[0].task,
        options
      };
    });
  };
  
  // Get queue statistics
  const getQueueStats = async (filters = {}) => {
    const { queue, namespace, task } = filters;
    
    let query = `
      SELECT 
        q.name as queue,
        q.namespace,
        q.task,
        p.name as partition,
        COUNT(CASE WHEN m.status = 'pending' THEN 1 END) as pending,
        COUNT(CASE WHEN m.status = 'processing' THEN 1 END) as processing,
        COUNT(CASE WHEN m.status = 'completed' THEN 1 END) as completed,
        COUNT(CASE WHEN m.status = 'failed' THEN 1 END) as failed,
        COUNT(CASE WHEN m.status = 'dead_letter' THEN 1 END) as dead_letter,
        COUNT(*) as total
      FROM queen.queues q
      LEFT JOIN queen.partitions p ON p.queue_id = q.id
      LEFT JOIN queen.messages m ON m.partition_id = p.id
    `;
    
    const conditions = [];
    const params = [];
    
    if (queue) {
      conditions.push(`q.name = $${params.length + 1}`);
      params.push(queue);
    }
    if (namespace) {
      conditions.push(`q.namespace = $${params.length + 1}`);
      params.push(namespace);
    }
    if (task) {
      conditions.push(`q.task = $${params.length + 1}`);
      params.push(task);
    }
    
    if (conditions.length > 0) {
      query += ` WHERE ${conditions.join(' AND ')}`;
    }
    
    query += ` GROUP BY q.name, q.namespace, q.task, p.name ORDER BY q.name, p.name`;
    
    const result = await pool.query(query, params);
    
    // Transform the flat structure into the expected nested format
    return result.rows.map(row => ({
      queue: row.queue,
      namespace: row.namespace,
      task: row.task,
      partition: row.partition,
      stats: {
        pending: parseInt(row.pending) || 0,
        processing: parseInt(row.processing) || 0,
        completed: parseInt(row.completed) || 0,
        failed: parseInt(row.failed) || 0,
        deadLetter: parseInt(row.dead_letter) || 0,
        total: parseInt(row.total) || 0
      }
    }));
  };
  
  // Reclaim expired leases
  const reclaimExpiredLeases = async () => {
    const result = await pool.query(
      `UPDATE queen.messages 
       SET status = 'pending',
           locked_at = NULL,
           lease_expires_at = NULL,
           worker_id = NULL
       WHERE status = 'processing' 
         AND lease_expires_at < NOW()
       RETURNING id, transaction_id`
    );
    
    if (result.rowCount > 0) {
      const transactionIds = result.rows.map(r => r.transaction_id);
      log(`${LogTypes.LEASE_EXPIRED} | Count: ${result.rowCount} | TransactionIds: [${transactionIds.join(', ')}]`);
    }
    
    return result.rowCount;
  };

  // Get queue lag statistics
  const getQueueLag = async (filters = {}) => {
    const { queue, namespace, task } = filters;
    
    let query = `
      WITH queue_stats AS (
        SELECT 
          q.name as queue,
          q.namespace,
          q.task,
          p.name as partition,
          COUNT(CASE WHEN m.status = 'pending' THEN 1 END) as pending_count,
          COUNT(CASE WHEN m.status = 'processing' THEN 1 END) as processing_count
        FROM queen.queues q
        LEFT JOIN queen.partitions p ON p.queue_id = q.id
        LEFT JOIN queen.messages m ON m.partition_id = p.id
        WHERE 1=1
    `;
    
    const conditions = [];
    const params = [];
    
    if (queue) {
      conditions.push(`q.name = $${params.length + 1}`);
      params.push(queue);
    }
    if (namespace) {
      conditions.push(`q.namespace = $${params.length + 1}`);
      params.push(namespace);
    }
    if (task) {
      conditions.push(`q.task = $${params.length + 1}`);
      params.push(task);
    }
    
    if (conditions.length > 0) {
      query += ` AND ${conditions.join(' AND ')}`;
    }
    
    query += `
        GROUP BY q.name, q.namespace, q.task, p.name
      ),
      processing_times AS (
        SELECT 
          q.name as queue,
          p.name as partition,
          AVG(EXTRACT(EPOCH FROM (m.completed_at - m.created_at))) as avg_processing_time_seconds,
          COUNT(*) as completed_messages,
          PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY EXTRACT(EPOCH FROM (m.completed_at - m.created_at))) as median_processing_time_seconds,
          PERCENTILE_CONT(0.95) WITHIN GROUP (ORDER BY EXTRACT(EPOCH FROM (m.completed_at - m.created_at))) as p95_processing_time_seconds
        FROM queen.queues q
        LEFT JOIN queen.partitions p ON p.queue_id = q.id
        LEFT JOIN queen.messages m ON m.partition_id = p.id
        WHERE m.status = 'completed' 
          AND m.completed_at IS NOT NULL 
          AND m.created_at IS NOT NULL
          AND m.completed_at >= NOW() - INTERVAL '24 hours'  -- Only consider recent completions
    `;
    
    if (conditions.length > 0) {
      query += ` AND ${conditions.join(' AND ')}`;
    }
    
    query += `
        GROUP BY q.name, p.name
        HAVING COUNT(*) >= 5  -- Only include queues with sufficient data
      )
      SELECT 
        qs.queue,
        qs.namespace,
        qs.task,
        qs.partition,
        qs.pending_count,
        qs.processing_count,
        COALESCE(pt.avg_processing_time_seconds, 0) as avg_processing_time_seconds,
        COALESCE(pt.median_processing_time_seconds, 0) as median_processing_time_seconds,
        COALESCE(pt.p95_processing_time_seconds, 0) as p95_processing_time_seconds,
        COALESCE(pt.completed_messages, 0) as completed_messages,
        -- Calculate lag using average processing time
        CASE 
          WHEN pt.avg_processing_time_seconds > 0 THEN 
            (qs.pending_count + qs.processing_count) * pt.avg_processing_time_seconds
          ELSE 0 
        END as estimated_lag_seconds,
        -- Calculate lag using median processing time (more robust to outliers)
        CASE 
          WHEN pt.median_processing_time_seconds > 0 THEN 
            (qs.pending_count + qs.processing_count) * pt.median_processing_time_seconds
          ELSE 0 
        END as median_lag_seconds,
        -- Calculate worst-case lag using 95th percentile
        CASE 
          WHEN pt.p95_processing_time_seconds > 0 THEN 
            (qs.pending_count + qs.processing_count) * pt.p95_processing_time_seconds
          ELSE 0 
        END as p95_lag_seconds
      FROM queue_stats qs
      LEFT JOIN processing_times pt ON qs.queue = pt.queue AND qs.partition = pt.partition
      ORDER BY qs.queue, qs.partition
    `;
    
    const result = await pool.query(query, params);
    
    // Transform the results into a more usable format
    return result.rows.map(row => ({
      queue: row.queue,
      namespace: row.namespace,
      task: row.task,
      partition: row.partition,
      stats: {
        pendingCount: parseInt(row.pending_count) || 0,
        processingCount: parseInt(row.processing_count) || 0,
        totalBacklog: (parseInt(row.pending_count) || 0) + (parseInt(row.processing_count) || 0),
        completedMessages: parseInt(row.completed_messages) || 0,
        avgProcessingTimeSeconds: parseFloat(row.avg_processing_time_seconds) || 0,
        medianProcessingTimeSeconds: parseFloat(row.median_processing_time_seconds) || 0,
        p95ProcessingTimeSeconds: parseFloat(row.p95_processing_time_seconds) || 0,
        estimatedLagSeconds: parseFloat(row.estimated_lag_seconds) || 0,
        medianLagSeconds: parseFloat(row.median_lag_seconds) || 0,
        p95LagSeconds: parseFloat(row.p95_lag_seconds) || 0,
        // Human-readable lag estimates
        estimatedLag: formatDuration(parseFloat(row.estimated_lag_seconds) || 0),
        medianLag: formatDuration(parseFloat(row.median_lag_seconds) || 0),
        p95Lag: formatDuration(parseFloat(row.p95_lag_seconds) || 0),
        // Processing time in human-readable format
        avgProcessingTime: formatDuration(parseFloat(row.avg_processing_time_seconds) || 0),
        medianProcessingTime: formatDuration(parseFloat(row.median_processing_time_seconds) || 0),
        p95ProcessingTime: formatDuration(parseFloat(row.p95_processing_time_seconds) || 0)
      }
    }));
  };

  // Helper function to format duration in human-readable format
  const formatDuration = (seconds) => {
    if (seconds === 0) return '0s';
    if (seconds < 60) return `${seconds.toFixed(1)}s`;
    if (seconds < 3600) return `${Math.floor(seconds / 60)}m ${Math.floor(seconds % 60)}s`;
    if (seconds < 86400) return `${Math.floor(seconds / 3600)}h ${Math.floor((seconds % 3600) / 60)}m`;
    return `${Math.floor(seconds / 86400)}d ${Math.floor((seconds % 86400) / 3600)}h`;
  };
  
  return {
    pushMessages,
    pushMessagesBatch,
    popMessages,
    popMessagesWithFilters,
    acknowledgeMessage,
    acknowledgeMessages,
    configureQueue,
    getQueueStats,
    getQueueLag,
    reclaimExpiredLeases
  };
};