-- ============================================================================
-- ack_messages_v2: Batch ACK with deadlock prevention
-- ============================================================================
-- NOTE: Using CREATE OR REPLACE (no DROP) for zero-downtime deployments

CREATE OR REPLACE FUNCTION queen.ack_messages_v2(
    p_acknowledgments JSONB
) RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_ack JSONB;
    v_results JSONB := '[]'::jsonb;
    v_message_id UUID;
    v_partition_id UUID;
    v_queue_name TEXT;
    v_partition_name TEXT;
    v_success BOOLEAN;
    v_error TEXT;
    v_consumer_group TEXT;
    v_message_created_at TIMESTAMPTZ;
    v_lease_id TEXT;
    v_status TEXT;
    v_lease_released BOOLEAN;
    v_batch_size INT;
    v_acked_count INT;
    v_retry_limit INT;
    v_batch_retry_count INT;
BEGIN
    -- Sort by partitionId + transactionId to prevent deadlocks
    FOR v_ack IN 
        SELECT value FROM jsonb_array_elements(p_acknowledgments)
        ORDER BY (value->>'partitionId'), (value->>'transactionId')
    LOOP
        v_success := false;
        v_error := NULL;
        v_lease_released := false;
        v_consumer_group := COALESCE(v_ack->>'consumerGroup', '__QUEUE_MODE__');
        v_partition_id := (v_ack->>'partitionId')::uuid;
        v_lease_id := v_ack->>'leaseId';
        v_status := COALESCE(v_ack->>'status', 'completed');
        
        SELECT m.id, q.name, p.name, m.created_at
        INTO v_message_id, v_queue_name, v_partition_name, v_message_created_at
        FROM queen.messages m
        JOIN queen.partitions p ON m.partition_id = p.id
        JOIN queen.queues q ON p.queue_id = q.id
        WHERE m.transaction_id = v_ack->>'transactionId'
          AND p.id = v_partition_id;
        
        IF v_message_id IS NULL THEN
            v_error := 'Message not found';
        ELSE
            IF v_lease_id IS NOT NULL AND v_lease_id != '' THEN
                IF NOT EXISTS (
                    SELECT 1 FROM queen.partition_consumers pc
                    WHERE pc.partition_id = v_partition_id
                      AND pc.consumer_group = v_consumer_group
                      AND pc.worker_id = v_lease_id
                      AND pc.lease_expires_at > NOW()
                ) THEN
                    v_error := 'Invalid or expired lease';
                END IF;
            END IF;
            
            IF v_error IS NULL THEN
                SELECT batch_size, acked_count, COALESCE(batch_retry_count, 0), COALESCE(q.retry_limit, 3)
                INTO v_batch_size, v_acked_count, v_batch_retry_count, v_retry_limit
                FROM queen.partition_consumers pc
                JOIN queen.partitions p ON p.id = pc.partition_id
                JOIN queen.queues q ON q.id = p.queue_id
                WHERE pc.partition_id = v_partition_id
                  AND pc.consumer_group = v_consumer_group;
                
                IF v_status = 'completed' OR v_status = 'success' THEN
                    UPDATE queen.partition_consumers 
                    SET last_consumed_id = v_message_id,
                        last_consumed_created_at = v_message_created_at,
                        last_consumed_at = NOW(),
                        total_messages_consumed = total_messages_consumed + 1,
                        acked_count = CASE 
                            WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                            THEN 0
                            ELSE acked_count + 1
                        END,
                        lease_expires_at = CASE 
                            WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                            THEN NULL 
                            ELSE lease_expires_at 
                        END,
                        lease_acquired_at = CASE 
                            WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                            THEN NULL 
                            ELSE lease_acquired_at 
                        END,
                        worker_id = CASE 
                            WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                            THEN NULL 
                            ELSE worker_id 
                        END,
                        batch_retry_count = CASE 
                            WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                            THEN 0 
                            ELSE batch_retry_count 
                        END
                    WHERE partition_id = v_partition_id
                      AND consumer_group = v_consumer_group
                    RETURNING (lease_expires_at IS NULL) INTO v_lease_released;
                    
                    INSERT INTO queen.messages_consumed (partition_id, consumer_group, messages_completed)
                    VALUES (v_partition_id, v_consumer_group, 1)
                    ON CONFLICT DO NOTHING;
                    
                    v_success := true;
                    
                ELSIF v_status = 'failed' OR v_status = 'dlq' THEN
                    IF v_batch_retry_count < v_retry_limit AND v_status != 'dlq' THEN
                        UPDATE queen.partition_consumers
                        SET lease_expires_at = NULL,
                            lease_acquired_at = NULL,
                            message_batch = NULL,
                            batch_size = 0,
                            acked_count = 0,
                            worker_id = NULL,
                            batch_retry_count = COALESCE(batch_retry_count, 0) + 1
                        WHERE partition_id = v_partition_id
                          AND consumer_group = v_consumer_group
                          AND worker_id IS NOT NULL;
                        
                        v_success := true;
                        v_lease_released := true;
                    ELSE
                        INSERT INTO queen.dead_letter_queue (
                            message_id, partition_id, consumer_group, error_message, 
                            retry_count, original_created_at
                        )
                        VALUES (
                            v_message_id, 
                            v_partition_id, 
                            v_consumer_group, 
                            COALESCE(v_ack->>'error', 'Retries exhausted'),
                            COALESCE(v_batch_retry_count, 0),
                            v_message_created_at
                        );
                        
                        UPDATE queen.partition_consumers 
                        SET last_consumed_id = v_message_id,
                            last_consumed_created_at = v_message_created_at,
                            last_consumed_at = NOW(),
                            lease_expires_at = CASE 
                                WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                                THEN NULL ELSE lease_expires_at END,
                            lease_acquired_at = CASE 
                                WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                                THEN NULL ELSE lease_acquired_at END,
                            worker_id = CASE 
                                WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                                THEN NULL ELSE worker_id END,
                            acked_count = CASE 
                                WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                                THEN 0 ELSE acked_count + 1 END
                        WHERE partition_id = v_partition_id
                          AND consumer_group = v_consumer_group;
                        
                        INSERT INTO queen.messages_consumed (partition_id, consumer_group, messages_failed)
                        VALUES (v_partition_id, v_consumer_group, 1)
                        ON CONFLICT DO NOTHING;
                        
                        v_success := true;
                    END IF;
                    
                ELSIF v_status = 'retry' THEN
                    UPDATE queen.partition_consumers
                    SET lease_expires_at = NULL,
                        lease_acquired_at = NULL,
                        worker_id = NULL
                    WHERE partition_id = v_partition_id
                      AND consumer_group = v_consumer_group;
                    
                    v_success := true;
                    v_lease_released := true;
                END IF;
            END IF;
        END IF;
        
        v_results := v_results || jsonb_build_object(
            'index', (v_ack->>'index')::int,
            'transactionId', v_ack->>'transactionId',
            'success', v_success,
            'error', v_error,
            'queueName', v_queue_name,
            'partitionName', v_partition_name,
            'leaseReleased', v_lease_released
        );
    END LOOP;
    
    SELECT COALESCE(jsonb_agg(item ORDER BY (item->>'index')::int), '[]'::jsonb)
    INTO v_results
    FROM jsonb_array_elements(v_results) item;
    
    RETURN v_results;
END;
$$;

GRANT EXECUTE ON FUNCTION queen.ack_messages_v2(jsonb) TO PUBLIC;

