-- wrappers.sql
-- Simplified wrapper functions for pg_qpubsub
-- These wrap the core Queen procedures with a Kafka-style API
-- Requires: pgcrypto extension (for gen_random_bytes)
--
-- Naming convention (Kafka-style):
--   produce  - send messages to a queue (Kafka: producer.send)
--   consume  - receive messages from a queue (Kafka: consumer.poll)
--   commit   - acknowledge message processing (Kafka: consumer.commitSync)
--   renew    - extend message lease
--   transaction - atomic multi-operation

-- ============================================================================
-- UUID V7 GENERATION (time-sortable UUIDs per RFC 9562)
-- ============================================================================
-- UUID v7 format (128 bits / 16 bytes):
--   Bytes 0-5:  48-bit timestamp (milliseconds since Unix epoch)
--   Byte 6:     version (high 4 bits = 0111) + sequence (high 4 bits)
--   Byte 7:     sequence (low 8 bits)
--   Byte 8:     variant (high 2 bits = 10) + random (low 6 bits)
--   Bytes 9-15: random
--
-- This implementation uses a SEQUENCE COUNTER (like libqueen C++) to guarantee
-- strict ordering even for UUIDs generated in the same millisecond.
-- ============================================================================

-- UUID v7 generator with sequence counter (matches libqueen C++ behavior)
CREATE OR REPLACE FUNCTION queen.uuid_generate_v7()
RETURNS uuid
LANGUAGE plpgsql
VOLATILE
AS $$
DECLARE
    v_now_ms BIGINT;
    v_last_ms BIGINT;
    v_seq INT;
    v_timestamp_bytes bytea;
    v_uuid_bytes bytea;
BEGIN
    -- Get current milliseconds since epoch
    v_now_ms := (extract(epoch from clock_timestamp()) * 1000)::bigint;
    
    -- Get last timestamp and sequence from session variables
    BEGIN
        v_last_ms := current_setting('queen.uuid_last_ms', true)::bigint;
        v_seq := current_setting('queen.uuid_seq', true)::int;
    EXCEPTION WHEN OTHERS THEN
        v_last_ms := 0;
        v_seq := 0;
    END;
    
    -- Update sequence: increment if same ms, reset if new ms
    IF v_now_ms <= v_last_ms THEN
        v_seq := v_seq + 1;
        -- Use last_ms to ensure monotonic timestamps
        v_now_ms := v_last_ms;
    ELSE
        v_seq := 0;
        v_last_ms := v_now_ms;
    END IF;
    
    -- Wrap sequence at 12 bits (0-4095)
    IF v_seq > 4095 THEN
        -- Overflow: advance timestamp by 1ms and reset sequence
        v_now_ms := v_now_ms + 1;
        v_last_ms := v_now_ms;
        v_seq := 0;
    END IF;
    
    -- Store state in session variables
    PERFORM set_config('queen.uuid_last_ms', v_last_ms::text, false);
    PERFORM set_config('queen.uuid_seq', v_seq::text, false);
    
    -- Build UUID bytes
    -- Bytes 0-5: 48-bit timestamp (big-endian)
    v_timestamp_bytes := decode(lpad(to_hex(v_now_ms), 12, '0'), 'hex');
    
    -- Bytes 6-15: version + sequence + variant + random
    v_uuid_bytes := v_timestamp_bytes || gen_random_bytes(10);
    
    -- Byte 6: version (0111) + high 4 bits of sequence
    v_uuid_bytes := set_byte(v_uuid_bytes, 6, 112 | ((v_seq >> 8) & 15));
    
    -- Byte 7: low 8 bits of sequence
    v_uuid_bytes := set_byte(v_uuid_bytes, 7, v_seq & 255);
    
    -- Byte 8: variant (10) + random (keep low 6 bits from random)
    v_uuid_bytes := set_byte(v_uuid_bytes, 8, (get_byte(v_uuid_bytes, 8) & 63) | 128);
    
    RETURN encode(v_uuid_bytes, 'hex')::uuid;
END;
$$;

-- Extract timestamp from UUID v7
CREATE OR REPLACE FUNCTION queen.uuid_v7_to_timestamptz(p_uuid uuid)
RETURNS timestamptz
LANGUAGE sql
IMMUTABLE
AS $$
    SELECT to_timestamp(
        ('x' || substr(replace($1::text, '-', ''), 1, 12))::bit(48)::bigint / 1000.0
    );
$$;

-- UUID v7 boundary for range queries (NOT for unique IDs - has no randomness)
-- Use for: WHERE id >= queen.uuid_v7_boundary('2024-01-01')
-- Do NOT use for: INSERT ... VALUES (queen.uuid_v7_boundary(...))
CREATE OR REPLACE FUNCTION queen.uuid_v7_boundary(p_time timestamptz)
RETURNS uuid
LANGUAGE sql
IMMUTABLE
AS $$
    SELECT (
        lpad(to_hex((extract(epoch from p_time) * 1000)::bigint), 12, '0') ||
        '7000-8000-000000000000'
    )::uuid;
$$;

-- UUID v7 at specific timestamp WITH randomness (safe for inserts/backfills)
CREATE OR REPLACE FUNCTION queen.uuid_generate_v7_at(p_time timestamptz)
RETURNS uuid
LANGUAGE plpgsql
VOLATILE
AS $$
DECLARE
    v_timestamp_bytes bytea;
    v_uuid_bytes bytea;
BEGIN
    v_timestamp_bytes := decode(
        lpad(to_hex((extract(epoch from p_time) * 1000)::bigint), 12, '0'), 
        'hex'
    );
    
    v_uuid_bytes := v_timestamp_bytes || gen_random_bytes(10);
    v_uuid_bytes := set_byte(v_uuid_bytes, 6, (get_byte(v_uuid_bytes, 6) & 15) | 112);
    v_uuid_bytes := set_byte(v_uuid_bytes, 8, (get_byte(v_uuid_bytes, 8) & 63) | 128);
    
    RETURN encode(v_uuid_bytes, 'hex')::uuid;
END;
$$;

-- ============================================================================
-- PRODUCE WRAPPERS (formerly push)
-- ============================================================================

-- Simple produce: queue + payload
CREATE OR REPLACE FUNCTION queen.produce(
    p_queue TEXT,
    p_payload JSONB,
    p_transaction_id TEXT DEFAULT NULL
)
RETURNS UUID
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
    v_txn_id TEXT;
BEGIN
    v_txn_id := COALESCE(p_transaction_id, queen.uuid_generate_v7()::text);
    
    v_result := queen.push_messages_v2(jsonb_build_array(jsonb_build_object(
        'queue', p_queue,
        'partition', 'Default',
        'transactionId', v_txn_id,
        'payload', p_payload
    )));
    
    RETURN (v_result->0->>'message_id')::uuid;
END;
$$;

-- Produce with partition
CREATE OR REPLACE FUNCTION queen.produce(
    p_queue TEXT,
    p_partition TEXT,
    p_payload JSONB,
    p_transaction_id TEXT DEFAULT NULL
)
RETURNS UUID
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
    v_txn_id TEXT;
BEGIN
    v_txn_id := COALESCE(p_transaction_id, queen.uuid_generate_v7()::text);
    
    v_result := queen.push_messages_v2(jsonb_build_array(jsonb_build_object(
        'queue', p_queue,
        'partition', p_partition,
        'transactionId', v_txn_id,
        'payload', p_payload
    )));
    
    RETURN (v_result->0->>'message_id')::uuid;
END;
$$;

-- Full produce with all options
CREATE OR REPLACE FUNCTION queen.produce_full(
    p_queue TEXT,
    p_payload JSONB,
    p_partition TEXT DEFAULT 'Default',
    p_transaction_id TEXT DEFAULT NULL,
    p_namespace TEXT DEFAULT NULL,
    p_task TEXT DEFAULT NULL,
    p_delay_until TIMESTAMPTZ DEFAULT NULL
)
RETURNS TABLE(message_id UUID, transaction_id TEXT)
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
    v_txn_id TEXT;
    v_msg JSONB;
BEGIN
    v_txn_id := COALESCE(p_transaction_id, queen.uuid_generate_v7()::text);
    
    v_msg := jsonb_build_object(
        'queue', p_queue,
        'partition', p_partition,
        'transactionId', v_txn_id,
        'payload', p_payload
    );
    
    IF p_namespace IS NOT NULL THEN
        v_msg := v_msg || jsonb_build_object('namespace', p_namespace);
    END IF;
    
    IF p_task IS NOT NULL THEN
        v_msg := v_msg || jsonb_build_object('task', p_task);
    END IF;
    
    IF p_delay_until IS NOT NULL THEN
        v_msg := v_msg || jsonb_build_object('delayUntil', p_delay_until);
    END IF;
    
    v_result := queen.push_messages_v2(jsonb_build_array(v_msg));
    
    RETURN QUERY SELECT 
        (v_result->0->>'message_id')::uuid,
        v_result->0->>'transaction_id';
END;
$$;

-- ============================================================================
-- CONSUME WRAPPERS (formerly pop)
-- ============================================================================

-- Simple consume: queue + consumer_group (discovers any available partition)
CREATE OR REPLACE FUNCTION queen.consume(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__',
    p_batch_size INTEGER DEFAULT 1,
    p_lease_seconds INTEGER DEFAULT 60
)
RETURNS TABLE(
    partition_id UUID,
    id UUID,
    transaction_id TEXT,
    payload JSONB,
    created_at TIMESTAMPTZ,
    lease_id TEXT
)
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
    v_batch JSONB;
    v_lease_id TEXT;
    v_partition_id UUID;
BEGIN
    -- pop_unified_batch expects an array of requests
    v_result := queen.pop_unified_batch(jsonb_build_array(jsonb_build_object(
        'idx', 0,
        'queue_name', p_queue,
        'consumer_group', p_consumer_group,
        'batch_size', p_batch_size,
        'lease_seconds', p_lease_seconds,
        'worker_id', 'wrapper-' || gen_random_uuid()::text
    )));
    
    -- Result is array: [{"idx": 0, "result": {...}}]
    v_batch := v_result->0->'result';
    v_lease_id := v_batch->>'leaseId';
    v_partition_id := (v_batch->>'partitionId')::uuid;
    
    IF v_batch->>'success' = 'true' THEN
        RETURN QUERY
        SELECT 
            v_partition_id,
            (m->>'id')::uuid,
            m->>'transactionId',
            m->'data',
            (m->>'createdAt')::timestamptz,
            v_lease_id
        FROM jsonb_array_elements(COALESCE(v_batch->'messages', '[]'::jsonb)) m;
    END IF;
END;
$$;

-- Consume from specific partition
CREATE OR REPLACE FUNCTION queen.consume(
    p_queue TEXT,
    p_partition TEXT,
    p_consumer_group TEXT,
    p_batch_size INTEGER,
    p_lease_seconds INTEGER DEFAULT 60
)
RETURNS TABLE(
    partition_id UUID,
    id UUID,
    transaction_id TEXT,
    payload JSONB,
    created_at TIMESTAMPTZ,
    lease_id TEXT
)
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
    v_batch JSONB;
    v_lease_id TEXT;
    v_partition_id UUID;
BEGIN
    v_result := queen.pop_unified_batch(jsonb_build_array(jsonb_build_object(
        'idx', 0,
        'queue_name', p_queue,
        'partition_name', p_partition,
        'consumer_group', p_consumer_group,
        'batch_size', p_batch_size,
        'lease_seconds', p_lease_seconds,
        'worker_id', 'wrapper-' || gen_random_uuid()::text
    )));
    
    v_batch := v_result->0->'result';
    v_lease_id := v_batch->>'leaseId';
    v_partition_id := (v_batch->>'partitionId')::uuid;
    
    IF v_batch->>'success' = 'true' THEN
        RETURN QUERY
        SELECT 
            v_partition_id,
            (m->>'id')::uuid,
            m->>'transactionId',
            m->'data',
            (m->>'createdAt')::timestamptz,
            v_lease_id
        FROM jsonb_array_elements(COALESCE(v_batch->'messages', '[]'::jsonb)) m;
    END IF;
END;
$$;

-- Consume batch from queue (convenience for queue mode with batch size)
CREATE OR REPLACE FUNCTION queen.consume_batch(
    p_queue TEXT,
    p_batch_size INTEGER,
    p_lease_seconds INTEGER DEFAULT 60
)
RETURNS TABLE(
    partition_id UUID,
    id UUID,
    transaction_id TEXT,
    payload JSONB,
    created_at TIMESTAMPTZ,
    lease_id TEXT
)
LANGUAGE sql
AS $$
    SELECT * FROM queen.consume(p_queue, '__QUEUE_MODE__', p_batch_size, p_lease_seconds);
$$;

-- Consume one message
CREATE OR REPLACE FUNCTION queen.consume_one(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__',
    p_lease_seconds INTEGER DEFAULT 60
)
RETURNS TABLE(
    partition_id UUID,
    id UUID,
    transaction_id TEXT,
    payload JSONB,
    created_at TIMESTAMPTZ,
    lease_id TEXT
)
LANGUAGE sql
AS $$
    SELECT * FROM queen.consume(p_queue, p_consumer_group, 1, p_lease_seconds);
$$;

-- Consume with auto-commit (fire-and-forget)
CREATE OR REPLACE FUNCTION queen.consume_auto_commit(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__',
    p_batch_size INTEGER DEFAULT 1
)
RETURNS TABLE(
    partition_id UUID,
    id UUID,
    transaction_id TEXT,
    payload JSONB,
    created_at TIMESTAMPTZ,
    lease_id TEXT
)
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
    v_batch JSONB;
    v_partition_id UUID;
BEGIN
    v_result := queen.pop_unified_batch(jsonb_build_array(jsonb_build_object(
        'idx', 0,
        'queue_name', p_queue,
        'consumer_group', p_consumer_group,
        'batch_size', p_batch_size,
        'lease_seconds', 1,
        'auto_ack', true,
        'worker_id', 'wrapper-' || gen_random_uuid()::text
    )));
    
    v_batch := v_result->0->'result';
    v_partition_id := (v_batch->>'partitionId')::uuid;
    
    IF v_batch->>'success' = 'true' THEN
        RETURN QUERY
        SELECT 
            v_partition_id,
            (m->>'id')::uuid,
            m->>'transactionId',
            m->'data',
            (m->>'createdAt')::timestamptz,
            v_batch->>'leaseId'
        FROM jsonb_array_elements(COALESCE(v_batch->'messages', '[]'::jsonb)) m;
    END IF;
END;
$$;

-- ============================================================================
-- COMMIT WRAPPERS (formerly ack)
-- ============================================================================

-- Commit success (simple 3-arg version for queue mode)
CREATE OR REPLACE FUNCTION queen.commit(
    p_transaction_id TEXT,
    p_partition_id UUID,
    p_lease_id TEXT
)
RETURNS BOOLEAN
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
    v_item JSONB;
BEGIN
    v_result := queen.ack_messages_v2(jsonb_build_array(jsonb_build_object(
        'index', 0,
        'transactionId', p_transaction_id,
        'partitionId', p_partition_id::text,
        'leaseId', p_lease_id,
        'consumerGroup', '__QUEUE_MODE__',
        'status', 'completed'
    )));
    
    v_item := v_result->0;
    RETURN COALESCE((v_item->>'success')::boolean, false);
END;
$$;

-- Commit with explicit consumer group (4-arg version for pubsub mode)
CREATE OR REPLACE FUNCTION queen.commit(
    p_transaction_id TEXT,
    p_partition_id UUID,
    p_lease_id TEXT,
    p_consumer_group TEXT
)
RETURNS BOOLEAN
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
    v_item JSONB;
BEGIN
    v_result := queen.ack_messages_v2(jsonb_build_array(jsonb_build_object(
        'index', 0,
        'transactionId', p_transaction_id,
        'partitionId', p_partition_id::text,
        'leaseId', p_lease_id,
        'consumerGroup', p_consumer_group,
        'status', 'completed'
    )));
    
    v_item := v_result->0;
    RETURN COALESCE((v_item->>'success')::boolean, false);
END;
$$;

-- Commit with explicit status (full control)
CREATE OR REPLACE FUNCTION queen.commit(
    p_transaction_id TEXT,
    p_partition_id UUID,
    p_lease_id TEXT,
    p_status TEXT,
    p_consumer_group TEXT,
    p_error_message TEXT DEFAULT NULL
)
RETURNS BOOLEAN
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
    v_item JSONB;
BEGIN
    v_result := queen.ack_messages_v2(jsonb_build_array(jsonb_build_object(
        'index', 0,
        'transactionId', p_transaction_id,
        'partitionId', p_partition_id::text,
        'leaseId', p_lease_id,
        'consumerGroup', p_consumer_group,
        'status', p_status,
        'error', p_error_message
    )));
    
    v_item := v_result->0;
    RETURN COALESCE((v_item->>'success')::boolean, false);
END;
$$;

-- NACK: Mark as failed (will retry)
CREATE OR REPLACE FUNCTION queen.nack(
    p_transaction_id TEXT,
    p_partition_id UUID,
    p_lease_id TEXT,
    p_error_message TEXT DEFAULT 'Processing failed',
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__'
)
RETURNS BOOLEAN
LANGUAGE sql
AS $$
    SELECT queen.commit(
        p_transaction_id,
        p_partition_id,
        p_lease_id,
        'failed',
        p_consumer_group,
        p_error_message
    );
$$;

-- REJECT: Send directly to DLQ
CREATE OR REPLACE FUNCTION queen.reject(
    p_transaction_id TEXT,
    p_partition_id UUID,
    p_lease_id TEXT,
    p_error_message TEXT DEFAULT 'Rejected',
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__'
)
RETURNS BOOLEAN
LANGUAGE sql
AS $$
    SELECT queen.commit(
        p_transaction_id,
        p_partition_id,
        p_lease_id,
        'dlq',
        p_consumer_group,
        p_error_message
    );
$$;

-- ============================================================================
-- LEASE WRAPPERS
-- ============================================================================

-- Renew lease
CREATE OR REPLACE FUNCTION queen.renew(
    p_lease_id TEXT,
    p_extend_seconds INTEGER DEFAULT 60
)
RETURNS TIMESTAMPTZ
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
    v_item JSONB;
BEGIN
    v_result := queen.renew_lease_v2(jsonb_build_array(jsonb_build_object(
        'index', 0,
        'leaseId', p_lease_id,
        'extendSeconds', p_extend_seconds
    )));
    
    v_item := v_result->0;
    
    IF COALESCE((v_item->>'success')::boolean, false) THEN
        RETURN (v_item->>'expiresAt')::timestamptz;
    ELSE
        RETURN NULL;
    END IF;
END;
$$;

-- ============================================================================
-- TRANSACTION WRAPPERS
-- ============================================================================

-- Forward: Commit source + Produce to destination atomically
CREATE OR REPLACE FUNCTION queen.forward(
    p_source_transaction_id TEXT,
    p_source_partition_id UUID,
    p_source_lease_id TEXT,
    p_source_consumer_group TEXT,
    p_dest_queue TEXT,
    p_dest_payload JSONB,
    p_dest_partition TEXT DEFAULT 'Default',
    p_dest_transaction_id TEXT DEFAULT NULL
)
RETURNS UUID
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
    v_dest_txn_id TEXT;
BEGIN
    v_dest_txn_id := COALESCE(p_dest_transaction_id, queen.uuid_generate_v7()::text);
    
    -- execute_transaction_v2 expects an array of operations with 'type' field
    v_result := queen.execute_transaction_v2(jsonb_build_array(
        -- Ack operation (commit the source)
        jsonb_build_object(
            'type', 'ack',
            'transactionId', p_source_transaction_id,
            'partitionId', p_source_partition_id::text,
            'leaseId', p_source_lease_id,
            'consumerGroup', p_source_consumer_group,
            'status', 'completed'
        ),
        -- Push operation (produce to destination)
        jsonb_build_object(
            'type', 'push',
            'queue', p_dest_queue,
            'partition', p_dest_partition,
            'transactionId', v_dest_txn_id,
            'payload', p_dest_payload
        )
    ));
    
    -- Result format: [{"idx": 0, "success": true/false, ...}]
    -- Find the push result (idx 1)
    RETURN (v_result->1->>'messageId')::uuid;
END;
$$;

-- Simplified forward (uses __QUEUE_MODE__ for consumer group)
CREATE OR REPLACE FUNCTION queen.forward(
    p_source_transaction_id TEXT,
    p_source_partition_id UUID,
    p_source_lease_id TEXT,
    p_dest_queue TEXT,
    p_dest_payload JSONB,
    p_dest_partition TEXT DEFAULT 'Default',
    p_dest_transaction_id TEXT DEFAULT NULL
)
RETURNS UUID
LANGUAGE sql
AS $$
    SELECT queen.forward(
        p_source_transaction_id,
        p_source_partition_id,
        p_source_lease_id,
        '__QUEUE_MODE__',
        p_dest_queue,
        p_dest_payload,
        p_dest_partition,
        p_dest_transaction_id
    );
$$;

-- Execute transaction (wrapper for execute_transaction_v2)
-- Operations format: [{"type": "push"|"ack", ...}, ...]
CREATE OR REPLACE FUNCTION queen.transaction(
    p_operations JSONB
)
RETURNS JSONB
LANGUAGE sql
AS $$
    SELECT queen.execute_transaction_v2(p_operations);
$$;

-- ============================================================================
-- UTILITY WRAPPERS
-- ============================================================================

-- Check if queue has pending messages
CREATE OR REPLACE FUNCTION queen.has_messages(
    p_queue TEXT,
    p_partition TEXT DEFAULT NULL,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__'
)
RETURNS BOOLEAN
LANGUAGE sql
AS $$
    SELECT queen.has_pending_messages(p_queue, p_partition, p_consumer_group);
$$;

-- Get queue depth (approximate) - also known as "lag" in Kafka
CREATE OR REPLACE FUNCTION queen.depth(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__'
)
RETURNS BIGINT
LANGUAGE plpgsql
AS $$
DECLARE
    v_count BIGINT;
BEGIN
    SELECT COUNT(*)
    INTO v_count
    FROM queen.messages m
    JOIN queen.partitions p ON m.partition_id = p.id
    JOIN queen.queues q ON p.queue_id = q.id
    LEFT JOIN queen.partition_consumers pc 
        ON pc.partition_id = p.id 
        AND pc.consumer_group = p_consumer_group
    WHERE q.name = p_queue
      AND (pc.last_consumed_created_at IS NULL 
           OR (m.created_at, m.id) > (pc.last_consumed_created_at, pc.last_consumed_id));
    
    RETURN COALESCE(v_count, 0);
END;
$$;

-- Alias: lag (Kafka terminology for unconsumed messages)
CREATE OR REPLACE FUNCTION queen.lag(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__'
)
RETURNS BIGINT
LANGUAGE sql
AS $$
    SELECT queen.depth(p_queue, p_consumer_group);
$$;

-- Configure queue
CREATE OR REPLACE FUNCTION queen.configure(
    p_queue TEXT,
    p_lease_time INTEGER DEFAULT 300,
    p_retry_limit INTEGER DEFAULT 3,
    p_dead_letter_queue BOOLEAN DEFAULT FALSE
)
RETURNS BOOLEAN
LANGUAGE plpgsql
AS $$
DECLARE
    v_queue_id UUID;
BEGIN
    INSERT INTO queen.queues (name, lease_time, retry_limit, dead_letter_queue)
    VALUES (p_queue, p_lease_time, p_retry_limit, p_dead_letter_queue)
    ON CONFLICT (name) DO UPDATE SET
        lease_time = EXCLUDED.lease_time,
        retry_limit = EXCLUDED.retry_limit,
        dead_letter_queue = EXCLUDED.dead_letter_queue
    RETURNING id INTO v_queue_id;
    
    -- Create default partition
    INSERT INTO queen.partitions (queue_id, name)
    VALUES (v_queue_id, 'Default')
    ON CONFLICT (queue_id, name) DO NOTHING;
    
    RETURN TRUE;
END;
$$;

-- ============================================================================
-- NOTIFY/LISTEN SUPPORT
-- ============================================================================

-- Get channel name for a queue (sanitized for NOTIFY)
CREATE OR REPLACE FUNCTION queen.channel_name(p_queue TEXT)
RETURNS TEXT
LANGUAGE sql
IMMUTABLE
AS $$
    -- Sanitize queue name for use as channel: replace dots and special chars
    SELECT 'queen_' || regexp_replace(lower(p_queue), '[^a-z0-9_]', '_', 'g');
$$;

-- Send notification for a queue
CREATE OR REPLACE FUNCTION queen.notify(p_queue TEXT, p_payload TEXT DEFAULT '')
RETURNS VOID
LANGUAGE plpgsql
AS $$
DECLARE
    v_channel TEXT;
BEGIN
    v_channel := queen.channel_name(p_queue);
    
    IF p_payload = '' THEN
        EXECUTE format('NOTIFY %I', v_channel);
    ELSE
        -- Payload limited to 8000 bytes in PostgreSQL
        EXECUTE format('NOTIFY %I, %L', v_channel, left(p_payload, 7999));
    END IF;
END;
$$;

-- Enhanced produce that sends notification
CREATE OR REPLACE FUNCTION queen.produce_notify(
    p_queue TEXT,
    p_payload JSONB,
    p_transaction_id TEXT DEFAULT NULL,
    p_partition TEXT DEFAULT 'Default'
)
RETURNS UUID
LANGUAGE plpgsql
AS $$
DECLARE
    v_id UUID;
BEGIN
    v_id := queen.produce(p_queue, p_partition, p_payload, p_transaction_id);
    PERFORM queen.notify(p_queue);
    RETURN v_id;
END;
$$;

-- Batch produce with single notification
CREATE OR REPLACE FUNCTION queen.produce_notify(
    p_queue TEXT,
    p_partition TEXT,
    p_payloads JSONB[]
)
RETURNS UUID[]
LANGUAGE plpgsql
AS $$
DECLARE
    v_ids UUID[];
    v_items JSONB;
    v_result JSONB;
    v_payload JSONB;
    v_idx INTEGER := 0;
BEGIN
    -- Build batch request
    v_items := '[]'::jsonb;
    FOREACH v_payload IN ARRAY p_payloads
    LOOP
        v_items := v_items || jsonb_build_array(jsonb_build_object(
            'queue', p_queue,
            'partition', p_partition,
            'transactionId', queen.uuid_generate_v7()::text,
            'payload', v_payload
        ));
        v_idx := v_idx + 1;
    END LOOP;
    
    -- Single batch push call
    v_result := queen.push_messages_v2(v_items);
    
    -- Extract message IDs
    SELECT array_agg((item->>'message_id')::uuid ORDER BY (item->>'index')::int)
    INTO v_ids
    FROM jsonb_array_elements(v_result) item;
    
    -- Single notification for all messages
    IF array_length(p_payloads, 1) > 0 THEN
        PERFORM queen.notify(p_queue, array_length(p_payloads, 1)::text);
    END IF;
    
    RETURN v_ids;
END;
$$;

-- ============================================================================
-- LONG POLLING SUPPORT (poll = consume with wait)
-- ============================================================================

-- Poll with wait (long polling) - Kafka-style poll
CREATE OR REPLACE FUNCTION queen.poll(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__',
    p_batch_size INTEGER DEFAULT 1,
    p_lease_seconds INTEGER DEFAULT 60,
    p_timeout_seconds INTEGER DEFAULT 30
)
RETURNS TABLE(
    partition_id UUID,
    id UUID,
    transaction_id TEXT,
    payload JSONB,
    created_at TIMESTAMPTZ,
    lease_id TEXT
)
LANGUAGE plpgsql
AS $$
DECLARE
    v_channel TEXT;
    v_start_time TIMESTAMPTZ;
    v_remaining_seconds FLOAT;
    v_found BOOLEAN := FALSE;
BEGIN
    v_channel := queen.channel_name(p_queue);
    v_start_time := clock_timestamp();
    
    -- First, try immediate consume
    FOR partition_id, id, transaction_id, payload, created_at, lease_id IN
        SELECT * FROM queen.consume(p_queue, p_consumer_group, p_batch_size, p_lease_seconds)
    LOOP
        v_found := TRUE;
        RETURN NEXT;
    END LOOP;
    
    IF v_found THEN
        RETURN;
    END IF;
    
    -- No messages - set up LISTEN and wait
    EXECUTE format('LISTEN %I', v_channel);
    
    BEGIN
        LOOP
            -- Calculate remaining time
            v_remaining_seconds := p_timeout_seconds - EXTRACT(EPOCH FROM (clock_timestamp() - v_start_time));
            
            IF v_remaining_seconds <= 0 THEN
                -- Timeout reached
                EXIT;
            END IF;
            
            -- Wait for notification (pg_sleep releases locks, allows notifications)
            -- Check every 100ms or remaining time, whichever is smaller
            PERFORM pg_sleep(LEAST(0.1, v_remaining_seconds));
            
            -- Check if messages available
            IF queen.has_messages(p_queue, NULL, p_consumer_group) THEN
                -- Try to consume
                FOR partition_id, id, transaction_id, payload, created_at, lease_id IN
                    SELECT * FROM queen.consume(p_queue, p_consumer_group, p_batch_size, p_lease_seconds)
                LOOP
                    v_found := TRUE;
                    RETURN NEXT;
                END LOOP;
                
                IF v_found THEN
                    EXIT;
                END IF;
            END IF;
        END LOOP;
    EXCEPTION WHEN OTHERS THEN
        -- Ensure we unlisten on error
        EXECUTE format('UNLISTEN %I', v_channel);
        RAISE;
    END;
    
    EXECUTE format('UNLISTEN %I', v_channel);
    RETURN;
END;
$$;

-- Convenience: poll one message
CREATE OR REPLACE FUNCTION queen.poll_one(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__',
    p_lease_seconds INTEGER DEFAULT 60,
    p_timeout_seconds INTEGER DEFAULT 30
)
RETURNS TABLE(
    partition_id UUID,
    id UUID,
    transaction_id TEXT,
    payload JSONB,
    created_at TIMESTAMPTZ,
    lease_id TEXT
)
LANGUAGE sql
AS $$
    SELECT * FROM queen.poll(p_queue, p_consumer_group, 1, p_lease_seconds, p_timeout_seconds);
$$;

-- Poll from specific partition
CREATE OR REPLACE FUNCTION queen.poll(
    p_queue TEXT,
    p_partition TEXT,
    p_consumer_group TEXT,
    p_batch_size INTEGER,
    p_lease_seconds INTEGER,
    p_timeout_seconds INTEGER DEFAULT 30
)
RETURNS TABLE(
    partition_id UUID,
    id UUID,
    transaction_id TEXT,
    payload JSONB,
    created_at TIMESTAMPTZ,
    lease_id TEXT
)
LANGUAGE plpgsql
AS $$
DECLARE
    v_channel TEXT;
    v_start_time TIMESTAMPTZ;
    v_remaining_seconds FLOAT;
    v_found BOOLEAN := FALSE;
BEGIN
    v_channel := queen.channel_name(p_queue);
    v_start_time := clock_timestamp();
    
    -- First, try immediate consume
    FOR partition_id, id, transaction_id, payload, created_at, lease_id IN
        SELECT * FROM queen.consume(p_queue, p_partition, p_consumer_group, p_batch_size, p_lease_seconds)
    LOOP
        v_found := TRUE;
        RETURN NEXT;
    END LOOP;
    
    IF v_found THEN
        RETURN;
    END IF;
    
    -- No messages - set up LISTEN and wait
    EXECUTE format('LISTEN %I', v_channel);
    
    BEGIN
        LOOP
            v_remaining_seconds := p_timeout_seconds - EXTRACT(EPOCH FROM (clock_timestamp() - v_start_time));
            
            IF v_remaining_seconds <= 0 THEN
                EXIT;
            END IF;
            
            PERFORM pg_sleep(LEAST(0.1, v_remaining_seconds));
            
            IF queen.has_messages(p_queue, p_partition, p_consumer_group) THEN
                FOR partition_id, id, transaction_id, payload, created_at, lease_id IN
                    SELECT * FROM queen.consume(p_queue, p_partition, p_consumer_group, p_batch_size, p_lease_seconds)
                LOOP
                    v_found := TRUE;
                    RETURN NEXT;
                END LOOP;
                
                IF v_found THEN
                    EXIT;
                END IF;
            END IF;
        END LOOP;
    EXCEPTION WHEN OTHERS THEN
        EXECUTE format('UNLISTEN %I', v_channel);
        RAISE;
    END;
    
    EXECUTE format('UNLISTEN %I', v_channel);
    RETURN;
END;
$$;
