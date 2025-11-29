-- Migration: Add execute_transaction_v2 stored procedure
-- For high-performance async transaction operations via sidecar pattern
--
-- Queen Architecture Notes:
-- - Messages table is immutable (no status column)
-- - Cursor tracked via partition_consumers
-- - Leases at partition level

-- Drop if exists (for development iteration)
DROP FUNCTION IF EXISTS queen.execute_transaction_v2(jsonb);

-- ============================================================================
-- execute_transaction_v2: Atomic transaction execution
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.execute_transaction_v2(
    p_operations JSONB
    -- Push: {type: "push", queue, partition, payload, transactionId, traceId, messageId}
    -- Ack:  {type: "ack", transactionId, partitionId, status, consumerGroup, leaseId, error}
) RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_op JSONB;
    v_results JSONB := '[]'::jsonb;
    v_transaction_id TEXT;
    v_idx INT := 0;
    v_op_success BOOLEAN;
    v_op_error TEXT;
    
    -- For push operations
    v_queue_id UUID;
    v_partition_id UUID;
    v_queue_name TEXT;
    v_partition_name TEXT;
    v_namespace TEXT;
    v_task TEXT;
    v_message_id UUID;
    v_txn_id TEXT;
    v_payload JSONB;
    v_trace_id UUID;
    
    -- For ack operations
    v_ack_message_id UUID;
    v_consumer_group TEXT;
    v_message_created_at TIMESTAMPTZ;
    v_lease_id TEXT;
    v_status TEXT;
BEGIN
    -- Generate transaction ID for this atomic operation
    v_transaction_id := gen_random_uuid()::text;
    
    FOR v_op IN SELECT * FROM jsonb_array_elements(p_operations)
    LOOP
        v_op_success := false;
        v_op_error := NULL;
        
        CASE v_op->>'type'
            WHEN 'push' THEN
                -- Extract push parameters
                v_queue_name := v_op->>'queue';
                v_partition_name := COALESCE(v_op->>'partition', 'Default');
                v_txn_id := COALESCE(v_op->>'transactionId', gen_random_uuid()::text);
                v_payload := COALESCE(v_op->'payload', v_op->'data', '{}'::jsonb);
                v_message_id := COALESCE((v_op->>'messageId')::uuid, gen_random_uuid());
                v_namespace := COALESCE(v_op->>'namespace', split_part(v_queue_name, '.', 1));
                v_task := COALESCE(v_op->>'task', 
                                   CASE WHEN position('.' in v_queue_name) > 0 
                                        THEN split_part(v_queue_name, '.', 2) 
                                        ELSE '' END);
                
                -- Parse trace_id
                IF v_op->>'traceId' IS NOT NULL AND v_op->>'traceId' != '' THEN
                    v_trace_id := (v_op->>'traceId')::uuid;
                ELSE
                    v_trace_id := NULL;
                END IF;
                
                -- Ensure queue exists
                INSERT INTO queen.queues (name, namespace, task)
                VALUES (v_queue_name, v_namespace, v_task)
                ON CONFLICT (name) DO UPDATE SET name = EXCLUDED.name
                RETURNING id INTO v_queue_id;
                
                -- Ensure partition exists
                INSERT INTO queen.partitions (queue_id, name)
                VALUES (v_queue_id, v_partition_name)
                ON CONFLICT (queue_id, name) DO UPDATE SET name = EXCLUDED.name
                RETURNING id INTO v_partition_id;
                
                -- Insert message
                INSERT INTO queen.messages (id, transaction_id, partition_id, payload, trace_id)
                VALUES (v_message_id, v_txn_id, v_partition_id, v_payload, v_trace_id);
                
                -- Update partition lookup for fast pop queries
                -- NOTE: Unique constraint is on (queue_name, partition_id)
                INSERT INTO queen.partition_lookup (partition_id, queue_name, last_message_id, last_message_created_at)
                VALUES (v_partition_id, v_queue_name, v_message_id, NOW())
                ON CONFLICT (queue_name, partition_id) DO UPDATE SET
                    last_message_id = EXCLUDED.last_message_id,
                    last_message_created_at = EXCLUDED.last_message_created_at;
                
                v_op_success := true;
                
                v_results := v_results || jsonb_build_object(
                    'index', v_idx,
                    'type', 'push',
                    'success', v_op_success,
                    'transactionId', v_txn_id,
                    'messageId', v_message_id::text,
                    'partitionId', v_partition_id::text
                );
                
            WHEN 'ack' THEN
                v_consumer_group := COALESCE(v_op->>'consumerGroup', '__QUEUE_MODE__');
                v_partition_id := (v_op->>'partitionId')::uuid;
                v_lease_id := v_op->>'leaseId';
                v_status := COALESCE(v_op->>'status', 'completed');
                
                -- Find the message
                SELECT m.id, m.created_at
                INTO v_ack_message_id, v_message_created_at
                FROM queen.messages m
                WHERE m.transaction_id = v_op->>'transactionId'
                  AND m.partition_id = v_partition_id;
                
                IF v_ack_message_id IS NULL THEN
                    v_op_error := 'Message not found';
                ELSE
                    -- Validate lease if provided
                    IF v_lease_id IS NOT NULL AND v_lease_id != '' THEN
                        IF NOT EXISTS (
                            SELECT 1 FROM queen.partition_consumers pc
                            WHERE pc.partition_id = v_partition_id
                              AND pc.consumer_group = v_consumer_group
                              AND pc.worker_id = v_lease_id
                              AND pc.lease_expires_at > NOW()
                        ) THEN
                            v_op_error := 'Invalid or expired lease';
                        END IF;
                    END IF;
                    
                    IF v_op_error IS NULL THEN
                        IF v_status IN ('completed', 'success') THEN
                            -- Update cursor and handle batch/lease tracking
                            -- If batch is complete (acked_count + 1 >= batch_size), release the lease
                            UPDATE queen.partition_consumers
                            SET last_consumed_id = v_ack_message_id,
                                last_consumed_created_at = v_message_created_at,
                                last_consumed_at = NOW(),
                                total_messages_consumed = COALESCE(total_messages_consumed, 0) + 1,
                                acked_count = COALESCE(acked_count, 0) + 1,
                                -- Release lease if batch is complete
                                lease_expires_at = CASE 
                                    WHEN COALESCE(acked_count, 0) + 1 >= COALESCE(batch_size, 1) 
                                    THEN NULL ELSE lease_expires_at END,
                                lease_acquired_at = CASE 
                                    WHEN COALESCE(acked_count, 0) + 1 >= COALESCE(batch_size, 1) 
                                    THEN NULL ELSE lease_acquired_at END,
                                worker_id = CASE 
                                    WHEN COALESCE(acked_count, 0) + 1 >= COALESCE(batch_size, 1) 
                                    THEN NULL ELSE worker_id END,
                                batch_size = CASE 
                                    WHEN COALESCE(acked_count, 0) + 1 >= COALESCE(batch_size, 1) 
                                    THEN 0 ELSE batch_size END
                            WHERE partition_id = v_partition_id
                              AND consumer_group = v_consumer_group;
                            
                            -- If no row exists yet, insert a new one
                            IF NOT FOUND THEN
                                INSERT INTO queen.partition_consumers (
                                    partition_id, consumer_group, last_consumed_id, 
                                    last_consumed_created_at, last_consumed_at, total_messages_consumed
                                )
                                VALUES (v_partition_id, v_consumer_group, v_ack_message_id, 
                                        v_message_created_at, NOW(), 1);
                            END IF;
                            
                            v_op_success := true;
                        ELSIF v_status IN ('failed', 'dlq') THEN
                            -- Move to DLQ
                            INSERT INTO queen.dead_letter_queue (
                                message_id, partition_id, consumer_group, error_message, 
                                retry_count, original_created_at
                            )
                            VALUES (
                                v_ack_message_id, 
                                v_partition_id, 
                                v_consumer_group, 
                                COALESCE(v_op->>'error', 'Unknown error'),
                                0,
                                v_message_created_at
                            );
                            
                            -- Update cursor and release lease (failed = batch done)
                            UPDATE queen.partition_consumers
                            SET last_consumed_id = v_ack_message_id,
                                last_consumed_created_at = v_message_created_at,
                                last_consumed_at = NOW(),
                                -- Release lease on failure
                                lease_expires_at = NULL,
                                lease_acquired_at = NULL,
                                worker_id = NULL,
                                batch_size = 0,
                                acked_count = 0
                            WHERE partition_id = v_partition_id
                              AND consumer_group = v_consumer_group;
                            
                            -- If no row exists yet, insert a new one
                            IF NOT FOUND THEN
                                INSERT INTO queen.partition_consumers (
                                    partition_id, consumer_group, last_consumed_id, 
                                    last_consumed_created_at, last_consumed_at
                                )
                                VALUES (v_partition_id, v_consumer_group, v_ack_message_id, 
                                        v_message_created_at, NOW());
                            END IF;
                            
                            v_op_success := true;
                        END IF;
                    END IF;
                END IF;
                
                v_results := v_results || jsonb_build_object(
                    'index', v_idx,
                    'type', 'ack',
                    'success', v_op_success,
                    'transactionId', v_op->>'transactionId',
                    'error', v_op_error
                );
                
                -- ATOMIC: If ACK failed, rollback the entire transaction
                IF NOT v_op_success THEN
                    RAISE EXCEPTION 'ACK operation failed: %', COALESCE(v_op_error, 'Unknown error');
                END IF;
                
            ELSE
                v_results := v_results || jsonb_build_object(
                    'index', v_idx,
                    'type', v_op->>'type',
                    'success', false,
                    'error', 'Unknown operation type: ' || COALESCE(v_op->>'type', 'null')
                );
                
                -- ATOMIC: Unknown operation type should fail the transaction
                RAISE EXCEPTION 'Unknown operation type: %', COALESCE(v_op->>'type', 'null');
        END CASE;
        
        v_idx := v_idx + 1;
    END LOOP;
    
    RETURN jsonb_build_object(
        'transactionId', v_transaction_id,
        'success', true,
        'results', v_results
    );
    
EXCEPTION WHEN OTHERS THEN
    -- Transaction auto-rollbacks
    RETURN jsonb_build_object(
        'transactionId', v_transaction_id,
        'success', false,
        'error', SQLERRM,
        'results', v_results
    );
END;
$$;

-- Grant execute permissions
GRANT EXECUTE ON FUNCTION queen.execute_transaction_v2(jsonb) TO PUBLIC;

COMMENT ON FUNCTION queen.execute_transaction_v2 IS 
'Atomic transaction execution for push and ack operations.
All operations succeed or fail together within a single database transaction.

Architecture:
- Messages table is immutable
- Cursor tracked via partition_consumers
- Updates partition_lookup for fast pop queries

Parameters:
  p_operations: Array of operation objects:
    Push: {type: "push", queue, partition, payload, transactionId, traceId, messageId}
    Ack: {type: "ack", transactionId, partitionId, status, consumerGroup, leaseId, error}

Returns: {
  transactionId: UUID for this transaction batch,
  success: true if all operations succeeded,
  error: error message if failed,
  results: Array of per-operation results
}

Note: This function does NOT support micro-batching (each transaction is atomic).
';
