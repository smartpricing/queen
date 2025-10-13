import { generateUUID } from '../utils/uuid.js';
import { withTransaction } from '../database/connection.js';
import * as encryption from '../services/encryptionService.js';
import { log, LogTypes } from '../utils/logger.js';
import config from '../config.js';
import { EventTypes } from './systemEventManager.js';

export const createOptimizedQueueManager = (pool, resourceCache, eventManager, systemEventManager = null) => {
  
  // Batch size for database operations
  const BATCH_INSERT_SIZE = config.QUEUE.BATCH_INSERT_SIZE;
  
  // Performance metrics tracking for filtered pops
  const popMetrics = {
    partitionQueryTimes: [],
    leaseAttempts: 0,
    leaseSuccesses: 0,
    messageQueryTimes: [],
    totalPopTimes: [],
    batchSizes: [],
    lastLogTime: Date.now()
  };
  
  // Performance metrics for direct pops (queue/partition)
  const directPopMetrics = {
    leaseAcquisitionTimes: [],
    messageQueryTimes: [],
    statusUpdateTimes: [],
    leaseUpdateTimes: [],
    decryptionTimes: [],
    totalPopTimes: [],
    batchSizes: [],
    lastLogTime: Date.now()
  };
  
  // Log metrics periodically (every 5 seconds OR after 10 samples)
  const logMetrics = () => {
    const now = Date.now();
    const timeSinceLastLog = now - Math.max(popMetrics.lastLogTime, directPopMetrics.lastLogTime);
    const hasEnoughSamples = popMetrics.totalPopTimes.length >= 10 || directPopMetrics.totalPopTimes.length >= 10;
    
    // Only log if enough time passed OR we have enough samples
    if (timeSinceLastLog < 5000 && !hasEnoughSamples) return;
    
    if (popMetrics.totalPopTimes.length === 0 && directPopMetrics.totalPopTimes.length === 0) return; // No data yet
    
    const avg = arr => arr.length > 0 ? (arr.reduce((a,b) => a+b, 0) / arr.length).toFixed(2) : 0;
    const max = arr => arr.length > 0 ? Math.max(...arr).toFixed(2) : 0;
    const min = arr => arr.length > 0 ? Math.min(...arr).toFixed(2) : 0;
    
    if (popMetrics.totalPopTimes.length > 0) {
      log(`📊 PERF METRICS (filtered pop) | Samples: ${popMetrics.totalPopTimes.length}`);
      log(`  - Partition Query: avg=${avg(popMetrics.partitionQueryTimes)}ms max=${max(popMetrics.partitionQueryTimes)}ms`);
      log(`  - Message Query: avg=${avg(popMetrics.messageQueryTimes)}ms max=${max(popMetrics.messageQueryTimes)}ms`);
      log(`  - Total Pop: avg=${avg(popMetrics.totalPopTimes)}ms max=${max(popMetrics.totalPopTimes)}ms`);
      log(`  - Lease Success Rate: ${popMetrics.leaseSuccesses}/${popMetrics.leaseAttempts} (${(popMetrics.leaseSuccesses/popMetrics.leaseAttempts*100).toFixed(1)}%)`);
      log(`  - Avg Batch Size: ${avg(popMetrics.batchSizes)} messages`);
    }
    
    if (directPopMetrics.totalPopTimes.length > 0) {
      log(`📊 PERF METRICS (direct pop) | Samples: ${directPopMetrics.totalPopTimes.length}`);
      log(`  - Lease Acquisition: avg=${avg(directPopMetrics.leaseAcquisitionTimes)}ms max=${max(directPopMetrics.leaseAcquisitionTimes)}ms`);
      log(`  - Message Query: avg=${avg(directPopMetrics.messageQueryTimes)}ms max=${max(directPopMetrics.messageQueryTimes)}ms`);
      log(`  - Status Update: avg=${avg(directPopMetrics.statusUpdateTimes)}ms max=${max(directPopMetrics.statusUpdateTimes)}ms`);
      log(`  - Lease Update: avg=${avg(directPopMetrics.leaseUpdateTimes)}ms max=${max(directPopMetrics.leaseUpdateTimes)}ms`);
      log(`  - Decryption/Format: avg=${avg(directPopMetrics.decryptionTimes)}ms max=${max(directPopMetrics.decryptionTimes)}ms`);
      log(`  - Total Pop: avg=${avg(directPopMetrics.totalPopTimes)}ms max=${max(directPopMetrics.totalPopTimes)}ms`);
      log(`  - Avg Batch Size: ${avg(directPopMetrics.batchSizes)} messages`);
    }
    
    // Reset metrics after logging
    popMetrics.partitionQueryTimes = [];
    popMetrics.leaseAttempts = 0;
    popMetrics.leaseSuccesses = 0;
    popMetrics.messageQueryTimes = [];
    popMetrics.totalPopTimes = [];
    popMetrics.batchSizes = [];
    popMetrics.lastLogTime = now;
    
    directPopMetrics.leaseAcquisitionTimes = [];
    directPopMetrics.messageQueryTimes = [];
    directPopMetrics.statusUpdateTimes = [];
    directPopMetrics.leaseUpdateTimes = [];
    directPopMetrics.decryptionTimes = [];
    directPopMetrics.totalPopTimes = [];
    directPopMetrics.batchSizes = [];
    directPopMetrics.lastLogTime = now;
  };
  
  // Ensure queue exists and partition exist (with caching)
  // Queue must be created via configure endpoint, but partitions can be created on-demand
  const ensureResources = async (client, queueName, partitionName = 'Default', namespace = null, task = null) => {
    // CRITICAL: System queues bypass cache completely
    if (queueName.startsWith('__system_')) {
      // Direct database query for system queues
      const result = await client.query(`
        SELECT 
          q.id as queue_id,
          q.name as queue_name,
          p.id as partition_id,
          q.*
        FROM queen.queues q
        JOIN queen.partitions p ON p.queue_id = q.id
        WHERE q.name = $1 AND p.name = $2
      `, [queueName, partitionName]);
      
      if (result.rows.length === 0) {
        throw new Error(`System queue ${queueName} not found`);
      }
      
      const row = result.rows[0];
      return {
        queueId: row.queue_id,
        queueName: row.queue_name,
        partitionId: row.partition_id,
        queueConfig: {
          leaseTime: row.lease_time,
          retryLimit: row.retry_limit,
          retryDelay: row.retry_delay,
          maxSize: row.max_size,
          ttl: row.ttl,
          deadLetterQueue: row.dead_letter_queue,
          dlqAfterMaxRetries: row.dlq_after_max_retries,
          delayedProcessing: row.delayed_processing,
          windowBuffer: row.window_buffer,
          retentionSeconds: row.retention_seconds,
          completedRetentionSeconds: row.completed_retention_seconds,
          retentionEnabled: row.retention_enabled,
          priority: row.priority,
          maxQueueSize: row.max_queue_size
        },
        encryptionEnabled: false,  // System queues never encrypted
        maxWaitTimeSeconds: row.max_wait_time_seconds
      };
    }
    
    // Check cache first, but skip cache if we're updating namespace/task
    const cacheKey = `${queueName}:${partitionName}`;
    const cached = resourceCache.checkResource(queueName, partitionName);
    if (cached && namespace === null && task === null) return cached;
    
    // Get queue - don't create if it doesn't exist
    const queueResult = await client.query(
      `SELECT id, name, namespace, task, encryption_enabled, max_wait_time_seconds,
              lease_time, retry_limit, retry_delay, max_size, ttl, dead_letter_queue,
              dlq_after_max_retries, delayed_processing, window_buffer, retention_seconds,
              completed_retention_seconds, retention_enabled, priority, max_queue_size
       FROM queen.queues
       WHERE name = $1`,
      [queueName]
    );
    
    if (queueResult.rows.length === 0) {
      // Queue doesn't exist - throw error instead of creating
      const errorMsg = `Queue '${queueName}' does not exist. Please create it using the configure endpoint first.`;
      log(`${LogTypes.ERROR} | ${errorMsg}`);
      throw new Error(errorMsg);
    }
    
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
      namespace: queue.namespace,
      task: queue.task,
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
        priority: queue.priority,
        maxQueueSize: queue.max_queue_size
      },
      encryptionEnabled: queue.encryption_enabled,
      maxWaitTimeSeconds: queue.max_wait_time_seconds
    };
    
    // Cache the result
    resourceCache.cacheResource(queueName, partitionName, result);
    
    return result;
  };
  
  // Optimized batch insert for high throughput (simplified - no status)
  const pushMessagesBatch = async (items) => {
    const pushStartTime = Date.now();
    
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
      
      // Use transaction for better error handling with retry on stale cache
      let retryCount = 0;
      const maxRetries = 1;
      
      while (retryCount <= maxRetries) {
        try {
          const results = await withTransaction(pool, async (client) => {
            // Ensure resources exist (cached after first call)
            const resources = await ensureResources(client, queueName, partitionName);
            const { partitionId, encryptionEnabled, queueConfig } = resources;
        
        // Check queue capacity if max_queue_size is set
        if (queueConfig.maxQueueSize > 0) {
          const capacityCheck = await client.query(
            `SELECT COUNT(m.id) as current_depth
             FROM queen.messages m
             JOIN queen.partitions p ON m.partition_id = p.id
             JOIN queen.queues q ON p.queue_id = q.id
             LEFT JOIN queen.messages_status ms ON m.id = ms.message_id 
               AND ms.consumer_group = '__QUEUE_MODE__'
             WHERE q.name = $1
               AND (ms.status IS NULL OR ms.status IN ('pending', 'processing'))`,
            [queueName]
          );
          
          const currentDepth = parseInt(capacityCheck.rows[0].current_depth);
          const batchSize = partitionItems.length;
          if (currentDepth + batchSize > queueConfig.maxQueueSize) {
            throw new Error(`Queue '${queueName}' would exceed max capacity (${queueConfig.maxQueueSize}). Current: ${currentDepth}, Batch: ${batchSize}`);
          }
        }
        
        // Process items in batches
        const batchResults = [];
        
        for (let i = 0; i < partitionItems.length; i += BATCH_INSERT_SIZE) {
          const batch = partitionItems.slice(i, i + BATCH_INSERT_SIZE);
          
          // Prepare batch data
          const messageIds = [];
          const transactionIds = [];
          const traceIds = [];
          const payloads = [];
          const encryptedFlags = [];
          const duplicates = [];
          
          // ⚡ OPTIMIZATION #1: Batch duplicate check - single query for all transaction IDs
          const allTransactionIds = batch.map(item => item.transactionId || generateUUID());
          
          // Use more efficient LEFT JOIN approach instead of ANY() for better index usage
          const dupCheck = await client.query(
            `SELECT t.txn_id as transaction_id, m.id
             FROM UNNEST($1::varchar[]) AS t(txn_id)
             LEFT JOIN queen.messages m ON m.transaction_id = t.txn_id
             WHERE m.id IS NOT NULL`,
            [allTransactionIds]
          );
          
          // Create a map of existing transaction IDs for O(1) lookup
          const existingTxnIds = new Map(dupCheck.rows.map(row => [row.transaction_id, row.id]));
          
          // ⚡ OPTIMIZATION #2: Parallel encryption - encrypt all payloads concurrently
          const encryptionTasks = batch.map(async (item, idx) => {
            const transactionId = allTransactionIds[idx];
            
            // Check if this transaction ID already exists
            if (existingTxnIds.has(transactionId)) {
              return {
                isDuplicate: true,
                id: existingTxnIds.get(transactionId),
                transactionId
              };
            }
            
            // Handle encryption if enabled
            let payload = item.payload;
            let isEncrypted = false;
            
            if (encryptionEnabled && encryption.isEncryptionEnabled()) {
              try {
                payload = await encryption.encryptPayload(item.payload);
                isEncrypted = true;
              } catch (error) {
                log(`ERROR: Encryption failed for transaction ${transactionId}:`, error);
                // Continue with unencrypted payload
              }
            }
            
            return {
              isDuplicate: false,
              messageId: generateUUID(),
              transactionId,
              traceId: item.traceId || null,
              payload,
              isEncrypted
            };
          });
          
          // Wait for all encryption tasks to complete in parallel
          const encryptionResults = await Promise.all(encryptionTasks);
          
          // Process results
          for (const result of encryptionResults) {
            if (result.isDuplicate) {
              duplicates.push({
                id: result.id,
                transactionId: result.transactionId,
                status: 'duplicate'
              });
              continue;
            }
            
            // Handle large payloads and special cases
            let jsonPayload;
            try {
              // Handle null/undefined payloads - convert to JSON null string
              if (result.payload === null || result.payload === undefined) {
                jsonPayload = 'null';  // JSONB expects the string 'null' for null values
              } else {
                // Try to stringify the payload
                jsonPayload = JSON.stringify(result.payload);
              }
            } catch (error) {
              // If JSON.stringify fails (e.g., circular references, too large), 
              // store as a string representation
              log(`WARN: Failed to stringify payload for message ${result.messageId}:`, error.message);
              jsonPayload = JSON.stringify({ 
                error: 'Payload serialization failed', 
                type: typeof result.payload,
                message: error.message 
              });
            }
            
            messageIds.push(result.messageId);
            transactionIds.push(result.transactionId);
            traceIds.push(result.traceId);
            payloads.push(jsonPayload);
            encryptedFlags.push(result.isEncrypted);
          }
          
          // Batch insert messages with trace_id
          if (messageIds.length > 0) {
            const insertQuery = `
              INSERT INTO queen.messages (id, transaction_id, trace_id, partition_id, payload, is_encrypted)
              SELECT * FROM UNNEST($1::uuid[], $2::varchar[], $3::uuid[], $4::uuid[], $5::jsonb[], $6::boolean[])
              RETURNING id, transaction_id, trace_id
            `;
            
            let insertResult;
            try {
              insertResult = await client.query(insertQuery, [
                messageIds,
                transactionIds,
                traceIds,
                Array(messageIds.length).fill(partitionId),
                payloads,
                encryptedFlags
              ]);
            } catch (error) {
              // If foreign key constraint error, the cached partition is stale
              if (error.code === '23503' && error.constraint === 'messages_partition_id_fkey') {
                // Invalidate cache and throw a more helpful error
                resourceCache.invalidate(queueName, partitionName);
                throw new Error(`Stale cache detected for queue '${queueName}' partition '${partitionName}'. Please retry the operation.`);
              }
              throw error;
            }
            
            // Format results
            for (const row of insertResult.rows) {
              batchResults.push({
                id: row.id,
                transactionId: row.transaction_id,
                traceId: row.trace_id,
                status: 'queued'
              });
            }
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
          break; // Success, exit retry loop
        } catch (error) {
          // Check if it's a stale cache error
          if (error.message && error.message.includes('Stale cache detected') && retryCount < maxRetries) {
            retryCount++;
            // Cache already invalidated in the error handler, just retry
            continue;
          }
          throw error; // Re-throw if not a stale cache error or max retries exceeded
        }
      }
    }
    
    const duration = Date.now() - pushStartTime;
    log(`[PUSH] Pushed ${items.length} items → ${allResults.length} results in ${duration}ms (${(items.length / (duration / 1000)).toFixed(0)} msg/s)`);
    return allResults;
  };


  // Acknowledge messages (now per consumer group)
  const acknowledgeMessage = async (transactionId, status = 'completed', error = null, consumerGroup = null) => {
    // CURSOR-BASED: Convert single ACK to batch ACK to advance cursor properly
    const batchResult = await acknowledgeMessages([{ transactionId, status, error }], consumerGroup);
    return batchResult[0] || { status: 'not_found', transaction_id: transactionId };
    
    /* OLD single-message ACK code (deprecated - use batch ACK for cursor advancement)
    return withTransaction(pool, async (client) => {
      // PERFORMANCE OPTIMIZATION: Since we no longer create status on POP, we need to handle both cases:
      // 1. Status exists (message was popped before optimization, or retry)
      // 2. Status doesn't exist (message was just popped with new optimization)
      
      // Find the message and get queue config (don't require status to exist)
      const findQuery = consumerGroup 
        ? `SELECT ms.retry_count, ms.status, ms.message_id, m.partition_id, m.id as message_id,
                  q.retry_limit, q.dlq_after_max_retries
           FROM queen.messages m
           JOIN queen.partitions p ON m.partition_id = p.id
           JOIN queen.queues q ON p.queue_id = q.id
           LEFT JOIN queen.messages_status ms ON ms.message_id = m.id AND ms.consumer_group = $2
           WHERE m.transaction_id = $1`
        : `SELECT ms.retry_count, ms.status, ms.message_id, m.partition_id, m.id as message_id,
                  q.retry_limit, q.dlq_after_max_retries
           FROM queen.messages m
           JOIN queen.partitions p ON m.partition_id = p.id
           JOIN queen.queues q ON p.queue_id = q.id
           LEFT JOIN queen.messages_status ms ON ms.message_id = m.id AND ms.consumer_group = '__QUEUE_MODE__'
           WHERE m.transaction_id = $1`;
      
      const params = consumerGroup ? [transactionId, consumerGroup] : [transactionId];
      const result = await client.query(findQuery, params);
      
      if (result.rows.length === 0) {
        log(`WARN: Message not found for acknowledgment: ${transactionId}, consumerGroup: ${consumerGroup || 'QUEUE_MODE'}`);
        return { status: 'not_found', transaction_id: transactionId };
      }
      
      const messageStatus = result.rows[0];
      const currentRetryCount = messageStatus.retry_count || 0;
      log(`DEBUG: Acknowledging message transaction=${transactionId} status=${messageStatus.status || 'NEW'} retry_count=${currentRetryCount}`);
      
      if (status === 'completed') {
        // Mark as completed using UPSERT (handles both new and existing status records)
        const actualConsumerGroup = consumerGroup || '__QUEUE_MODE__';
        
        // Use UPSERT to create or update status
        const upsertQuery = `
          INSERT INTO queen.messages_status (message_id, consumer_group, status, completed_at, retry_count)
          VALUES ($1, $2, 'completed', NOW(), $3)
          ON CONFLICT (message_id, consumer_group) 
          DO UPDATE SET
            status = 'completed',
            completed_at = NOW()
        `;
        
        await client.query(upsertQuery, [messageStatus.message_id, actualConsumerGroup, currentRetryCount]);
        
        // HYBRID COUNTER OPTIMIZATION: Increment acked_count instead of manipulating JSONB
        // This is O(1) instead of O(n) for large batches
        const incrementQuery = `
          WITH message_info AS (
            SELECT m.partition_id
             FROM queen.messages m
            WHERE m.transaction_id = $1
          )
          UPDATE queen.partition_leases pl
          SET acked_count = acked_count + 1
          FROM message_info mi
          WHERE pl.partition_id = mi.partition_id
            AND pl.consumer_group = $2
            AND pl.released_at IS NULL
        `;
        
        await client.query(incrementQuery, [transactionId, actualConsumerGroup]);
        
        // Release partition lease if all messages in batch have been acknowledged
        // Simple integer comparison: O(1) instead of checking entire JSONB array
        const releaseQuery = `
          WITH message_info AS (
            SELECT m.partition_id
            FROM queen.messages m
            WHERE m.transaction_id = $1
          )
          UPDATE queen.partition_leases pl
          SET released_at = NOW(), 
              message_batch = NULL,
              batch_size = 0,
              acked_count = 0
          FROM message_info mi
          WHERE pl.partition_id = mi.partition_id
            AND pl.consumer_group = $2
            AND pl.released_at IS NULL
            AND pl.acked_count >= pl.batch_size
            AND pl.batch_size > 0
        `;
        
        await client.query(releaseQuery, [transactionId, actualConsumerGroup]);
        
        log(`${LogTypes.ACK} | TransactionId: ${transactionId} | Status: completed | ConsumerGroup: ${consumerGroup || 'QUEUE_MODE'}`);
      } else if (status === 'failed') {
        // Handle failure with retry logic
        const actualConsumerGroup = consumerGroup || '__QUEUE_MODE__';
        const nextRetryCount = currentRetryCount + 1;
        const retryLimit = messageStatus.retry_limit || config.QUEUE.DEFAULT_RETRY_LIMIT;
        const dlqEnabled = messageStatus.dlq_after_max_retries;
        
        log(`${LogTypes.ACK} | Retry check: current=${currentRetryCount}, next=${nextRetryCount}, limit=${retryLimit}, dlq=${dlqEnabled}`);
        
        if (nextRetryCount > retryLimit && dlqEnabled) {
          // Move to dead letter queue after exceeding retry limit using UPSERT
          const upsertQuery = `
            INSERT INTO queen.messages_status (message_id, consumer_group, status, failed_at, error_message, retry_count)
            VALUES ($1, $2, 'dead_letter', NOW(), $3, $4)
            ON CONFLICT (message_id, consumer_group) 
            DO UPDATE SET
              status = 'dead_letter',
              failed_at = NOW(),
              error_message = EXCLUDED.error_message
          `;
          
          await client.query(upsertQuery, [messageStatus.message_id, actualConsumerGroup, error, nextRetryCount]);
          
          log(`${LogTypes.ACK} | TransactionId: ${transactionId} | Status: dead_letter | ConsumerGroup: ${consumerGroup || 'QUEUE_MODE'} | Error: ${error}`);
          
          return { status: 'dead_letter', transaction_id: transactionId };
        } else {
          // Mark as failed and increment retry count using UPSERT
          const upsertQuery = `
            INSERT INTO queen.messages_status (message_id, consumer_group, status, failed_at, error_message, lease_expires_at, retry_count)
            VALUES ($1, $2, 'failed', NOW(), $3, NULL, $4)
            ON CONFLICT (message_id, consumer_group) 
            DO UPDATE SET
              status = 'failed',
                   failed_at = NOW(), 
              error_message = EXCLUDED.error_message,
                   lease_expires_at = NULL,
              retry_count = EXCLUDED.retry_count
          `;
          
          await client.query(upsertQuery, [messageStatus.message_id, actualConsumerGroup, error, nextRetryCount]);
          
          // HYBRID COUNTER OPTIMIZATION: Increment acked_count instead of manipulating JSONB
          const incrementQuery = `
            WITH message_info AS (
              SELECT m.partition_id
               FROM queen.messages m
              WHERE m.transaction_id = $1
            )
            UPDATE queen.partition_leases pl
            SET acked_count = acked_count + 1
            FROM message_info mi
            WHERE pl.partition_id = mi.partition_id
              AND pl.consumer_group = $2
              AND pl.released_at IS NULL
          `;
          
          await client.query(incrementQuery, [transactionId, actualConsumerGroup]);
          
          // Release partition lease if all messages acknowledged
          const releaseOnFailQuery = `
            WITH message_info AS (
              SELECT m.partition_id
              FROM queen.messages m
              WHERE m.transaction_id = $1
            )
            UPDATE queen.partition_leases pl
            SET released_at = NOW(), 
                message_batch = NULL,
                batch_size = 0,
                acked_count = 0
            FROM message_info mi
            WHERE pl.partition_id = mi.partition_id
              AND pl.consumer_group = $2
              AND pl.released_at IS NULL
              AND pl.acked_count >= pl.batch_size
              AND pl.batch_size > 0
          `;
          
          await client.query(releaseOnFailQuery, [transactionId, actualConsumerGroup]);
          
          log(`${LogTypes.ACK} | TransactionId: ${transactionId} | Status: failed (retry ${nextRetryCount}/${retryLimit}) | ConsumerGroup: ${consumerGroup || 'QUEUE_MODE'} | Error: ${error}`);
        }
      }
      
      return { status, transaction_id: transactionId };
    });
    */
  };
  
  // Batch acknowledge messages with cursor-based advancement
  const acknowledgeMessages = async (acknowledgments, consumerGroup = null) => {
    const results = [];
    const actualConsumerGroup = consumerGroup || '__QUEUE_MODE__';
    
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
    
    const totalMessages = acknowledgments.length;
    const failedCount = grouped.failed.length;
    const successCount = grouped.completed.length;
    
    // Determine if this is a TOTAL batch failure or partial failure
    // Only treat as "total batch failure" if it's a real batch (>1 message)
    // Single-message failures always go to DLQ
    const isTotalBatchFailure = failedCount === totalMessages && totalMessages > 1;
    
    await withTransaction(pool, async (client) => {
      // CURSOR-BASED APPROACH: Handle total batch failure vs partial failure differently
      
      if (isTotalBatchFailure) {
        // ═══════════════════════════════════════════════════════════════
        // TOTAL BATCH FAILURE: Don't advance cursor, allow retry
        // ═══════════════════════════════════════════════════════════════
        log(`[CURSOR] Total batch failure - cursor NOT advanced, same batch will retry on next POP`);
        log(`${LogTypes.ACK_BATCH} | Total batch failure (${failedCount} messages) - cursor NOT advanced, batch will retry | ConsumerGroup: ${consumerGroup || 'QUEUE_MODE'}`);
        
        // Just release the lease - next POP will get same batch
        const allIds = acknowledgments.map(a => a.transactionId);
        await client.query(`
          WITH affected_partitions AS (
            SELECT DISTINCT m.partition_id
            FROM queen.messages m
            WHERE m.transaction_id = ANY($1::varchar[])
          )
          UPDATE queen.partition_leases pl
          SET released_at = NOW(), 
              message_batch = NULL,
              batch_size = 0,
              acked_count = 0
          FROM affected_partitions ap
          WHERE pl.partition_id = ap.partition_id
            AND pl.consumer_group = $2
            AND pl.released_at IS NULL
        `, [allIds, actualConsumerGroup]);
        
        grouped.failed.forEach(ack => {
          results.push({ transactionId: ack.transactionId, status: 'failed_retry' });
        });
        
      } else {
        // ═══════════════════════════════════════════════════════════════
        // PARTIAL SUCCESS: Advance cursor, move failed to DLQ
        // ═══════════════════════════════════════════════════════════════
        
        // Get all messages in this batch (successful and failed) to find cursor position
        const allIds = acknowledgments.map(a => a.transactionId);
        const batchInfo = await client.query(`
               SELECT 
            m.id,
                 m.transaction_id,
            m.created_at,
            m.partition_id,
            p.name as partition_name
               FROM queen.messages m
          JOIN queen.partitions p ON p.id = m.partition_id
               WHERE m.transaction_id = ANY($1::varchar[])
          ORDER BY m.created_at DESC, m.id DESC
          LIMIT 1
        `, [allIds]);
        
        if (batchInfo.rows.length === 0) {
          throw new Error('No messages found for acknowledgment');
        }
        
        const lastMessage = batchInfo.rows[0];
        const partitionId = lastMessage.partition_id;
        const partitionName = lastMessage.partition_name;
        
        // Advance cursor to last message in batch (regardless of success/failure)
        const cursorUpdateResult = await client.query(`
          INSERT INTO queen.partition_cursors (partition_id, consumer_group, last_consumed_created_at, last_consumed_id, total_messages_consumed, total_batches_consumed, last_consumed_at)
          VALUES ($4, $5, $1, $2, $3, 1, NOW())
          ON CONFLICT (partition_id, consumer_group) DO UPDATE
          SET last_consumed_created_at = EXCLUDED.last_consumed_created_at,
              last_consumed_id = EXCLUDED.last_consumed_id,
              total_messages_consumed = queen.partition_cursors.total_messages_consumed + EXCLUDED.total_messages_consumed,
              total_batches_consumed = queen.partition_cursors.total_batches_consumed + 1,
              last_consumed_at = NOW()
          RETURNING total_messages_consumed, total_batches_consumed
        `, [lastMessage.created_at, lastMessage.id, totalMessages, partitionId, actualConsumerGroup]);
        
        const newTotalConsumed = cursorUpdateResult.rows[0]?.total_messages_consumed || 0;
        const newTotalBatches = cursorUpdateResult.rows[0]?.total_batches_consumed || 0;
        
        log(`${LogTypes.ACK_BATCH} | Success: ${successCount} | Failed: ${failedCount} | Cursor: ${newTotalConsumed} msgs | ConsumerGroup: ${consumerGroup || 'QUEUE_MODE'}`);
        
        // Move failed messages to DLQ (individual failures, not batch retry)
        if (grouped.failed.length > 0) {
          const failedIds = grouped.failed.map(a => a.transactionId);
          const failedErrors = grouped.failed.map(a => a.error || 'Unknown error');
          
            await client.query(`
            INSERT INTO queen.dead_letter_queue (message_id, partition_id, consumer_group, error_message, original_created_at)
            SELECT 
              m.id,
              m.partition_id,
              $2,
              e.error_message,
              m.created_at
            FROM queen.messages m
            CROSS JOIN LATERAL UNNEST($1::varchar[], $3::text[]) AS e(txn_id, error_message)
            WHERE m.transaction_id = e.txn_id
              AND m.transaction_id = ANY($1::varchar[])
            ON CONFLICT DO NOTHING
          `, [failedIds, actualConsumerGroup, failedErrors]);
          
          log(`${LogTypes.ACK_BATCH} | Moved ${failedCount} failed messages to DLQ | ConsumerGroup: ${consumerGroup || 'QUEUE_MODE'}`);
        }
        
        // Increment acked_count and release partition lease ONLY if all messages are ACKed
        const leaseReleaseResult = await client.query(`
          UPDATE queen.partition_leases
          SET 
              -- Increment acked count and conditionally release when batch complete
              acked_count = CASE 
                WHEN acked_count + $3 >= batch_size AND batch_size > 0 
                THEN 0
                ELSE acked_count + $3
              END,
              released_at = CASE 
                WHEN acked_count + $3 >= batch_size AND batch_size > 0 
                THEN NOW() 
                ELSE released_at 
              END,
              message_batch = CASE 
                WHEN acked_count + $3 >= batch_size AND batch_size > 0 
                THEN NULL 
                ELSE message_batch 
              END,
              batch_size = CASE 
                WHEN acked_count + $3 >= batch_size AND batch_size > 0 
                THEN 0 
                ELSE batch_size 
              END
          WHERE partition_id = $1
            AND consumer_group = $2
            AND released_at IS NULL
          RETURNING (acked_count = 0 AND released_at IS NOT NULL) as lease_released
            `, [partitionId, actualConsumerGroup, totalMessages]);
        
        // Optionally log lease release for debugging
        // if (leaseReleaseResult.rows[0]?.lease_released) {
        //   log(`[ACK] Partition lease released after completing batch`);
        // }
        
        // Add results
        grouped.completed.forEach(ack => {
          results.push({ transactionId: ack.transactionId, status: 'completed' });
        });
        
        grouped.failed.forEach(ack => {
          results.push({ transactionId: ack.transactionId, status: 'failed_dlq' });
        });
      }
    });
    
    return results;
  };
  
  // Reclaim expired leases (now includes partition leases)
  const reclaimExpiredLeases = async () => {
    return withTransaction(pool, async (client) => {
      // First, handle expired partition leases
      // PERFORMANCE OPTIMIZATION: With our optimization, messages won't have 'processing' status
      // The partition lease release is what matters - it makes messages available again
      const partitionLeaseResult = await client.query(
        `WITH expired_leases AS (
          UPDATE queen.partition_leases
          SET released_at = NOW()
          WHERE lease_expires_at < NOW()
            AND released_at IS NULL
          RETURNING partition_id, consumer_group, message_batch
        )
        -- Reset message status for messages in expired leases (if status exists)
        -- NOTE: With optimization, most messages won't have status, so this updates fewer rows
        UPDATE queen.messages_status ms
        SET status = 'pending',
            retry_count = COALESCE(retry_count, 0) + 1,
            failed_at = NOW(),
            error_message = 'Partition lease expired',
            lease_expires_at = NULL,
            worker_id = NULL,
            processing_at = NULL
        FROM expired_leases el
        WHERE ms.message_id = ANY(
          SELECT jsonb_array_elements_text(el.message_batch)::uuid
        )
        AND ms.consumer_group = el.consumer_group
        RETURNING ms.message_id`
      );
      
      if (partitionLeaseResult.rows.length > 0) {
        log(`${LogTypes.RECLAIM} | Expired partition leases reclaimed | Status rows updated: ${partitionLeaseResult.rows.length}`);
      }
      
      // Then handle individual message lease expiration
      const result = await client.query(
        `UPDATE queen.messages_status 
         SET status = 'pending', 
             lease_expires_at = NULL,
             worker_id = NULL,
             processing_at = NULL
         WHERE status = 'processing' 
           AND lease_expires_at < NOW()
         RETURNING message_id, consumer_group`
      );
      
      if (result.rows.length > 0) {
        const byGroup = {};
        result.rows.forEach(row => {
          const group = row.consumer_group || 'QUEUE_MODE';
          byGroup[group] = (byGroup[group] || 0) + 1;
        });
        
        Object.entries(byGroup).forEach(([group, count]) => {
          log(`Reclaimed ${count} expired leases for consumer group: ${group}`);
        });
      }
      
      return result.rows.length;
    });
  };
  
  // Get queue statistics (updated for new structure)
  const getQueueStats = async (filters = {}) => {
    const { queue, namespace, task, fromDateTime, toDateTime } = filters;
    
    let query = `
      SELECT 
        q.name as queue,
        q.namespace,
        q.task,
        p.name as partition,
        cg.name as consumer_group,
        COUNT(CASE WHEN ms.status = 'pending' THEN 1 END) as pending,
        COUNT(CASE WHEN ms.status = 'processing' THEN 1 END) as processing,
        COUNT(CASE WHEN ms.status = 'completed' THEN 1 END) as completed,
        COUNT(CASE WHEN ms.status = 'failed' THEN 1 END) as failed,
        COUNT(CASE WHEN ms.status = 'dead_letter' THEN 1 END) as dead_letter,
        COUNT(m.id) as total_messages,
        COUNT(DISTINCT cg.name) as consumer_groups_count
      FROM queen.queues q
      LEFT JOIN queen.partitions p ON p.queue_id = q.id
      LEFT JOIN queen.messages m ON m.partition_id = p.id
      LEFT JOIN queen.messages_status ms ON ms.message_id = m.id
      LEFT JOIN queen.consumer_groups cg ON cg.queue_id = q.id AND cg.active = true
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
    
    // Add date/time filtering
    if (fromDateTime) {
      conditions.push(`m.created_at >= $${params.length + 1}::timestamp`);
      params.push(fromDateTime);
    }
    
    if (toDateTime) {
      conditions.push(`m.created_at <= $${params.length + 1}::timestamp`);
      params.push(toDateTime);
    }
    
    if (conditions.length > 0) {
      query += ` WHERE ${conditions.join(' AND ')}`;
    }
    
    query += ` GROUP BY q.name, q.namespace, q.task, p.name, cg.name
               ORDER BY q.name, p.name, cg.name`;
    
    const result = await pool.query(query, params);
    return result.rows;
  };
  
  // Get queue lag statistics
  const getQueueLag = async (filters = {}) => {
    const { queue, namespace, task, fromDateTime, toDateTime } = filters;
    
    let query = `
      WITH lag_stats AS (
        SELECT 
          q.name as queue,
          q.namespace,
          q.task,
          p.name as partition,
          COUNT(CASE WHEN ms.status = 'pending' THEN 1 END) as pending_count,
          COUNT(CASE WHEN ms.status = 'processing' THEN 1 END) as processing_count,
          COUNT(CASE WHEN ms.status IN ('pending', 'processing') THEN 1 END) as total_backlog,
          COUNT(CASE 
            WHEN ms.status = 'completed' 
            AND ms.completed_at >= COALESCE($1::timestamp, NOW() - INTERVAL '1 hour')
            AND ms.completed_at <= COALESCE($2::timestamp, NOW())
            THEN 1 
          END) as completed_messages,
          AVG(CASE 
            WHEN ms.status = 'completed' AND ms.completed_at IS NOT NULL AND m.created_at IS NOT NULL
            THEN EXTRACT(EPOCH FROM (ms.completed_at - m.created_at))
            ELSE NULL
          END) as avg_processing_time_seconds,
          PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY 
            CASE 
              WHEN ms.status = 'completed' AND ms.completed_at IS NOT NULL AND m.created_at IS NOT NULL
              THEN EXTRACT(EPOCH FROM (ms.completed_at - m.created_at))
              ELSE NULL
            END
          ) as median_processing_time_seconds,
          PERCENTILE_CONT(0.95) WITHIN GROUP (ORDER BY 
            CASE 
              WHEN ms.status = 'completed' AND ms.completed_at IS NOT NULL AND m.created_at IS NOT NULL
              THEN EXTRACT(EPOCH FROM (ms.completed_at - m.created_at))
              ELSE NULL
            END
          ) as p95_processing_time_seconds,
          MIN(m.created_at) FILTER (WHERE ms.status IN ('pending', 'processing')) as oldest_unprocessed,
          MAX(m.created_at) FILTER (WHERE ms.status = 'completed') as newest_completed
        FROM queen.queues q
        LEFT JOIN queen.partitions p ON p.queue_id = q.id
        LEFT JOIN queen.messages m ON m.partition_id = p.id
        LEFT JOIN queen.messages_status ms ON ms.message_id = m.id AND ms.consumer_group = '__QUEUE_MODE__'
    `;
    
    const conditions = [];
    const params = [];
    
    // Add date params first (they're referenced in the query above as $1 and $2)
    params.push(fromDateTime || null);
    params.push(toDateTime || null);
    
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
    
    query += ` GROUP BY q.name, q.namespace, q.task, p.name
      )
      SELECT 
        queue,
        namespace,
        task,
        partition,
        jsonb_build_object(
          'pendingCount', pending_count,
          'processingCount', processing_count,
          'totalBacklog', total_backlog,
          'completedMessages', completed_messages,
          'avgProcessingTimeSeconds', COALESCE(avg_processing_time_seconds, 0),
          'medianProcessingTimeSeconds', COALESCE(median_processing_time_seconds, 0),
          'p95ProcessingTimeSeconds', COALESCE(p95_processing_time_seconds, 0),
          'estimatedLagSeconds', CASE 
            WHEN oldest_unprocessed IS NOT NULL 
            THEN EXTRACT(EPOCH FROM (NOW() - oldest_unprocessed))
            ELSE 0
          END,
          'medianLagSeconds', CASE
            WHEN oldest_unprocessed IS NOT NULL AND newest_completed IS NOT NULL
            THEN EXTRACT(EPOCH FROM (newest_completed - oldest_unprocessed)) / 2
            ELSE 0
          END,
          'p95LagSeconds', CASE
            WHEN oldest_unprocessed IS NOT NULL
            THEN EXTRACT(EPOCH FROM (NOW() - oldest_unprocessed)) * 1.5
            ELSE 0
          END
        ) as stats
      FROM lag_stats
      ORDER BY queue, partition`;
    
    const result = await pool.query(query, params);
    return result.rows;
  };

  /**
   * Configure queue settings with namespace and task support
   * Updates queue configuration options and optionally sets namespace/task
   */
  const configureQueue = async (queueName, options = {}, namespace = null, task = null) => {
    return withTransaction(pool, async (client) => {
      // Check if queue exists (for update vs create detection)
      const existingQueue = await client.query(
        'SELECT * FROM queen.queues WHERE name = $1',
        [queueName]
      );
      const isUpdate = existingQueue.rows.length > 0;
      
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
        maxWaitTimeSeconds: 'max_wait_time_seconds',
        maxQueueSize: 'max_queue_size'
      };
      
      // Detect what changed (for update events)
      const changes = {};
      
      // Process each option
      for (const [optionKey, columnName] of Object.entries(optionMappings)) {
        if (options[optionKey] !== undefined) {
          // Track changes for update events
          if (isUpdate && existingQueue.rows[0]) {
            const old = existingQueue.rows[0];
            const newValue = options[optionKey];
            if (old[columnName] !== newValue) {
              changes[optionKey] = { old: old[columnName], new: newValue };
            }
          }
          
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
      
      // Emit appropriate event (if systemEventManager is available)
      if (systemEventManager) {
        await systemEventManager.emit(
          isUpdate ? EventTypes.QUEUE_UPDATED : EventTypes.QUEUE_CREATED,
          {
            entityType: 'queue',
            entityId: queueName,
            changes: isUpdate ? changes : options,
            namespace,
            task
          }
        );
      }
      
      // Local cache invalidation (immediate)
      resourceCache.invalidateQueue(queueName);
      
      return { 
        queue: queueName, 
        namespace: queueResult.rows[0].namespace, 
        task: queueResult.rows[0].task,
        options
      };
    });
  };

  /**
   * ═══════════════════════════════════════════════════════════════════════════
   * UNIFIED POP FUNCTION - Handles all 3 access patterns in one implementation
   * ═══════════════════════════════════════════════════════════════════════════
   * 
   * This function unifies:
   * 1. Direct partition access (queue + partition specified)
   * 2. Queue-level access (queue only, find unlocked partition)
   * 3. Filtered access (namespace/task, find matching queues + partitions)
   * 
   * Benefits over separate functions:
   * - Single source of truth for lease acquisition
   * - Consistent multi-candidate retry logic
   * - Easier to maintain and optimize
   * - Shared backpressure and metrics
   */
  const uniquePop = async (scope, options = {}) => {
    const { 
      queue, 
      partition, 
      namespace, 
      task, 
      consumerGroup 
    } = scope;
    
    const { 
      wait = false, 
      timeout = config.QUEUE.DEFAULT_TIMEOUT, 
      batch = config.QUEUE.DEFAULT_BATCH_SIZE,
      subscriptionMode = null,
      subscriptionFrom = null
    } = options;
    
    const startTime = Date.now();
    const actualConsumerGroup = consumerGroup || '__QUEUE_MODE__';
    
    // Determine access mode
    const accessMode = partition ? 'direct' : (namespace || task ? 'filtered' : 'queue');
    
    return withTransaction(pool, async (client) => {
      const connectionWaitTime = Date.now() - startTime;
      if (connectionWaitTime > 50) {
        log(`⚠️  : ${connectionWaitTime}ms`);
      }
      
      // ─────────────────────────────────────────────────────────────────────
      // PHASE 1: Find Candidate Partitions (mode-specific queries)
      // ─────────────────────────────────────────────────────────────────────
      
      const candidateQueryStart = Date.now();
      let candidatePartitions = [];
      let queueInfo = null;
      
      if (accessMode === 'direct') {
        // CASE 1: Direct partition access - single partition, no search needed
        const result = await client.query(`
          SELECT 
            q.id as queue_id,
            q.name as queue_name,
            q.lease_time,
            q.retry_limit,
            q.delayed_processing,
            q.max_wait_time_seconds,
            q.window_buffer,
            q.priority,
            p.id as partition_id,
            p.name as partition_name
          FROM queen.queues q
          JOIN queen.partitions p ON p.queue_id = q.id
          WHERE q.name = $1 AND p.name = $2
        `, [queue, partition]);
        
        if (result.rows.length > 0) {
          candidatePartitions = [result.rows[0]];
          queueInfo = result.rows[0];
        }
        
      } else if (accessMode === 'queue') {
        // CASE 2: Queue-level access - find unlocked partitions
        const result = await client.query(`
          SELECT 
            q.id as queue_id,
            q.name as queue_name,
            q.lease_time,
            q.retry_limit,
            q.delayed_processing,
            q.max_wait_time_seconds,
            q.window_buffer,
            q.priority,
            p.id as partition_id,
            p.name as partition_name
          FROM queen.queues q
          JOIN queen.partitions p ON p.queue_id = q.id
          LEFT JOIN queen.partition_leases pl ON p.id = pl.partition_id
            AND pl.consumer_group = $1
            AND pl.released_at IS NULL
            AND pl.lease_expires_at > NOW()
          WHERE q.name = $2
            AND pl.id IS NULL  -- Only unlocked partitions
          ORDER BY RANDOM()
          LIMIT $3
        `, [actualConsumerGroup, queue, config.QUEUE.MAX_PARTITION_CANDIDATES]);
        
        if (result.rows.length > 0) {
          candidatePartitions = result.rows;
          queueInfo = result.rows[0];
        }
        
      } else {
        // CASE 3: Filtered access - namespace/task
        let filterQuery = `
          SELECT 
            q.id as queue_id,
            q.name as queue_name,
            q.lease_time,
            q.retry_limit,
            q.delayed_processing,
            q.max_wait_time_seconds,
            q.window_buffer,
            q.priority,
            p.id as partition_id,
            p.name as partition_name,
            MIN(m.created_at) as oldest_message
          FROM queen.messages m
          JOIN queen.partitions p ON m.partition_id = p.id
          JOIN queen.queues q ON p.queue_id = q.id
          LEFT JOIN queen.partition_cursors pc ON p.id = pc.partition_id
            AND pc.consumer_group = $1
          LEFT JOIN queen.partition_leases pl ON p.id = pl.partition_id 
            AND pl.consumer_group = $1
          WHERE m.id > COALESCE(pc.last_consumed_id, '00000000-0000-0000-0000-000000000000'::uuid)
            AND m.created_at <= NOW() - INTERVAL '1 second' * q.delayed_processing
            AND (q.max_wait_time_seconds = 0 OR 
                 m.created_at > NOW() - INTERVAL '1 second' * q.max_wait_time_seconds)
            AND (q.window_buffer = 0 OR NOT EXISTS (
              SELECT 1 FROM queen.messages m2 
              WHERE m2.partition_id = p.id 
                AND m2.created_at > NOW() - INTERVAL '1 second' * q.window_buffer
            ))
            AND (pl.id IS NULL OR pl.released_at IS NOT NULL OR pl.lease_expires_at <= NOW())
        `;
        
        const params = [actualConsumerGroup];
        
        if (namespace) {
          params.push(namespace);
          filterQuery += ` AND q.namespace = $${params.length}`;
        }
        
        if (task) {
          params.push(task);
          filterQuery += ` AND q.task = $${params.length}`;
        }
        
        params.push(config.QUEUE.MAX_PARTITION_CANDIDATES);
        filterQuery += `
          GROUP BY q.id, q.name, q.lease_time, q.retry_limit, q.delayed_processing,
                   q.max_wait_time_seconds, q.window_buffer, q.priority,
                   p.id, p.name
          HAVING COUNT(*) > 0
          ORDER BY q.priority DESC, MIN(m.created_at) ASC, RANDOM()
          LIMIT $${params.length}
        `;
        
        const result = await client.query(filterQuery, params);
        
        if (result.rows.length > 0) {
          candidatePartitions = result.rows;
          queueInfo = result.rows[0];
        }
      }
      
      const candidateQueryTime = Date.now() - candidateQueryStart;
      
      if (candidatePartitions.length === 0) {
        return { messages: [] };
      }
      
      // Log partition selection for debugging (disabled to reduce noise)
      // if (!partition && queueInfo && !queueInfo.queue_name.startsWith('__')) {
      //   log(`DEBUG: Found ${candidatePartitions.length} candidate partitions for ${accessMode} mode`);
      // }
      
      // ─────────────────────────────────────────────────────────────────────
      // PHASE 2: Try to Acquire Lease on a Candidate Partition (shared logic)
      // ─────────────────────────────────────────────────────────────────────
      
      let acquiredPartition = null;
      const MAX_ATTEMPTS = accessMode === 'direct' ? 1 : 3;
      
      for (let attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        // Shuffle candidates to distribute across consumers
        const shuffled = [...candidatePartitions];
        for (let i = shuffled.length - 1; i > 0; i--) {
          const j = Math.floor(Math.random() * (i + 1));
          [shuffled[i], shuffled[j]] = [shuffled[j], shuffled[i]];
        }
        
        // Try to acquire lease on each candidate
        for (const candidate of shuffled) {
          const leaseAcqStart = Date.now();
          
          // Try to acquire lease - check if lease is available based on DB state
          // The key insight: released_at IS NULL means someone holds the lease
          // We can only acquire if: released_at IS NOT NULL OR lease expired
          try {
            // First, try to INSERT (for new lease) or check if UPDATE is possible
            const leaseResult = await client.query(`
              WITH attempted_acquire AS (
                INSERT INTO queen.partition_leases (
                  partition_id, 
                  consumer_group, 
                  lease_expires_at,
                  batch_size,
                  acked_count,
                  message_batch,
                  released_at
                ) VALUES ($1, $2, NOW() + INTERVAL '1 second' * $3, 0, 0, NULL, NULL)
                ON CONFLICT (partition_id, consumer_group) 
                DO UPDATE SET 
                  lease_expires_at = NOW() + INTERVAL '1 second' * $3,
                  released_at = NULL,
                  message_batch = NULL,
                  batch_size = 0,
                  acked_count = 0
                WHERE 
                  -- Only acquire if lease is released OR expired
                  queen.partition_leases.released_at IS NOT NULL 
                  OR queen.partition_leases.lease_expires_at <= NOW()
                RETURNING partition_id, true as acquired
              )
              SELECT 
                CASE WHEN EXISTS (SELECT 1 FROM attempted_acquire) THEN true ELSE false END as acquired
            `, [candidate.partition_id, actualConsumerGroup, candidate.lease_time]);
            
            // Check if we acquired the lease
            const result = leaseResult.rows[0];
            const acquired = result && result.acquired;
            
            if (acquired) {
              // Lease acquired! Now check if this partition has messages
              // If not, release and try next candidate
              const messageQueryStart = Date.now();
              
              // Get or create cursor position for this partition/consumer group
              // Handle subscription modes for bus mode (new-only, from-timestamp, etc.)
              let initialCursorId = '00000000-0000-0000-0000-000000000000';
              let initialCursorTimestamp = null;
              
              // For bus mode with subscription preferences
              if (consumerGroup && (subscriptionMode || subscriptionFrom)) {
                if (subscriptionMode === 'new' || subscriptionMode === 'new-only' || subscriptionFrom === 'now') {
                  // Start from NOW - get the latest message ID to use as cursor
                  const latestMsg = await client.query(`
                    SELECT id, created_at FROM queen.messages
                    WHERE partition_id = $1
                    ORDER BY created_at DESC, id DESC
                    LIMIT 1
                  `, [candidate.partition_id]);
                  
                  if (latestMsg.rows.length > 0) {
                    initialCursorId = latestMsg.rows[0].id;
                    initialCursorTimestamp = latestMsg.rows[0].created_at;
                  }
                }
                // else: subscriptionFrom='all' or no preference → start from beginning (00000000...)
              }
              
              const cursorResult = await client.query(`
                INSERT INTO queen.partition_cursors (partition_id, consumer_group, last_consumed_id, last_consumed_created_at)
                VALUES ($1, $2, $3, $4)
                ON CONFLICT (partition_id, consumer_group) DO UPDATE
                  SET last_consumed_at = NOW()
                RETURNING last_consumed_created_at, last_consumed_id, total_messages_consumed
              `, [candidate.partition_id, actualConsumerGroup, initialCursorId, initialCursorTimestamp]);
              
              const cursor = cursorResult.rows[0];
              const cursorId = cursor.last_consumed_id || '00000000-0000-0000-0000-000000000000';
              const totalConsumed = cursor.total_messages_consumed || 0;
              
              // Cursor-based query using ONLY UUID comparison (UUIDv7 is time-ordered)
              // Conditionally add timestamp filters only when configured
              const hasDelayedProcessing = candidate.delayed_processing > 0;
              const hasEviction = candidate.max_wait_time_seconds > 0;
              
              let whereClause = 'm.partition_id = $4 AND m.id > $5::uuid';
              let queryParams = [
                candidate.partition_name,
                candidate.queue_name,
                candidate.priority || 0,
                candidate.partition_id,
                cursorId
              ];
              let paramIndex = 6;
              
              if (hasDelayedProcessing) {
                whereClause += ` AND m.created_at <= NOW() - INTERVAL '1 second' * $${paramIndex}`;
                queryParams.push(candidate.delayed_processing);
                paramIndex++;
              }
              
              if (hasEviction) {
                whereClause += ` AND m.created_at > NOW() - INTERVAL '1 second' * $${paramIndex}`;
                queryParams.push(candidate.max_wait_time_seconds);
                paramIndex++;
              }
              
              queryParams.push(batch);
              const limitParam = `$${paramIndex}`;
              
              const messagesResult = await client.query(`
                SELECT 
                  m.id,
                  m.transaction_id,
                  m.trace_id,
                  m.payload,
                  m.is_encrypted,
                  m.created_at,
                  $1 as partition_name,
                  $2 as queue_name,
                  $3 as priority,
                  0 as retry_count
                FROM queen.messages m
                WHERE ${whereClause}
                ORDER BY m.created_at ASC, m.id ASC
                LIMIT ${limitParam}
                FOR UPDATE OF m SKIP LOCKED
              `, queryParams)
              .catch(err => {
                if (err.code === '55P03') { // Lock not available
                  return { rows: [] };
                }
                throw err;
              });
              
              const messageQueryTime = Date.now() - messageQueryStart;
              const messages = messagesResult.rows;
              
              // Check if we found messages
              if (messages.length === 0) {
                // No messages in this partition (cursor exhausted) - release lease and try next candidate
                log(`[CURSOR] Partition ${candidate.partition_name} exhausted after ${totalConsumed} messages`);
                await client.query(`
                  UPDATE queen.partition_leases
                  SET released_at = NOW()
                  WHERE partition_id = $1 AND consumer_group = $2
                `, [candidate.partition_id, actualConsumerGroup]);
                continue; // Try next candidate partition
              }
              
              // Success! We have messages from this partition
              acquiredPartition = candidate;
              
              // ─────────────────────────────────────────────────────────────────────
              // PHASE 4: Update Partition Lease with Message Batch
              // ─────────────────────────────────────────────────────────────────────
              
              const leaseUpdateStart = Date.now();
              const messageIds = messages.map(m => m.id);
              await client.query(`
                UPDATE queen.partition_leases
                SET message_batch = $1::jsonb,
                    batch_size = $2::integer,
                    acked_count = 0
                WHERE partition_id = $3 AND consumer_group = $4
              `, [JSON.stringify(messageIds), messageIds.length, acquiredPartition.partition_id, actualConsumerGroup]);
              const leaseUpdateTime = Date.now() - leaseUpdateStart;
              
              log(`[CURSOR-LEASE] Partition: ${acquiredPartition.partition_name} | Leased ${messageIds.length} messages for processing`);
              
              // ─────────────────────────────────────────────────────────────────────
              // PHASE 5: Decrypt and Format Messages
              // ─────────────────────────────────────────────────────────────────────
              
              const decryptStart = Date.now();
              const formattedMessages = await Promise.all(messages.map(async (msg) => {
                let payload = msg.payload;
                
                if (msg.is_encrypted && encryption.isEncryptionEnabled()) {
                  try {
                    payload = await encryption.decryptPayload(msg.payload);
                  } catch (error) {
                    log(`${LogTypes.ERROR} | Failed to decrypt message ${msg.transaction_id}: ${error.message}`);
                  }
                }
                
                return {
                  id: msg.id,
                  transactionId: msg.transaction_id,
                  traceId: msg.trace_id,
                  queue: msg.queue_name,
                  partition: msg.partition_name,
                  data: payload,
                  payload: payload,
                  retryCount: msg.retry_count || 0,
                  priority: msg.priority || 0,
                  createdAt: msg.created_at,
                  consumerGroup: consumerGroup || null
                };
              }));
              const decryptTime = Date.now() - decryptStart;
              
              // ─────────────────────────────────────────────────────────────────────
              // PHASE 6: Metrics and Logging
              // ─────────────────────────────────────────────────────────────────────
              
              const totalTime = Date.now() - startTime;
              const overhead = totalTime - (connectionWaitTime + candidateQueryTime + messageQueryTime + leaseUpdateTime + decryptTime);
              
              log(`${LogTypes.POP} | Mode: ${accessMode} | Count: ${formattedMessages.length} | ConsumerGroup: ${actualConsumerGroup === '__QUEUE_MODE__' ? 'QUEUE_MODE' : actualConsumerGroup}`);
              log(`  ⏱️  ConnWait:${connectionWaitTime.toFixed(0)}ms CandidateQ:${candidateQueryTime.toFixed(0)}ms MsgQuery:${messageQueryTime.toFixed(0)}ms LeaseUpd:${leaseUpdateTime.toFixed(0)}ms Decrypt:${decryptTime.toFixed(0)}ms Overhead:${overhead.toFixed(0)}ms Total:${totalTime.toFixed(0)}ms`);
              
              return { messages: formattedMessages };
            }
          } catch (err) {
            // Unexpected error
            if (!candidate.queue_name.startsWith('__')) {
              log(`LEASE ERROR: Partition=${candidate.partition_name} | Error: ${err.message}`);
            }
            throw err;
          }
        }
        
        if (acquiredPartition) break;
      }
      
      // If we exhausted all attempts and couldn't get messages
      if (!acquiredPartition) {
        log(`WARN: Failed to acquire any partition with messages after ${MAX_ATTEMPTS} attempts`);
        return { messages: [] };
      }
    });
  };

  // Export all functions
  return {
    pushMessages: pushMessagesBatch,
    pushMessagesBatch,
    acknowledgeMessage,
    acknowledgeMessages,
    ackMessage: acknowledgeMessage,  // Alias for compatibility
    reclaimExpiredLeases,
    getQueueStats,
    getQueueLag,
    uniquePop,  // ← New unified function
    configureQueue
  };
};
