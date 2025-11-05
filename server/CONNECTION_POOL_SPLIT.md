# Connection Pool Split - Temporary Solution

## Current State (Temporary)

**Problem**: Running both async and regular DB pools at full capacity exceeds PostgreSQL's `max_connections`.

**Solution**: Split connection budget between two pools.

### Configuration

```cpp
int total_connections = config.database.pool_size * 0.95;

// SPLIT: 60/40
int async_connections = total_connections * 0.6;      // For push operations
int regular_connections = total_connections * 0.4;    // For pop/ack/analytics
```

### Split Rationale

- **60% Async Pool**: Push operations are typically high-volume
- **40% Regular Pool**: Pop, ack, analytics, background jobs

### Example (DB_POOL_SIZE=90)

- Total available: 90 * 0.95 = **85 connections**
- Async pool: 85 * 0.6 = **51 connections** (push)
- Regular pool: 85 * 0.4 = **34 connections** (pop/ack/analytics)
- Thread pool: 34 threads (1:1 with regular pool)

## Future Migration Path

### Phase 1 (Current): Split Pools
- ✅ Async pool for push operations
- ✅ Regular pool for everything else
- ✅ Both pools active simultaneously

### Phase 2 (Future): Expand Async Operations
When ready, migrate additional operations to async:
1. **Pop operations** → `AsyncQueueManager::pop_messages()`
2. **Ack operations** → `AsyncQueueManager::acknowledge_messages()`
3. **Lease operations** → Async lease management
4. **Analytics queries** → Async analytics

### Phase 3 (Final): Single Async Pool
- Remove regular `DatabasePool` entirely
- Use only `AsyncDbPool` for everything
- Eliminate thread pool dependency completely
- Use 100% of connection budget for async pool

## Benefits of Full Migration

✅ **Performance**:
- No thread pool overhead
- Lower latency across all operations
- Better CPU efficiency

✅ **Simplicity**:
- Single connection pool to manage
- No split configuration
- Simpler code paths

✅ **Scalability**:
- More connections available (no split)
- Linear scaling with workers

## Migration Checklist

When migrating to full async:

- [ ] Implement `AsyncQueueManager::pop_messages()`
- [ ] Implement `AsyncQueueManager::acknowledge_messages()`
- [ ] Implement async lease management
- [ ] Migrate analytics to async
- [ ] Update background jobs to use async pool
- [ ] Test all operations with async pool only
- [ ] Remove `global_db_pool` and `global_db_thread_pool`
- [ ] Update connection split to 100% async

## Monitoring

Watch these metrics during split operation:

1. **Connection utilization**:
   - Async pool: Should be high (push is high-volume)
   - Regular pool: May be underutilized

2. **Pool exhaustion**:
   - If async pool exhausts: Increase async %
   - If regular pool exhausts: Increase regular %

3. **PostgreSQL max_connections**:
   - Total should stay under limit
   - Default PostgreSQL limit: 100
   - Can be increased in postgresql.conf

## Current Status

- ✅ **Date**: 2025-11-05
- ✅ **Split Implemented**: 60% async / 40% regular
- ✅ **Works With**: DB_POOL_SIZE up to PostgreSQL max_connections
- ⏳ **Future**: Migrate all operations to async

## Related Files

- `src/acceptor_server.cpp` - Pool initialization (lines ~2535-2575)
- `include/queen/async_database.hpp` - Async pool interface
- `include/queen/async_queue_manager.hpp` - Async queue manager
- `ASYNC_DATABASE_IMPLEMENTATION.md` - Implementation details
- `ASYNC_PUSH_IMPLEMENTATION.md` - Push route migration

---

**Note**: This is a temporary measure. The end goal is full async migration.

