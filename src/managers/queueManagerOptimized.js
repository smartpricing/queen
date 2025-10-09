import { generateUUID } from '../utils/uuid.js';
import { withTransaction } from '../database/connection.js';
import { PoolManager } from '../database/poolManager.js';
import * as encryption from '../services/encryptionService.js';
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
                completed_retention_seconds, retention_enabled, priority, max_queue_size`,
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
  
  // Ensure consumer group exists and get subscription preferences
  /*const ensureConsumerGroup = async (client, queueId, consumerGroup, subscriptionMode = null, subscriptionFrom = null) => {
    if (!consumerGroup) return null;
    
    // Check if consumer group exists
    const existing = await client.query(
      `SELECT * FROM queen.consumer_groups 
       WHERE queue_id = $1 AND name = $2`,
      [queueId, consumerGroup]
    );
    
    if (existing.rows.length > 0) {
      // Update last seen
      await client.query(
        `UPDATE queen.consumer_groups 
         SET last_seen_at = NOW() 
         WHERE queue_id = $1 AND name = $2`,
        [queueId, consumerGroup]
      );
      return existing.rows[0];
    }
    
    // Create new consumer group with subscription preferences
    let subscriptionStartFrom = null;
    
    if (subscriptionMode === 'new') {
      // Only consume messages created after this exact moment
      // Get the max created_at from existing messages to ensure we only get newer ones
      // If no messages exist, use current time
      // Simply use NOW() - with TIMESTAMPTZ, timezone handling is automatic
      const nowResult = await client.query("SELECT NOW() as db_now");
      subscriptionStartFrom = nowResult.rows[0].db_now;
    } else if (subscriptionFrom) {
      // Consume from specific timestamp
      subscriptionStartFrom = new Date(subscriptionFrom);
    }
    // If neither, subscriptionStartFrom remains NULL (consume all)
    
    const result = await client.query(
      `INSERT INTO queen.consumer_groups (queue_id, name, subscription_start_from)
       VALUES ($1, $2, $3)
       RETURNING *`,
      [queueId, consumerGroup, subscriptionStartFrom]
    );
    
    return result.rows[0];
  };*/
  
  // Optimized batch insert for high throughput (simplified - no status)
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
          
          for (const item of batch) {
            const messageId = generateUUID();
            const transactionId = item.transactionId || generateUUID();
            const traceId = item.traceId || null;
            
            // Check for duplicate transaction ID
            const dupCheck = await client.query(
              'SELECT id FROM queen.messages WHERE transaction_id = $1',
              [transactionId]
            );
            
            if (dupCheck.rows.length > 0) {
              duplicates.push({
                id: dupCheck.rows[0].id,
                transactionId: transactionId,
                status: 'duplicate'
              });
              continue;
            }
            
            messageIds.push(messageId);
            transactionIds.push(transactionId);
            traceIds.push(traceId);
            
            // Handle encryption if enabled
            let payload = item.payload;
            let isEncrypted = false;
            
            if (encryptionEnabled && encryption.isEncryptionEnabled()) {
              try {
                payload = await encryption.encryptPayload(item.payload);
                isEncrypted = true;
              } catch (error) {
                log(`ERROR: Encryption failed for message ${messageId}:`, error);
                // Continue with unencrypted payload
              }
            }
            
            // Handle large payloads and special cases
            let jsonPayload;
            try {
              // Handle null/undefined payloads - convert to JSON null string
              if (payload === null || payload === undefined) {
                jsonPayload = 'null';  // JSONB expects the string 'null' for null values
              } else {
                // Try to stringify the payload
                jsonPayload = JSON.stringify(payload);
              }
            } catch (error) {
              // If JSON.stringify fails (e.g., circular references, too large), 
              // store as a string representation
              log(`WARN: Failed to stringify payload for message ${messageId}:`, error.message);
              jsonPayload = JSON.stringify({ 
                error: 'Payload serialization failed', 
                type: typeof payload,
                message: error.message 
              });
            }
            
            payloads.push(jsonPayload);
            encryptedFlags.push(isEncrypted);
          }
          
          // Batch insert messages with trace_id
          if (messageIds.length > 0) {
            const insertQuery = `
              INSERT INTO queen.messages (id, transaction_id, trace_id, partition_id, payload, is_encrypted)
              SELECT * FROM UNNEST($1::uuid[], $2::varchar[], $3::uuid[], $4::uuid[], $5::jsonb[], $6::boolean[])
              RETURNING id, transaction_id, trace_id
            `;
            
            const insertResult = await client.query(insertQuery, [
              messageIds,
              transactionIds,
              traceIds,
              Array(messageIds.length).fill(partitionId),
              payloads,
              encryptedFlags
            ]);
            
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
    }
    
    return allResults;
  };

  // Helper function to ensure consumer group exists
const ensureConsumerGroup = async (client, queueId, consumerGroup, subscriptionMode, subscriptionFrom) => {
  // Handle special subscription values
  let effectiveSubscriptionFrom = subscriptionFrom;
  if (subscriptionFrom === 'now' || subscriptionMode === 'new' || subscriptionMode === 'new-only') {
    effectiveSubscriptionFrom = new Date();
  } else if (!subscriptionFrom || subscriptionFrom === 'all') {
    effectiveSubscriptionFrom = null; // Will consume all messages
  }
  
  const result = await client.query(`
    INSERT INTO queen.consumer_groups (queue_id, name, subscription_start_from)
    VALUES ($1, $2, $3)
    ON CONFLICT (queue_id, name) DO UPDATE SET
      subscription_start_from = CASE 
        WHEN queen.consumer_groups.subscription_start_from IS NULL 
        THEN EXCLUDED.subscription_start_from
        ELSE queen.consumer_groups.subscription_start_from
      END,
      last_seen_at = NOW()
    RETURNING *
  `, [queueId, consumerGroup, effectiveSubscriptionFrom]);
  
  return result.rows[0];
};

// Helper function for eviction on pop
const evictOnPop = async (client, queueName) => {
  const evictionResult = await client.query(`
    WITH queue_config AS (
      SELECT id, max_wait_time_seconds
      FROM queen.queues
      WHERE name = $1 AND max_wait_time_seconds > 0
    ),
    evicted AS (
      DELETE FROM queen.messages
      WHERE id IN (
        SELECT m.id
        FROM queen.messages m
        JOIN queen.partitions p ON m.partition_id = p.id
        JOIN queue_config qc ON p.queue_id = qc.id
        WHERE m.created_at < NOW() - INTERVAL '1 second' * qc.max_wait_time_seconds
      )
      RETURNING id
    )
    SELECT COUNT(*) as count FROM evicted
  `, [queueName]);
  
  if (evictionResult.rows[0].count > 0) {
    log(`${LogTypes.POP} | EVICTION | Queue: ${queueName} | Count: ${evictionResult.rows[0].count}`);
  }
};

/**
 * Pop messages with simplified transaction-based approach
 * This is a drop-in replacement for the original popMessages function
 */
const popMessagesV2 = async (scope, options = {}) => {
  const { queue, partition, consumerGroup } = scope;
  const { 
    wait = false, 
    timeout = config.QUEUE.DEFAULT_TIMEOUT, 
    batch = config.QUEUE.DEFAULT_BATCH_SIZE,
    subscriptionMode = null,
    subscriptionFrom = null
  } = options;
  
  return withTransaction(pool, async (client) => {
    // Set transaction isolation level to prevent dirty reads
    await client.query('SET TRANSACTION ISOLATION LEVEL REPEATABLE READ');
    
    // Step 1: Reclaim expired leases
    const reclaimResult = await client.query(`
      UPDATE queen.messages_status 
      SET status = 'pending', 
          lease_expires_at = NULL, 
          worker_id = NULL, 
          processing_at = NULL
      WHERE status = 'processing' 
        AND lease_expires_at < NOW()
        AND consumer_group = $1
      RETURNING message_id
    `, [consumerGroup || '__QUEUE_MODE__']);
    
    if (reclaimResult.rowCount > 0) {
      log(`Reclaimed ${reclaimResult.rowCount} expired leases`);
    }
    
    // Step 2: Evict expired messages
    await evictOnPop(client, queue);
    
    // Step 3: Get queue information
    const queueResult = await client.query(
      'SELECT * FROM queen.queues WHERE name = $1',
      [queue]
    );
    
    if (queueResult.rows.length === 0) {
      return { messages: [] };
    }
    
    const queueInfo = queueResult.rows[0];
    
    // Step 4: Handle consumer group if specified
    let consumerGroupInfo = null;
    if (consumerGroup) {
      consumerGroupInfo = await ensureConsumerGroup(
        client, 
        queueInfo.id, 
        consumerGroup,
        subscriptionMode,
        subscriptionFrom
      );
    }
    
    const actualConsumerGroup = consumerGroup || '__QUEUE_MODE__';
    let subscriptionStart = consumerGroupInfo?.subscription_start_from;
    
    // Handle subscription start date
    if (consumerGroup) {
      if (!subscriptionStart) {
        subscriptionStart = '1970-01-01';
      } else if (typeof subscriptionStart === 'object' && subscriptionStart instanceof Date) {
        subscriptionStart = subscriptionStart.toISOString();
      }
    }
    
    // Step 5: Get or create partition
    let partitionInfo;
    if (partition) {
      // Specific partition requested
      const partitionResult = await client.query(`
        SELECT p.* FROM queen.partitions p
        WHERE p.queue_id = $1 AND p.name = $2
      `, [queueInfo.id, partition]);
      
      if (partitionResult.rows.length === 0) {
        // Create partition if it doesn't exist
        const createPartitionResult = await client.query(`
          INSERT INTO queen.partitions (id, queue_id, name)
          VALUES ($1, $2, $3)
          ON CONFLICT (queue_id, name) DO UPDATE SET name = EXCLUDED.name
          RETURNING *
        `, [uuidv4(), queueInfo.id, partition]);
        partitionInfo = createPartitionResult.rows[0];
      } else {
        partitionInfo = partitionResult.rows[0];
      }
    } else {
      // Find any available partition with messages that is not currently leased
      // This applies to both QUEUE MODE and BUS MODE for partition isolation
      const partitionResult = await client.query(`
        SELECT DISTINCT p.* 
        FROM queen.partitions p
        JOIN queen.messages m ON m.partition_id = p.id
        LEFT JOIN queen.messages_status ms ON m.id = ms.message_id 
          AND ms.consumer_group = $2
        LEFT JOIN queen.partition_leases pl ON p.id = pl.partition_id 
          AND pl.consumer_group = $2
          AND pl.released_at IS NULL
          AND pl.lease_expires_at > NOW()
        WHERE p.queue_id = $1
          AND (ms.id IS NULL OR ms.status IN ('pending', 'failed'))
          AND pl.id IS NULL  -- No active lease for this partition
        ORDER BY p.created_at
        LIMIT 1
      `, [queueInfo.id, actualConsumerGroup]);
      
      if (partitionResult.rows.length === 0) {
        // No partitions with available messages, check if we need to create default
        const anyPartitionResult = await client.query(`
          SELECT * FROM queen.partitions WHERE queue_id = $1 LIMIT 1
        `, [queueInfo.id]);
        
        if (anyPartitionResult.rows.length === 0) {
          // Create default partition
          const createPartitionResult = await client.query(`
            INSERT INTO queen.partitions (id, queue_id, name)
            VALUES ($1, $2, 'Default')
            ON CONFLICT (queue_id, name) DO UPDATE SET name = EXCLUDED.name
            RETURNING *
          `, [uuidv4(), queueInfo.id]);
          partitionInfo = createPartitionResult.rows[0];
        } else {
          return { messages: [] };
        }
      } else {
        partitionInfo = partitionResult.rows[0];
      }
    }
    
    // Step 6: Try to acquire or verify partition lease (for both QUEUE MODE and BUS MODE)
    // This ensures partition isolation between consumers in the same group
    
    // First, try to acquire a new lease or check if we can take over an expired one
    const leaseResult = await client.query(`
      INSERT INTO queen.partition_leases (
        partition_id, 
        consumer_group, 
        lease_expires_at
      ) VALUES ($1, $2, NOW() + INTERVAL '1 second' * $3)
      ON CONFLICT (partition_id, consumer_group) 
      DO UPDATE SET 
        lease_expires_at = CASE
          WHEN queen.partition_leases.released_at IS NOT NULL 
            OR queen.partition_leases.lease_expires_at <= NOW()
          THEN EXCLUDED.lease_expires_at
          ELSE queen.partition_leases.lease_expires_at
        END,
        released_at = CASE
          WHEN queen.partition_leases.released_at IS NOT NULL 
            OR queen.partition_leases.lease_expires_at <= NOW()
          THEN NULL
          ELSE queen.partition_leases.released_at
        END
      RETURNING 
        lease_expires_at,
        (lease_expires_at = NOW() + INTERVAL '1 second' * $3) as acquired
    `, [partitionInfo.id, actualConsumerGroup, queueInfo.lease_time]);
    
    // Check if we actually acquired the lease
    if (!leaseResult.rows[0].acquired) {
      // Another consumer still has an active lease on this partition
      return { messages: [] };
    }
    
    // Step 7: Build WHERE clause based on mode
    let whereClause;
    let queryParams;
    
    if (consumerGroup) {
      // BUS MODE with consumer group
      whereClause = `
        m.partition_id = $2
        AND (ms.id IS NULL OR ms.status IN ('pending', 'failed'))
        AND m.created_at <= NOW() - INTERVAL '1 second' * $3
        AND m.created_at >= $4::timestamp
        AND ($5 = 0 OR m.created_at > NOW() - INTERVAL '1 second' * $5)
      `;
      queryParams = [
        actualConsumerGroup,
        partitionInfo.id,
        queueInfo.delayed_processing || 0,
        subscriptionStart,
        queueInfo.max_wait_time_seconds || 0,
        batch
      ];
    } else {
      // QUEUE MODE
      whereClause = `
        m.partition_id = $2
        AND (ms.id IS NULL OR ms.status IN ('pending', 'failed'))
        AND m.created_at <= NOW() - INTERVAL '1 second' * $3
        AND ($4 = 0 OR m.created_at > NOW() - INTERVAL '1 second' * $4)
        AND ($5 = 0 OR NOT EXISTS (
          SELECT 1 FROM queen.messages m2 
          WHERE m2.partition_id = p.id 
            AND m2.created_at > NOW() - INTERVAL '1 second' * $5
        ))
      `;
      queryParams = [
        actualConsumerGroup,
        partitionInfo.id,
        queueInfo.delayed_processing || 0,
        queueInfo.max_wait_time_seconds || 0,
        queueInfo.window_buffer || 0,
        batch
      ];
    }
    
    // Step 8: Select and lock messages
    const messagesResult = await client.query(`
      SELECT 
        m.id,
        m.transaction_id,
        m.trace_id,
        m.payload,
        m.is_encrypted,
        m.created_at,
        p.name as partition_name,
        q.name as queue_name,
        q.priority,
        COALESCE(ms.retry_count, 0) as retry_count
      FROM queen.messages m
      JOIN queen.partitions p ON m.partition_id = p.id
      JOIN queen.queues q ON p.queue_id = q.id
      LEFT JOIN queen.messages_status ms ON m.id = ms.message_id 
        AND ms.consumer_group = $1
      WHERE ${whereClause}
      ORDER BY m.created_at ASC
      LIMIT $6
      FOR UPDATE OF m NOWAIT
    `, queryParams).catch(err => {
      if (err.code === '55P03') { // Lock not available
        return { rows: [] };
      }
      throw err;
    });
    
    const messages = messagesResult.rows;
    
    if (messages.length === 0) {
      // Release partition lease if no messages
      await client.query(`
        UPDATE queen.partition_leases
        SET released_at = NOW()
        WHERE partition_id = $1 AND consumer_group = $2
      `, [partitionInfo.id, actualConsumerGroup]);
      return { messages: [] };
    }
    
    // Step 9: Update or insert message status for selected messages
    const leaseExpiresAt = new Date(Date.now() + (queueInfo.lease_time * 1000));
    
    for (const message of messages) {
      await client.query(`
        INSERT INTO queen.messages_status (
          message_id,
          consumer_group,
          status,
          lease_expires_at,
          processing_at,
          worker_id,
          retry_count
        ) VALUES ($1, $2, 'processing', $3, NOW(), $4, $5)
        ON CONFLICT (message_id, consumer_group) 
        DO UPDATE SET
          status = 'processing',
          lease_expires_at = EXCLUDED.lease_expires_at,
          processing_at = NOW(),
          worker_id = EXCLUDED.worker_id
          -- Keep existing retry_count when re-processing failed messages
      `, [
        message.id,
        actualConsumerGroup,
        leaseExpiresAt,
        config.WORKER_ID,
        message.retry_count || 0
      ]);
    }
    
    // Step 10: Update partition lease with message batch
    if (messages.length > 0) {
      const messageIds = messages.map(m => m.id);
      await client.query(`
        UPDATE queen.partition_leases
        SET message_batch = $1::jsonb
        WHERE partition_id = $2 AND consumer_group = $3
      `, [JSON.stringify(messageIds), partitionInfo.id, actualConsumerGroup]);
    }
    
    // Step 11: Decrypt payloads if needed and format response
    const decryptedMessages = await Promise.all(messages.map(async (msg) => {
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
    
    log(`${LogTypes.POP} | Count: ${decryptedMessages.length} | ConsumerGroup: ${actualConsumerGroup === '__QUEUE_MODE__' ? 'QUEUE_MODE' : actualConsumerGroup}`);
    
    return { messages: decryptedMessages };
  });
};


const popMessages = async (scope, options = {}) => {
  return popMessagesV2(scope, options);
};

  // Acknowledge messages (now per consumer group)
  const acknowledgeMessage = async (transactionId, status = 'completed', error = null, consumerGroup = null) => {
    return withTransaction(pool, async (client) => {
      // Find the message and status entry (must be in 'processing' status to be acknowledged)
      const findQuery = consumerGroup 
        ? `SELECT ms.retry_count, ms.status, ms.message_id, m.partition_id, 
                  q.retry_limit, q.dlq_after_max_retries
           FROM queen.messages_status ms
           JOIN queen.messages m ON ms.message_id = m.id
           JOIN queen.partitions p ON m.partition_id = p.id
           JOIN queen.queues q ON p.queue_id = q.id
           WHERE m.transaction_id = $1 AND ms.consumer_group = $2 AND ms.status = 'processing'`
        : `SELECT ms.retry_count, ms.status, ms.message_id, m.partition_id, 
                  q.retry_limit, q.dlq_after_max_retries
           FROM queen.messages_status ms
           JOIN queen.messages m ON ms.message_id = m.id
           JOIN queen.partitions p ON m.partition_id = p.id
           JOIN queen.queues q ON p.queue_id = q.id
           WHERE m.transaction_id = $1 AND ms.consumer_group = '__QUEUE_MODE__' AND ms.status = 'processing'`;
      
      const params = consumerGroup ? [transactionId, consumerGroup] : [transactionId];
      const result = await client.query(findQuery, params);
      
      if (result.rows.length === 0) {
        log(`WARN: Message not found for acknowledgment: ${transactionId}, consumerGroup: ${consumerGroup || 'QUEUE_MODE'}`);
        
        // Try to find the message regardless of status for debugging
        const debugQuery = consumerGroup
          ? `SELECT ms.retry_count, ms.status FROM queen.messages_status ms
             JOIN queen.messages m ON ms.message_id = m.id
             WHERE m.transaction_id = $1 AND ms.consumer_group = $2`
          : `SELECT ms.retry_count, ms.status FROM queen.messages_status ms
             JOIN queen.messages m ON ms.message_id = m.id
             WHERE m.transaction_id = $1 AND ms.consumer_group = '__QUEUE_MODE__'`;
        const debugResult = await client.query(debugQuery, params);
        if (debugResult.rows.length > 0) {
          log(`DEBUG: Found message with status=${debugResult.rows[0].status}, retry_count=${debugResult.rows[0].retry_count}`);
        }
        
        return { status: 'not_found', transaction_id: transactionId };
      }
      
      const messageStatus = result.rows[0];
      log(`DEBUG: Found message for ack with retry_count=${messageStatus.retry_count}, status=${messageStatus.status}, dlq=${messageStatus.dlq_after_max_retries}, limit=${messageStatus.retry_limit}`);
      
      if (status === 'completed') {
        // Mark as completed and release partition lease
        const actualConsumerGroup = consumerGroup || '__QUEUE_MODE__';
        const updateParams = consumerGroup 
          ? [transactionId, consumerGroup]
          : [transactionId];
        
        const updateQuery = consumerGroup
          ? `UPDATE queen.messages_status ms
             SET status = 'completed', completed_at = NOW()
             FROM queen.messages m
             WHERE ms.message_id = m.id 
               AND m.transaction_id = $1 
               AND ms.consumer_group = $2`
          : `UPDATE queen.messages_status ms
             SET status = 'completed', completed_at = NOW()
             FROM queen.messages m
             WHERE ms.message_id = m.id 
               AND m.transaction_id = $1 
               AND ms.consumer_group = '__QUEUE_MODE__'`;
        
        await client.query(updateQuery, updateParams);
        
        // Release partition lease if this was the last message in the batch
        const releaseQuery = `
          WITH message_partition AS (
            -- Get the partition for this message
            SELECT DISTINCT m.partition_id
            FROM queen.messages m
            WHERE m.transaction_id = $1
          ),
          check_remaining AS (
            -- Check if there are any other processing messages in this partition
            SELECT 
              mp.partition_id,
              COUNT(ms.message_id) as processing_count
            FROM message_partition mp
            LEFT JOIN queen.messages m ON m.partition_id = mp.partition_id
            LEFT JOIN queen.messages_status ms ON ms.message_id = m.id
              AND ms.consumer_group = $2
              AND ms.status = 'processing'
            GROUP BY mp.partition_id
          )
          -- Release the lease only if no messages are processing
          UPDATE queen.partition_leases pl
          SET released_at = NOW()
          FROM check_remaining cr
          WHERE pl.partition_id = cr.partition_id
            AND pl.consumer_group = $2
            AND pl.released_at IS NULL
            AND cr.processing_count = 0`;
        
        await client.query(releaseQuery, [transactionId, actualConsumerGroup]);
        
        log(`${LogTypes.ACK} | TransactionId: ${transactionId} | Status: completed | ConsumerGroup: ${consumerGroup || 'QUEUE_MODE'}`);
      } else if (status === 'failed') {
        // Handle failure with retry logic
        const actualConsumerGroup = consumerGroup || '__QUEUE_MODE__';
        const currentRetryCount = messageStatus.retry_count || 0;
        const nextRetryCount = currentRetryCount + 1;
        const retryLimit = messageStatus.retry_limit || config.QUEUE.DEFAULT_RETRY_LIMIT;
        const dlqEnabled = messageStatus.dlq_after_max_retries;
        
        log(`${LogTypes.ACK} | Retry check: current=${currentRetryCount}, next=${nextRetryCount}, limit=${retryLimit}, dlq=${dlqEnabled}`);
        
        if (nextRetryCount > retryLimit && dlqEnabled) {
          // Move to dead letter queue after exceeding retry limit
          const updateParams = consumerGroup 
            ? [transactionId, error, consumerGroup]
            : [transactionId, error];
          
          const updateQuery = consumerGroup
            ? `UPDATE queen.messages_status ms
               SET status = 'dead_letter', failed_at = NOW(), error_message = $2
               FROM queen.messages m
               WHERE ms.message_id = m.id 
                 AND m.transaction_id = $1 
                 AND ms.consumer_group = $3`
            : `UPDATE queen.messages_status ms
               SET status = 'dead_letter', failed_at = NOW(), error_message = $2
               FROM queen.messages m
               WHERE ms.message_id = m.id 
                 AND m.transaction_id = $1 
                 AND ms.consumer_group = '__QUEUE_MODE__'`;
          
          await client.query(updateQuery, updateParams);
          
          log(`${LogTypes.ACK} | TransactionId: ${transactionId} | Status: dead_letter | ConsumerGroup: ${consumerGroup || 'QUEUE_MODE'} | Error: ${error}`);
          
          return { status: 'dead_letter', transaction_id: transactionId };
        } else {
          // Mark as failed and increment retry count
          const updateParams = consumerGroup 
            ? [transactionId, error, consumerGroup]
            : [transactionId, error];
          
          const updateQuery = consumerGroup
            ? `UPDATE queen.messages_status ms
               SET status = 'failed', 
                   failed_at = NOW(), 
                   error_message = $2, 
                   lease_expires_at = NULL,
                   retry_count = COALESCE(retry_count, 0) + 1
               FROM queen.messages m
               WHERE ms.message_id = m.id 
                 AND m.transaction_id = $1 
                 AND ms.consumer_group = $3`
            : `UPDATE queen.messages_status ms
               SET status = 'failed', 
                   failed_at = NOW(), 
                   error_message = $2, 
                   lease_expires_at = NULL,
                   retry_count = COALESCE(retry_count, 0) + 1
               FROM queen.messages m
               WHERE ms.message_id = m.id 
                 AND m.transaction_id = $1 
                 AND ms.consumer_group = '__QUEUE_MODE__'`;
          
          await client.query(updateQuery, updateParams);
          
          // Release partition lease on failure as well (so partition can be retried)
          const releaseOnFailQuery = `
            WITH message_partition AS (
              SELECT DISTINCT m.partition_id
              FROM queen.messages m
              WHERE m.transaction_id = $1
            ),
            check_remaining AS (
              SELECT 
                mp.partition_id,
                COUNT(ms.message_id) as processing_count
              FROM message_partition mp
              LEFT JOIN queen.messages m ON m.partition_id = mp.partition_id
              LEFT JOIN queen.messages_status ms ON ms.message_id = m.id
                AND ms.consumer_group = $2
                AND ms.status = 'processing'
              GROUP BY mp.partition_id
            )
            UPDATE queen.partition_leases pl
            SET released_at = NOW()
            FROM check_remaining cr
            WHERE pl.partition_id = cr.partition_id
              AND pl.consumer_group = $2
              AND pl.released_at IS NULL
              AND cr.processing_count = 0`;
          
          await client.query(releaseOnFailQuery, [transactionId, actualConsumerGroup]);
          
          log(`${LogTypes.ACK} | TransactionId: ${transactionId} | Status: failed (retry ${nextRetryCount}/${retryLimit}) | ConsumerGroup: ${consumerGroup || 'QUEUE_MODE'} | Error: ${error}`);
        }
      }
      
      return { status, transaction_id: transactionId };
    });
  };
  
  // Batch acknowledge messages
  const acknowledgeMessages = async (acknowledgments, consumerGroup = null) => {
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
        const actualConsumerGroup = consumerGroup || '__QUEUE_MODE__';
        
        const updateQuery = consumerGroup
          ? `UPDATE queen.messages_status ms
             SET status = 'completed', completed_at = NOW()
             FROM queen.messages m
             WHERE ms.message_id = m.id 
               AND m.transaction_id = ANY($1::varchar[])
               AND ms.consumer_group = $2`
          : `UPDATE queen.messages_status ms
             SET status = 'completed', completed_at = NOW()
             FROM queen.messages m
             WHERE ms.message_id = m.id 
               AND m.transaction_id = ANY($1::varchar[])
               AND ms.consumer_group = '__QUEUE_MODE__'`;
        
        const params = consumerGroup ? [ids, consumerGroup] : [ids];
        await client.query(updateQuery, params);
        
        // Release partition leases for partitions with no more processing messages
        const releaseQuery = `
          WITH affected_partitions AS (
            SELECT DISTINCT m.partition_id
            FROM queen.messages m
            WHERE m.transaction_id = ANY($1::varchar[])
          )
          UPDATE queen.partition_leases pl
          SET released_at = NOW()
          FROM affected_partitions ap
          WHERE pl.partition_id = ap.partition_id
            AND pl.consumer_group = $2
            AND pl.released_at IS NULL
            AND NOT EXISTS (
              SELECT 1 
              FROM queen.messages_status ms
              JOIN queen.messages m2 ON ms.message_id = m2.id
              WHERE m2.partition_id = ap.partition_id
                AND ms.consumer_group = $2
                AND ms.status = 'processing'
            )`;
        
        await client.query(releaseQuery, [ids, actualConsumerGroup]);
        
        log(`${LogTypes.ACK_BATCH} | Status: completed | Count: ${ids.length} | ConsumerGroup: ${consumerGroup || 'QUEUE_MODE'} | TransactionIds: [${ids.join(', ')}]`);
        
        grouped.completed.forEach(ack => {
          results.push({ transactionId: ack.transactionId, status: 'completed' });
        });
      }
      
      // Process failed messages (needs individual handling for retry logic)
      for (const ack of grouped.failed) {
        const result = await acknowledgeMessage(ack.transactionId, 'failed', ack.error, consumerGroup);
        results.push({ transactionId: ack.transactionId, ...result });
      }
    });
    
    return results;
  };
  
  // Reclaim expired leases (now includes partition leases)
  const reclaimExpiredLeases = async () => {
    return withTransaction(pool, async (client) => {
      // First, handle expired partition leases
      const partitionLeaseResult = await client.query(
        `WITH expired_leases AS (
          UPDATE queen.partition_leases
          SET released_at = NOW()
          WHERE lease_expires_at < NOW()
            AND released_at IS NULL
          RETURNING partition_id, consumer_group, message_batch
        )
        -- Reset message status for messages in expired leases
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
        log(`${LogTypes.RECLAIM} | Expired partition leases reclaimed | Message count: ${partitionLeaseResult.rows.length}`);
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

  // Export all functions
  return {
    pushMessages: pushMessagesBatch,
    pushMessagesBatch,
    popMessages,
    acknowledgeMessage,
    acknowledgeMessages,
    ackMessage: acknowledgeMessage,  // Alias for compatibility
    reclaimExpiredLeases,
    getQueueStats,
    getQueueLag,
    // Additional functions for compatibility
    popMessagesWithFilters: async (filters, options = {}) => {
      const { namespace, task } = filters;
      const { wait = false, timeout = config.QUEUE.DEFAULT_TIMEOUT, batch = config.QUEUE.DEFAULT_BATCH_SIZE } = options;
      
      return withTransaction(pool, async (client) => {
        // Build query for namespace/task filtering with new schema
        let query = `
          WITH available_messages AS (
            SELECT m.id, m.transaction_id, m.trace_id, m.payload, m.is_encrypted, m.created_at,
                   p.name as partition_name, q.name as queue_name,
                   q.priority, q.lease_time, q.retry_limit, q.delayed_processing,
                   q.window_buffer, q.max_wait_time_seconds, q.namespace, q.task
            FROM queen.messages m
            JOIN queen.partitions p ON m.partition_id = p.id
            JOIN queen.queues q ON p.queue_id = q.id
            LEFT JOIN queen.messages_status ms ON m.id = ms.message_id AND ms.consumer_group = '__QUEUE_MODE__'
            WHERE (ms.id IS NULL OR ms.status = 'pending')
              AND m.created_at <= NOW() - INTERVAL '1 second' * q.delayed_processing
              AND (q.max_wait_time_seconds = 0 OR 
                   m.created_at > NOW() - INTERVAL '1 second' * q.max_wait_time_seconds)
              AND (q.window_buffer = 0 OR NOT EXISTS (
                SELECT 1 FROM queen.messages m2 
                WHERE m2.partition_id = p.id 
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
        query += ` ORDER BY q.priority DESC, m.created_at ASC LIMIT $${params.length} FOR UPDATE OF m SKIP LOCKED`;
        query += `)
          INSERT INTO queen.messages_status (message_id, consumer_group, status, lease_expires_at, processing_at, worker_id)
          SELECT id, '__QUEUE_MODE__', 'processing', NOW() + INTERVAL '1 second' * lease_time, NOW(), $${params.length + 1}
          FROM available_messages
          ON CONFLICT (message_id, consumer_group) 
          DO UPDATE SET 
            status = 'processing',
            lease_expires_at = EXCLUDED.lease_expires_at,
            processing_at = EXCLUDED.processing_at,
            worker_id = EXCLUDED.worker_id,
            retry_count = queen.messages_status.retry_count + 1
          RETURNING message_id,
            (SELECT transaction_id FROM queen.messages WHERE id = message_id),
            (SELECT trace_id FROM queen.messages WHERE id = message_id),
            (SELECT payload FROM queen.messages WHERE id = message_id),
            (SELECT is_encrypted FROM queen.messages WHERE id = message_id),
            (SELECT created_at FROM queen.messages WHERE id = message_id),
            (SELECT p.name FROM queen.messages m2 JOIN queen.partitions p ON m2.partition_id = p.id WHERE m2.id = message_id),
            (SELECT q.name FROM queen.messages m2 JOIN queen.partitions p ON m2.partition_id = p.id JOIN queen.queues q ON p.queue_id = q.id WHERE m2.id = message_id) as queue_name,
            (SELECT q.priority FROM queen.messages m2 JOIN queen.partitions p ON m2.partition_id = p.id JOIN queen.queues q ON p.queue_id = q.id WHERE m2.id = message_id) as priority,
            retry_count
        `;
        
        params.push(config.WORKER_ID);
        const result = await client.query(query, params);
        
        if (result.rows.length === 0 && wait) {
          // For namespace/task filtering, we can't easily wait on specific queue paths
          // So we'll use simple polling
          return { messages: [] };
        }
        
        // Format and decrypt messages
        const messages = await Promise.all(
          result.rows.map(async (row) => {
            let decryptedPayload = row.payload;
            
            if (row.is_encrypted && encryption.isEncryptionEnabled()) {
              try {
                decryptedPayload = await encryption.decryptPayload(row.payload);
              } catch (error) {
                log('ERROR: Decryption failed:', error);
              }
            }
            
            return {
              id: row.message_id,
              transactionId: row.transaction_id,
              traceId: row.trace_id,
              queue: row.queue_name,
              partition: row.partition_name,
              data: decryptedPayload,
              payload: decryptedPayload,
              retryCount: row.retry_count || 0,
              priority: row.priority || 0,
              createdAt: row.created_at
            };
          })
        );
        
        if (messages.length > 0) {
          log(`${LogTypes.POP} | Count: ${messages.length} | Namespace: ${namespace || 'ANY'} | Task: ${task || 'ANY'}`);
        }
        
        return { messages };
      });
    },
    configureQueue: async (queueName, options = {}, namespace = null, task = null) => {
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
          maxWaitTimeSeconds: 'max_wait_time_seconds',
          maxQueueSize: 'max_queue_size'
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
    }
  };
};
