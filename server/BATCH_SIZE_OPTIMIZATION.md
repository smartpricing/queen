# Batch Push Optimization - Size-Based Dynamic Batching

## Implementation Summary

This document describes the size-based dynamic batching optimization implemented for PostgreSQL write performance.

## Changes Made

### 1. Configuration (`include/queen/config.hpp`)
Added new environment-configurable parameters:
- `BATCH_PUSH_USE_SIZE_BASED` (bool, default: true) - Enable/disable size-based batching
- `BATCH_PUSH_TARGET_SIZE_MB` (int, default: 4) - Target batch size in MB
- `BATCH_PUSH_MIN_SIZE_MB` (int, default: 2) - Minimum batch size in MB
- `BATCH_PUSH_MAX_SIZE_MB` (int, default: 8) - Maximum batch size in MB
- `BATCH_PUSH_MIN_MESSAGES` (int, default: 100) - Minimum messages per batch
- `BATCH_PUSH_MAX_MESSAGES` (int, default: 10000) - Maximum messages per batch

Legacy parameter preserved for backward compatibility:
- `BATCH_PUSH_CHUNK_SIZE` (int, default: 1000) - Used when size-based batching is disabled

### 2. Queue Manager (`src/managers/queue_manager.cpp`)

#### New Function: `estimate_row_size()`
Estimates the total row size including:
- Fixed overhead (tuple header, alignment, system columns): ~500 bytes
- UUID columns (id, transaction_id, trace_id, partition_id): ~100 bytes
- Timestamps and flags: ~9 bytes
- JSONB payload: actual serialized size
- Encryption overhead: +35% if encryption enabled
- JSONB metadata overhead: ~50 bytes
- Index overhead (4 indexes): ~250 bytes
- Page alignment overhead: +10%

#### Modified Function: `push_messages_batch()`
Now supports two modes:

**Size-Based Mode (default):**
1. Checks queue encryption status
2. Accumulates messages by estimated size
3. Flushes batch when:
   - Would exceed MAX_BATCH_SIZE
   - Reaches MAX_MESSAGES count
   - Reaches TARGET_BATCH_SIZE with MIN_MESSAGES met
4. Processes final batch at end

**Legacy Count-Based Mode:**
- Preserves original behavior
- Uses fixed BATCH_PUSH_CHUNK_SIZE
- Enabled by setting `BATCH_PUSH_USE_SIZE_BASED=false`

## Backward Compatibility

✅ **API Compatibility:** No changes to function signatures
- `push_messages()` - unchanged
- `push_messages_batch()` - unchanged (internal method)
- `push_messages_chunk()` - unchanged (internal method)
- `PushItem` struct - unchanged
- `PushResult` struct - unchanged

✅ **Behavioral Compatibility:**
- Can be disabled via `BATCH_PUSH_USE_SIZE_BASED=false`
- Falls back to legacy behavior if disabled
- All existing error handling preserved
- Transaction semantics unchanged

✅ **Integration Points Verified:**
- `acceptor_server.cpp` - calls `push_messages()` unchanged
- `file_buffer.cpp` - calls `push_messages()` unchanged
- All tests continue to work (no test changes needed)

## Safety Measures

1. **Graceful Degradation:**
   - Encryption check wrapped in try-catch
   - Falls back to encryption_enabled=false on error
   - Legacy mode available as fallback

2. **Bounds Checking:**
   - MIN/MAX size limits enforced
   - MIN/MAX message count limits enforced
   - No single message can cause infinite loop

3. **Resource Safety:**
   - Max batch size prevents excessive memory usage
   - Max transaction size prevents long-running transactions
   - Debug logging only (no performance impact)

4. **Edge Cases Handled:**
   - Empty items list (early return)
   - Single item (accumulated and flushed)
   - Very large single message (added alone to batch)
   - Last item in list (properly flushed)

## Performance Benefits

Based on PostgreSQL best practices research (2024):

- **Target: 2-8 MB per batch** (sweet spot: 4-6 MB)
- **Expected improvement:** 20-50x faster than individual inserts
- **Optimal for:** Variable-sized message payloads
- **Benefits:**
  - Consistent throughput regardless of payload size variance
  - Better memory utilization
  - Reduced transaction commit overhead
  - Optimized for PostgreSQL write-ahead log (WAL) performance

## Testing Recommendations

1. **Functional Testing:**
   - Test with small messages (<1KB)
   - Test with medium messages (1-10KB)
   - Test with large messages (>10KB)
   - Test with mixed workloads
   - Test with encryption enabled/disabled

2. **Performance Testing:**
   - Benchmark with legacy mode (size-based disabled)
   - Benchmark with size-based mode (default)
   - Compare throughput and latency
   - Monitor memory usage

3. **Edge Case Testing:**
   - Single message push
   - Empty batch push
   - Very large single message (>8MB payload)
   - Rapid small message bursts
   - Database connection failures during batch

## Configuration Examples

### High-Throughput Small Messages
```bash
BATCH_PUSH_USE_SIZE_BASED=true
BATCH_PUSH_TARGET_SIZE_MB=6
BATCH_PUSH_MIN_SIZE_MB=3
BATCH_PUSH_MAX_SIZE_MB=10
BATCH_PUSH_MIN_MESSAGES=500
BATCH_PUSH_MAX_MESSAGES=20000
```

### Conservative Large Messages
```bash
BATCH_PUSH_USE_SIZE_BASED=true
BATCH_PUSH_TARGET_SIZE_MB=3
BATCH_PUSH_MIN_SIZE_MB=2
BATCH_PUSH_MAX_SIZE_MB=6
BATCH_PUSH_MIN_MESSAGES=50
BATCH_PUSH_MAX_MESSAGES=5000
```

### Disable (Legacy Mode)
```bash
BATCH_PUSH_USE_SIZE_BASED=false
BATCH_PUSH_CHUNK_SIZE=1000
```

## Monitoring

Watch for these metrics in production:
- Average batch size (bytes)
- Average batch message count
- Batch processing time
- Memory usage during batch operations
- PostgreSQL transaction commit time

Adjust configuration based on observed performance.

## References

- PostgreSQL Bulk Insert Best Practices: https://www.postgresql.org/docs/current/populate.html
- Optimal batch size research: 2-8 MB per transaction
- UNNEST performance: Order-preserving bulk insert method
- Index overhead: ~250 bytes per row for 4 indexes

