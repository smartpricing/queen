import config from '../config.js';
import { v4 as uuidv4 } from 'uuid';
import * as encryption from '../services/encryptionService.js';
import { log, LogTypes } from '../utils/logger.js';

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
export const popMessagesV2 = async (scope, options = {}, pool, withTransaction) => {
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
    // Check if another consumer has an active lease
    const leaseCheckResult = await client.query(`
      SELECT * FROM queen.partition_leases
      WHERE partition_id = $1 
        AND consumer_group = $2
        AND released_at IS NULL
        AND lease_expires_at > NOW()
      FOR UPDATE NOWAIT
    `, [partitionInfo.id, actualConsumerGroup]).catch(err => {
      if (err.code === '55P03') { // Lock not available
        return { rows: [{ locked: true }] };
      }
      throw err;
    });
    
    if (leaseCheckResult.rows.length > 0 && leaseCheckResult.rows[0].locked) {
      // Another consumer has the partition locked
      return { messages: [] };
    }
    
    // Acquire or update lease
    await client.query(`
      INSERT INTO queen.partition_leases (
        partition_id, 
        consumer_group, 
        lease_expires_at
      ) VALUES ($1, $2, NOW() + INTERVAL '1 second' * $3)
      ON CONFLICT (partition_id, consumer_group) 
      DO UPDATE SET 
        lease_expires_at = EXCLUDED.lease_expires_at,
        released_at = NULL
    `, [partitionInfo.id, actualConsumerGroup, queueInfo.lease_time]);
    
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
