-- ============================================================================
-- pg_qpubsub Wrapper Functions
-- ============================================================================
-- 
-- TWO-TIER API:
--
-- PRIMARY (JSONB-based, for programmatic access from Node.js/Python/etc):
--   - queen.produce(items JSONB)     → Batch produce, wraps push_messages_v2
--   - queen.consume(requests JSONB)  → Batch consume, wraps pop_unified_batch
--   - queen.commit(acks JSONB)       → Batch ack, wraps ack_messages_v2
--   - queen.renew(leases JSONB)      → Batch renew, wraps renew_lease_v2
--   - queen.transaction(ops JSONB)   → Atomic multi-op, wraps execute_transaction_v2
--
-- CONVENIENCE (scalar params, for SQL/psql usage):
--   - queen.produce_one(queue, payload, ...)    → Single message
--   - queen.consume_one(queue, ...)             → Single request
--   - queen.commit_one(txn_id, partition_id, ...)  → Single ack
--   - queen.renew_one(lease_id, ...)            → Single lease
--   - queen.nack(...)                           → Alias for commit with retry
--   - queen.reject(...)                         → Alias for commit with dlq
--
-- UTILITY:
--   - queen.configure(queue, ...)   → Queue settings
--   - queen.seek(...)               → Reposition cursor
--   - queen.delete_consumer_group(...)
--   - queen.lag(queue, ...)         → Queue depth
--   - queen.has_messages(...)       → Check pending
--   - queen.channel_name(queue)     → NOTIFY channel
--   - queen.notify(queue, payload)  → Send NOTIFY
--   - queen.forward(...)            → Atomic ack + produce
--
-- ============================================================================


-- ============================================================================
-- UUID v7 Functions
-- ============================================================================

-- Session variables for sequence counter (used within same millisecond)
DO $$ BEGIN
    PERFORM set_config('queen.uuid_v7_last_ts', '0', false);
    PERFORM set_config('queen.uuid_v7_seq', '0', false);
EXCEPTION WHEN OTHERS THEN NULL;
END $$;

-- Generate UUID v7 with sequence counter for strict ordering
CREATE OR REPLACE FUNCTION queen.uuid_generate_v7()
RETURNS UUID
LANGUAGE plpgsql
VOLATILE
AS $$
DECLARE
    v_time BIGINT;
    v_last_ts BIGINT;
    v_seq INT;
    v_rand BYTEA;
    v_bytes BYTEA;
BEGIN
    -- Get current timestamp in milliseconds
    v_time := (EXTRACT(EPOCH FROM clock_timestamp()) * 1000)::BIGINT;
    
    -- Get last timestamp and sequence from session
    BEGIN
        v_last_ts := current_setting('queen.uuid_v7_last_ts', true)::BIGINT;
        v_seq := current_setting('queen.uuid_v7_seq', true)::INT;
    EXCEPTION WHEN OTHERS THEN
        v_last_ts := 0;
        v_seq := 0;
    END;
    
    -- Handle same-millisecond generation
    IF v_time = v_last_ts THEN
        v_seq := v_seq + 1;
        IF v_seq > 4095 THEN
            -- Overflow: wait for next millisecond
            PERFORM pg_sleep(0.001);
            v_time := (EXTRACT(EPOCH FROM clock_timestamp()) * 1000)::BIGINT;
            v_seq := 0;
        END IF;
    ELSIF v_time < v_last_ts THEN
        -- Clock went backwards, use last_ts + 1
        v_time := v_last_ts;
        v_seq := v_seq + 1;
        IF v_seq > 4095 THEN
            v_time := v_time + 1;
            v_seq := 0;
        END IF;
    ELSE
        -- New millisecond
        v_seq := 0;
    END IF;
    
    -- Store for next call
    PERFORM set_config('queen.uuid_v7_last_ts', v_time::TEXT, false);
    PERFORM set_config('queen.uuid_v7_seq', v_seq::TEXT, false);
    
    -- Build UUID bytes: 48-bit timestamp + 4-bit version + 12-bit seq + 2-bit variant + 62-bit random
    v_rand := gen_random_bytes(8);
    
    v_bytes := 
        -- Bytes 0-5: 48-bit timestamp (big-endian)
        set_byte(set_byte(set_byte(set_byte(set_byte(set_byte(
            E'\\x000000000000'::bytea,
            0, ((v_time >> 40) & 255)::INT),
            1, ((v_time >> 32) & 255)::INT),
            2, ((v_time >> 24) & 255)::INT),
            3, ((v_time >> 16) & 255)::INT),
            4, ((v_time >> 8) & 255)::INT),
            5, (v_time & 255)::INT)
        ||
        -- Bytes 6-7: version (7) + 12-bit sequence
        set_byte(set_byte(E'\\x0000'::bytea,
            0, (112 + ((v_seq >> 8) & 15))::INT),  -- 0111xxxx = version 7 + high 4 bits of seq
            1, (v_seq & 255)::INT)                  -- low 8 bits of seq
        ||
        -- Bytes 8-15: variant (10) + random
        set_byte(v_rand, 0, (128 + (get_byte(v_rand, 0) & 63))::INT);  -- 10xxxxxx = variant
    
    RETURN encode(v_bytes, 'hex')::UUID;
END;
$$;

-- Extract timestamp from UUID v7
CREATE OR REPLACE FUNCTION queen.uuid_v7_to_timestamptz(p_uuid UUID)
RETURNS TIMESTAMPTZ
LANGUAGE sql
IMMUTABLE
PARALLEL SAFE
AS $$
    SELECT to_timestamp(
        (
            (get_byte(decode(replace(p_uuid::text, '-', ''), 'hex'), 0)::bigint << 40) |
            (get_byte(decode(replace(p_uuid::text, '-', ''), 'hex'), 1)::bigint << 32) |
            (get_byte(decode(replace(p_uuid::text, '-', ''), 'hex'), 2)::bigint << 24) |
            (get_byte(decode(replace(p_uuid::text, '-', ''), 'hex'), 3)::bigint << 16) |
            (get_byte(decode(replace(p_uuid::text, '-', ''), 'hex'), 4)::bigint << 8) |
            (get_byte(decode(replace(p_uuid::text, '-', ''), 'hex'), 5)::bigint)
        )::double precision / 1000
    );
$$;

-- Create boundary UUID for range queries (all zeros after timestamp)
CREATE OR REPLACE FUNCTION queen.uuid_v7_boundary(p_time TIMESTAMPTZ)
RETURNS UUID
LANGUAGE sql
IMMUTABLE
PARALLEL SAFE
AS $$
    SELECT (
        lpad(to_hex((EXTRACT(EPOCH FROM p_time) * 1000)::bigint), 12, '0') ||
        '-7000-8000-000000000000'
    )::UUID;
$$;

-- Generate UUID v7 at specific time (with randomness for safe inserts)
CREATE OR REPLACE FUNCTION queen.uuid_generate_v7_at(p_time TIMESTAMPTZ)
RETURNS UUID
LANGUAGE sql
VOLATILE
AS $$
    SELECT (
        lpad(to_hex((EXTRACT(EPOCH FROM p_time) * 1000)::bigint), 12, '0') ||
        '-' || lpad(to_hex(28672 + (random() * 4095)::int), 4, '0') ||  -- 7xxx
        '-' || lpad(to_hex(32768 + (random() * 16383)::int), 4, '0') || -- 8xxx-bxxx
        '-' || lpad(to_hex((random() * 281474976710655)::bigint), 12, '0')
    )::UUID;
$$;


-- ============================================================================
-- PRIMARY API: PRODUCE (JSONB batch)
-- ============================================================================
-- Thin wrapper around push_messages_v2
--
-- Input format (array of items):
-- [
--   {"queue": "orders", "payload": {...}, "partition": "Default", "transactionId": "..."},
--   ...
-- ]
--
-- Returns: JSONB array of results from push_messages_v2
-- ============================================================================

CREATE OR REPLACE FUNCTION queen.produce(p_items JSONB)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_enriched JSONB;
BEGIN
    -- Add messageId with UUID v7 to each item if not provided
    SELECT jsonb_agg(
        CASE 
            WHEN item->>'messageId' IS NULL THEN
                item || jsonb_build_object('messageId', queen.uuid_generate_v7()::TEXT)
            ELSE
                item
        END
    )
    INTO v_enriched
    FROM jsonb_array_elements(p_items) AS item;
    
    RETURN queen.push_messages_v2(v_enriched);
END;
$$;


-- ============================================================================
-- PRIMARY API: CONSUME (JSONB batch)
-- ============================================================================
-- Wrapper around pop_unified_batch with camelCase normalization
--
-- Input format (array of requests):
-- [
--   {
--     "queueName": "orders",
--     "partitionName": "",           -- empty = any partition
--     "consumerGroup": "__QUEUE_MODE__",
--     "batchSize": 10,
--     "leaseSeconds": 60,            -- optional, uses queue config if not set
--     "workerId": "worker-1",
--     "autoAck": false,
--     "subMode": "",                 -- 'new' or ''
--     "subFrom": ""                  -- 'now', timestamp, or ''
--   },
--   ...
-- ]
--
-- Returns: JSONB array of results from pop_unified_batch
-- ============================================================================

CREATE OR REPLACE FUNCTION queen.consume(p_requests JSONB)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_transformed JSONB;
BEGIN
    -- Transform camelCase keys to snake_case for pop_unified_batch
    -- Accepts both camelCase and snake_case for backwards compatibility
    SELECT jsonb_agg(
        jsonb_build_object(
            'queue_name',      COALESCE(req->>'queueName', req->>'queue_name'),
            'partition_name',  COALESCE(req->>'partitionName', req->>'partition_name', ''),
            'consumer_group',  COALESCE(req->>'consumerGroup', req->>'consumer_group', '__QUEUE_MODE__'),
            'batch_size',      COALESCE((req->>'batchSize')::int, (req->>'batch_size')::int, 1),
            'lease_seconds',   COALESCE((req->>'leaseSeconds')::int, (req->>'lease_seconds')::int, 60),
            'worker_id',       COALESCE(req->>'workerId', req->>'worker_id', 'wrapper-' || gen_random_uuid()::text),
            'auto_ack',        COALESCE((req->>'autoAck')::boolean, (req->>'auto_ack')::boolean, false),
            'sub_mode',        COALESCE(req->>'subMode', req->>'sub_mode', ''),
            'sub_from',        COALESCE(req->>'subFrom', req->>'sub_from', '')
        )
    )
    INTO v_transformed
    FROM jsonb_array_elements(p_requests) AS req;
    
    RETURN queen.pop_unified_batch(v_transformed);
END;
$$;


-- ============================================================================
-- PRIMARY API: COMMIT (JSONB batch)
-- ============================================================================
-- Thin wrapper around ack_messages_v2
--
-- Input format (array of acks):
-- [
--   {
--     "transactionId": "...",
--     "partitionId": "uuid",
--     "leaseId": "...",
--     "consumerGroup": "__QUEUE_MODE__",
--     "status": "completed",         -- 'completed', 'retry', 'dlq'
--     "errorMessage": null
--   },
--   ...
-- ]
--
-- Returns: JSONB array of results from ack_messages_v2
-- ============================================================================

CREATE OR REPLACE FUNCTION queen.commit(p_acks JSONB)
RETURNS JSONB
LANGUAGE sql
AS $$
    SELECT queen.ack_messages_v2(p_acks);
$$;


-- ============================================================================
-- PRIMARY API: RENEW (JSONB batch)
-- ============================================================================
-- Thin wrapper around renew_lease_v2
--
-- Input format (array of leases):
-- [
--   {"leaseId": "...", "extendSeconds": 60},
--   ...
-- ]
--
-- Returns: JSONB array of results from renew_lease_v2
-- ============================================================================

CREATE OR REPLACE FUNCTION queen.renew(p_leases JSONB)
RETURNS JSONB
LANGUAGE sql
AS $$
    SELECT queen.renew_lease_v2(p_leases);
$$;


-- ============================================================================
-- PRIMARY API: TRANSACTION (JSONB)
-- ============================================================================
-- Thin wrapper around execute_transaction_v2
--
-- Input format (array of operations):
-- [
--   {"type": "push", "queue": "...", "payload": {...}, ...},
--   {"type": "ack", "transactionId": "...", "partitionId": "...", ...},
--   ...
-- ]
--
-- Returns: JSONB array of results
-- ============================================================================

CREATE OR REPLACE FUNCTION queen.transaction(p_operations JSONB)
RETURNS JSONB
LANGUAGE sql
AS $$
    SELECT queen.execute_transaction_v2(p_operations);
$$;


-- ============================================================================
-- CONVENIENCE: produce_one (single message with scalar params)
-- ============================================================================
-- Send a single message to a queue
-- 
-- Parameters:
--   p_queue          - Queue name (required)
--   p_payload        - Message payload as JSONB (required)
--   p_partition      - Target partition (default: 'Default')
--   p_transaction_id - Idempotency key (default: auto-generated)
--   p_notify         - Send NOTIFY after push (default: false)
--
-- Returns: Message UUID
-- ============================================================================

CREATE OR REPLACE FUNCTION queen.produce_one(
    p_queue TEXT,
    p_payload JSONB,
    p_partition TEXT DEFAULT 'Default',
    p_transaction_id TEXT DEFAULT NULL,
    p_notify BOOLEAN DEFAULT FALSE
)
RETURNS UUID
LANGUAGE plpgsql
AS $$
DECLARE
    v_txn_id TEXT;
    v_result JSONB;
    v_msg_id UUID;
BEGIN
    v_txn_id := COALESCE(p_transaction_id, gen_random_uuid()::TEXT);
    
    v_result := queen.produce(jsonb_build_array(jsonb_build_object(
        'queue', p_queue,
        'partition', p_partition,
        'transactionId', v_txn_id,
        'payload', p_payload
    )));
    
    v_msg_id := (v_result->0->>'message_id')::UUID;
    
    IF p_notify THEN
        PERFORM queen.notify(p_queue);
    END IF;
    
    RETURN v_msg_id;
END;
$$;

-- Produce with full options (namespace, task, delay)
CREATE OR REPLACE FUNCTION queen.produce_full(
    p_queue TEXT,
    p_payload JSONB,
    p_partition TEXT DEFAULT 'Default',
    p_transaction_id TEXT DEFAULT NULL,
    p_namespace TEXT DEFAULT NULL,
    p_task TEXT DEFAULT NULL,
    p_delay_until TIMESTAMPTZ DEFAULT NULL,
    p_notify BOOLEAN DEFAULT FALSE
)
RETURNS TABLE(message_id UUID, transaction_id TEXT)
LANGUAGE plpgsql
AS $$
DECLARE
    v_txn_id TEXT;
    v_result JSONB;
    v_msg_id UUID;
BEGIN
    v_txn_id := COALESCE(p_transaction_id, gen_random_uuid()::TEXT);
    
    v_result := queen.produce(jsonb_build_array(jsonb_build_object(
        'queue', p_queue,
        'partition', p_partition,
        'transactionId', v_txn_id,
        'namespace', p_namespace,
        'task', p_task,
        'payload', p_payload
    )));
    
    v_msg_id := (v_result->0->>'message_id')::UUID;
    
    IF p_notify THEN
        PERFORM queen.notify(p_queue);
    END IF;
    
    message_id := v_msg_id;
    transaction_id := v_txn_id;
    RETURN NEXT;
END;
$$;

-- Produce with NOTIFY (convenience function)
CREATE OR REPLACE FUNCTION queen.produce_notify(
    p_queue TEXT,
    p_payload JSONB,
    p_partition TEXT DEFAULT 'Default',
    p_transaction_id TEXT DEFAULT NULL
)
RETURNS UUID
LANGUAGE sql
AS $$
    SELECT queen.produce_one(p_queue, p_payload, p_partition, p_transaction_id, TRUE);
$$;


-- ============================================================================
-- CONVENIENCE: consume_one (single request with scalar params)
-- ============================================================================
-- Receive messages from a queue
--
-- Parameters:
--   p_queue             - Queue name (required)
--   p_consumer_group    - Consumer group name (default: '__QUEUE_MODE__')
--   p_batch_size        - Max messages to return (default: 1)
--   p_partition         - Specific partition (default: NULL = any)
--   p_timeout_seconds   - Long-poll timeout, 0=immediate (default: 0)
--   p_auto_commit       - Auto-acknowledge messages (default: false)
--   p_subscription_mode - For new groups: 'new', 'all', or ISO timestamp (default: NULL = all)
--
-- Returns: Table of messages with partition_id, id, transaction_id, payload, created_at, lease_id
--
-- NOTE: Lease time comes from queue configuration (queen.configure)
-- ============================================================================

CREATE OR REPLACE FUNCTION queen.consume_one(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__',
    p_batch_size INTEGER DEFAULT 1,
    p_partition TEXT DEFAULT NULL,
    p_timeout_seconds INTEGER DEFAULT 0,
    p_auto_commit BOOLEAN DEFAULT FALSE,
    p_subscription_mode TEXT DEFAULT NULL
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
    v_msg JSONB;
    v_lease_id TEXT;
    v_partition_id UUID;
    v_queue_lease_time INTEGER;
    v_sub_mode TEXT;
    v_sub_from TEXT;
    v_attempt INTEGER := 0;
    v_max_attempts INTEGER;
BEGIN
    -- Get lease time from queue configuration
    SELECT COALESCE(q.lease_time, 300) INTO v_queue_lease_time
    FROM queen.queues q
    WHERE q.name = p_queue;
    
    -- Default to 300 if queue doesn't exist yet
    v_queue_lease_time := COALESCE(v_queue_lease_time, 300);
    
    -- Parse subscription mode
    v_sub_mode := '';
    v_sub_from := '';
    IF p_subscription_mode IS NOT NULL THEN
        IF p_subscription_mode = 'new' THEN
            v_sub_mode := 'new';
            v_sub_from := 'now';
        ELSIF p_subscription_mode = 'all' THEN
            v_sub_mode := '';
            v_sub_from := '';
        ELSE
            -- Assume it's a timestamp
            v_sub_from := p_subscription_mode;
        END IF;
    END IF;
    
    -- Generate lease ID
    v_lease_id := 'wrapper-' || gen_random_uuid()::TEXT;
    
    -- Calculate max attempts for long polling
    v_max_attempts := GREATEST(1, p_timeout_seconds);
    
    LOOP
        v_attempt := v_attempt + 1;
        
        -- Call consume (which calls pop_unified_batch)
        v_result := queen.consume(jsonb_build_array(jsonb_build_object(
            'queue_name', p_queue,
            'partition_name', COALESCE(p_partition, ''),
            'consumer_group', p_consumer_group,
            'batch_size', p_batch_size,
            'lease_seconds', v_queue_lease_time,
            'worker_id', v_lease_id,
            'auto_ack', p_auto_commit,
            'sub_mode', v_sub_mode,
            'sub_from', v_sub_from
        )));
        
        -- Extract batch (result is [{idx: 0, result: {success, partitionId, messages, ...}}])
        v_batch := v_result->0->'result';
        
        -- Get partition_id from batch level
        v_partition_id := (v_batch->>'partitionId')::UUID;
        
        -- Check if we got messages
        IF v_batch->'messages' IS NOT NULL AND jsonb_array_length(v_batch->'messages') > 0 THEN
            -- Return each message
            FOR v_msg IN SELECT * FROM jsonb_array_elements(v_batch->'messages')
            LOOP
                partition_id := v_partition_id;
                id := (v_msg->>'id')::UUID;
                transaction_id := v_msg->>'transactionId';
                payload := v_msg->'data';
                created_at := (v_msg->>'createdAt')::TIMESTAMPTZ;
                lease_id := v_lease_id;
                RETURN NEXT;
            END LOOP;
            RETURN;
        END IF;
        
        -- No messages - check if we should wait
        IF p_timeout_seconds <= 0 OR v_attempt >= v_max_attempts THEN
            -- Immediate return or timeout reached
            RETURN;
        END IF;
        
        -- Long poll: wait 1 second and retry
        PERFORM pg_sleep(1);
    END LOOP;
END;
$$;


-- ============================================================================
-- CONVENIENCE: commit_one (single ack with scalar params)
-- ============================================================================
-- Acknowledge message processing
--
-- Parameters:
--   p_transaction_id  - Transaction ID from consumed message (required)
--   p_partition_id    - Partition ID from consumed message (required)
--   p_lease_id        - Lease ID from consumed message (required)
--   p_consumer_group  - Consumer group (default: '__QUEUE_MODE__')
--   p_status          - Status: 'completed', 'failed', 'dlq' (default: 'completed')
--   p_error_message   - Error message for failed/dlq (default: NULL)
--
-- Returns: TRUE if acknowledged successfully
-- ============================================================================

CREATE OR REPLACE FUNCTION queen.commit_one(
    p_transaction_id TEXT,
    p_partition_id UUID,
    p_lease_id TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__',
    p_status TEXT DEFAULT 'completed',
    p_error_message TEXT DEFAULT NULL
)
RETURNS BOOLEAN
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
    v_ack_status TEXT;
BEGIN
    -- Map status to ack_messages_v2 status values
    v_ack_status := CASE p_status
        WHEN 'completed' THEN 'completed'
        WHEN 'failed' THEN 'retry'
        WHEN 'dlq' THEN 'dlq'
        ELSE 'completed'
    END;
    
    v_result := queen.commit(jsonb_build_array(jsonb_build_object(
        'transactionId', p_transaction_id,
        'partitionId', p_partition_id::TEXT,
        'leaseId', p_lease_id,
        'consumerGroup', p_consumer_group,
        'status', v_ack_status,
        'errorMessage', p_error_message
    )));
    
    RETURN (v_result->0->>'success')::BOOLEAN;
END;
$$;

-- Convenience: nack (mark for retry)
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
    SELECT queen.commit_one(p_transaction_id, p_partition_id, p_lease_id, p_consumer_group, 'failed', p_error_message);
$$;

-- Convenience: reject (send to DLQ)
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
    SELECT queen.commit_one(p_transaction_id, p_partition_id, p_lease_id, p_consumer_group, 'dlq', p_error_message);
$$;


-- ============================================================================
-- CONVENIENCE: renew_one (single lease with scalar params)
-- ============================================================================

CREATE OR REPLACE FUNCTION queen.renew_one(
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
    v_result := queen.renew(jsonb_build_array(jsonb_build_object(
        'leaseId', p_lease_id,
        'extendSeconds', p_extend_seconds
    )));
    
    v_item := v_result->0;
    IF (v_item->>'success')::BOOLEAN THEN
        RETURN (v_item->>'expiresAt')::TIMESTAMPTZ;
    END IF;
    
    RETURN NULL;
END;
$$;


-- ============================================================================
-- UTILITY: Configure Queue
-- ============================================================================

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
    v_result JSONB;
BEGIN
    v_result := queen.configure_queue_v1(p_queue, jsonb_build_object(
        'leaseTime', p_lease_time,
        'retryLimit', p_retry_limit,
        'deadLetterQueue', p_dead_letter_queue
    ));
    
    RETURN (v_result->>'success')::BOOLEAN;
END;
$$;


-- ============================================================================
-- UTILITY: Seek Consumer Group
-- ============================================================================

CREATE OR REPLACE FUNCTION queen.seek(
    p_consumer_group TEXT,
    p_queue TEXT,
    p_to_end BOOLEAN DEFAULT FALSE,
    p_to_timestamp TIMESTAMPTZ DEFAULT NULL
)
RETURNS BOOLEAN
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
BEGIN
    v_result := queen.seek_consumer_group_v1(p_consumer_group, p_queue, p_to_timestamp, p_to_end);
    RETURN (v_result->>'success')::BOOLEAN;
END;
$$;


-- ============================================================================
-- UTILITY: Delete Consumer Group
-- ============================================================================

CREATE OR REPLACE FUNCTION queen.delete_consumer_group(
    p_consumer_group TEXT,
    p_queue TEXT DEFAULT NULL,
    p_delete_metadata BOOLEAN DEFAULT TRUE
)
RETURNS BOOLEAN
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
BEGIN
    IF p_queue IS NULL THEN
        v_result := queen.delete_consumer_group_v1(p_consumer_group, p_delete_metadata);
    ELSE
        v_result := queen.delete_consumer_group_for_queue_v1(p_consumer_group, p_queue, p_delete_metadata);
    END IF;
    RETURN (v_result->>'success')::BOOLEAN;
END;
$$;


-- ============================================================================
-- UTILITY: Queue Depth/Lag
-- ============================================================================

CREATE OR REPLACE FUNCTION queen.lag(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__'
)
RETURNS BIGINT
LANGUAGE plpgsql
AS $$
DECLARE
    v_count BIGINT;
    v_sub_ts TIMESTAMPTZ;
BEGIN
    -- Get subscription timestamp for "new" mode consumers (NULL for "all" mode)
    SELECT cgm.subscription_timestamp INTO v_sub_ts
    FROM queen.consumer_groups_metadata cgm
    WHERE cgm.consumer_group = p_consumer_group
      AND cgm.queue_name = p_queue
      AND cgm.subscription_timestamp IS NOT NULL
    LIMIT 1;

    SELECT COUNT(*) INTO v_count
    FROM queen.messages m
    JOIN queen.partitions p ON m.partition_id = p.id
    JOIN queen.queues q ON p.queue_id = q.id
    LEFT JOIN queen.partition_consumers pc 
        ON pc.partition_id = p.id AND pc.consumer_group = p_consumer_group
    WHERE q.name = p_queue
      AND (COALESCE(pc.last_consumed_created_at, v_sub_ts) IS NULL 
           OR (m.created_at, m.id) > (
               COALESCE(pc.last_consumed_created_at, v_sub_ts),
               COALESCE(pc.last_consumed_id, '00000000-0000-0000-0000-000000000000'::uuid)
           ));
    
    RETURN COALESCE(v_count, 0);
END;
$$;

-- Alias for backwards compatibility
CREATE OR REPLACE FUNCTION queen.depth(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__'
)
RETURNS BIGINT
LANGUAGE sql
AS $$
    SELECT queen.lag(p_queue, p_consumer_group);
$$;


-- ============================================================================
-- UTILITY: Has Messages
-- ============================================================================

CREATE OR REPLACE FUNCTION queen.has_messages(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__'
)
RETURNS BOOLEAN
LANGUAGE sql
AS $$
    SELECT queen.has_pending_messages(p_queue, NULL, p_consumer_group);
$$;


-- ============================================================================
-- UTILITY: Forward (Atomic Ack + Produce)
-- ============================================================================

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
    v_txn_id TEXT;
    v_result JSONB;
    v_msg_id UUID;
BEGIN
    v_txn_id := COALESCE(p_dest_transaction_id, gen_random_uuid()::TEXT);
    
    v_result := queen.transaction(jsonb_build_array(
        jsonb_build_object(
            'type', 'ack',
            'transactionId', p_source_transaction_id,
            'partitionId', p_source_partition_id::TEXT,
            'leaseId', p_source_lease_id,
            'consumerGroup', p_source_consumer_group
        ),
        jsonb_build_object(
            'type', 'push',
            'queue', p_dest_queue,
            'partition', p_dest_partition,
            'transactionId', v_txn_id,
            'payload', p_dest_payload
        )
    ));
    
    v_msg_id := (v_result->1->>'messageId')::UUID;
    RETURN v_msg_id;
END;
$$;


-- ============================================================================
-- UTILITY: NOTIFY/LISTEN Support
-- ============================================================================

-- Get channel name for a queue
CREATE OR REPLACE FUNCTION queen.channel_name(p_queue TEXT)
RETURNS TEXT
LANGUAGE sql
IMMUTABLE
AS $$
    SELECT 'queen_' || replace(replace(p_queue, '.', '_'), '-', '_');
$$;

-- Send NOTIFY on queue channel
CREATE OR REPLACE FUNCTION queen.notify(
    p_queue TEXT,
    p_payload TEXT DEFAULT ''
)
RETURNS VOID
LANGUAGE plpgsql
AS $$
BEGIN
    EXECUTE format('NOTIFY %I, %L', queen.channel_name(p_queue), p_payload);
END;
$$;


-- ============================================================================
-- Grant Permissions
-- ============================================================================

-- UUID functions
GRANT EXECUTE ON FUNCTION queen.uuid_generate_v7() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.uuid_v7_to_timestamptz(UUID) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.uuid_v7_boundary(TIMESTAMPTZ) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.uuid_generate_v7_at(TIMESTAMPTZ) TO PUBLIC;

-- Primary API (JSONB)
GRANT EXECUTE ON FUNCTION queen.produce(JSONB) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.consume(JSONB) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.commit(JSONB) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.renew(JSONB) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.transaction(JSONB) TO PUBLIC;

-- Convenience API (scalar)
GRANT EXECUTE ON FUNCTION queen.produce_one(TEXT, JSONB, TEXT, TEXT, BOOLEAN) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.produce_full(TEXT, JSONB, TEXT, TEXT, TEXT, TEXT, TIMESTAMPTZ, BOOLEAN) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.produce_notify(TEXT, JSONB, TEXT, TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.consume_one(TEXT, TEXT, INTEGER, TEXT, INTEGER, BOOLEAN, TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.commit_one(TEXT, UUID, TEXT, TEXT, TEXT, TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.nack(TEXT, UUID, TEXT, TEXT, TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.reject(TEXT, UUID, TEXT, TEXT, TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.renew_one(TEXT, INTEGER) TO PUBLIC;

-- Utility
GRANT EXECUTE ON FUNCTION queen.configure(TEXT, INTEGER, INTEGER, BOOLEAN) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.seek(TEXT, TEXT, BOOLEAN, TIMESTAMPTZ) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.delete_consumer_group(TEXT, TEXT, BOOLEAN) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.lag(TEXT, TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.depth(TEXT, TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.has_messages(TEXT, TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.forward(TEXT, UUID, TEXT, TEXT, TEXT, JSONB, TEXT, TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.channel_name(TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.notify(TEXT, TEXT) TO PUBLIC;
