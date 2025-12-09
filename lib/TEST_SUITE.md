# Queen C++ Library Test Suite

Comprehensive test suite for `queen.hpp` - the high-performance C++ client library for QueenMQ.

## Overview

This test suite validates all core operations of the libqueen library:

- **PUSH**: Single, batch, duplicate detection, partitions, large payloads, null/empty payloads
- **POP**: Empty queue, non-empty, batch, partition-specific, consumer groups
- **ACK**: Single, batch, success/failed status handling
- **TRANSACTIONS**: Atomic push+ack, multiple pushes, multiple acks, partition operations
- **RENEW_LEASE**: Manual renewal, batch renewal, expired lease rejection, multiple renewals
- **MESSAGE ORDERING**: FIFO ordering verification

## Requirements

- PostgreSQL server running with QueenMQ schema
- C++17 compiler (g++ or clang++)
- Dependencies (from `../server/vendor/`):
  - libuv (static library)
  - json.hpp (nlohmann json)
  - spdlog (header-only)
  - libpq (PostgreSQL client)

## Building

```bash
cd lib/
make test-suite
```

## Running

### Default Connection

```bash
./build/test_suite
# Uses: postgres://postgres:postgres@localhost:5432/postgres
```

### Custom Connection String

```bash
./build/test_suite "postgres://user:pass@host:port/database"
```

## Test Structure

Each test follows a pattern:

1. **Setup**: Create unique queue names to avoid conflicts
2. **Execute**: Perform the operation(s) being tested
3. **Verify**: Assert expected results
4. **Cleanup**: Async operations complete naturally

### Test Categories

#### PUSH Tests
- `test_push_single_message` - Push single message to queue
- `test_push_batch_messages` - Push multiple messages in one call
- `test_push_duplicate_detection` - Verify duplicate transactionId detection
- `test_push_to_different_partitions` - Same txnId to different partitions (allowed)
- `test_push_large_payload` - ~100KB payload handling
- `test_push_null_payload` - null payload support
- `test_push_empty_object_payload` - Empty {} payload support

#### POP Tests
- `test_pop_empty_queue` - Pop from empty queue returns 0 messages
- `test_pop_non_empty_queue` - Pop returns pushed message
- `test_pop_batch` - Pop multiple messages at once
- `test_pop_from_specific_partition` - Partition isolation
- `test_pop_with_consumer_group` - Multiple consumer groups see same messages

#### ACK Tests
- `test_ack_single_message` - Acknowledge one message
- `test_ack_batch_messages` - Acknowledge multiple messages
- `test_ack_with_failed_status` - Failed/retry status handling

#### TRANSACTION Tests
- `test_transaction_basic_push_ack` - Atomic push + ack
- `test_transaction_multiple_pushes` - Push to multiple queues atomically
- `test_transaction_multiple_acks` - Ack multiple messages atomically
- `test_transaction_batch_push` - Multiple pushes in single transaction
- `test_transaction_with_partitions` - Cross-partition atomic operations

#### RENEW LEASE Tests
- `test_renew_lease_manual` - Extend lease before expiry
- `test_renew_lease_batch` - Extend lease for batch pop
- `test_renew_expired_lease` - Renewal of expired lease fails
- `test_renew_multiple_times` - Multiple successive renewals

#### ORDERING Tests
- `test_message_ordering` - Verify FIFO message ordering

## Output Format

```
============================================
Queen C++ Library Test Suite
============================================
Connection: postgres://...

=== PUSH Tests ===
  ✅ test_push_single_message (45ms)
  ✅ test_push_batch_messages (38ms)
  ...

=== POP Tests ===
  ✅ test_pop_empty_queue (22ms)
  ...

============================================
Results: 24 passed, 0 failed
============================================
```

## Test Isolation

Each test uses dynamically generated queue names with UUID suffixes to ensure:
- Tests don't interfere with each other
- Tests can run in parallel (future enhancement)
- No manual cleanup required between runs

## Async Pattern

The test suite uses `AsyncWaiter` helpers to synchronize async callbacks:

```cpp
AsyncWaiter waiter;
bool success = false;

queen.submit(JobRequest{...}, [&](std::string result) {
    success = check_result(result);
    waiter.signal();
});

waiter.wait();  // Blocks until callback fires
ASSERT(success, "Operation failed");
```

## Parameter Formats

The tests use helper functions that match the SQL procedure parameter formats:

| Operation | SQL Procedure | Helper Function |
|-----------|---------------|-----------------|
| PUSH | `push_messages_v2` | `build_push_params()` |
| POP | `pop_unified_batch` | `build_pop_params()` |
| ACK | `ack_messages_v2` | `build_ack_params()` |
| TRANSACTION | `execute_transaction_v2` | `build_transaction_params()` |
| RENEW | `renew_lease_v2` | `build_renew_params()` |

## Excluded Tests

The following JS test functionality is NOT included (server-specific):
- Maintenance mode tests
- Retention/TTL tests
- DLQ (Dead Letter Queue) reprocessing
- Encryption at rest tests
- Server configuration tests

These features are tested at the server level, not the client library level.

## Adding New Tests

1. Define test function: `TEST(test_name) { ... }`
2. Use helpers: `build_*_params()` functions
3. Use assertions: `ASSERT()`, `ASSERT_EQ()`, `ASSERT_GT()`
4. Add to main(): `RUN_TEST(test_name);`

Example:

```cpp
TEST(test_my_new_feature) {
    std::string queue = generate_queue_name("test-new-feature");
    AsyncWaiter waiter;
    bool success = false;
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params(queue, "Default", {{"test", "data"}}) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        success = json[0]["status"] == "queued";
        waiter.signal();
    });
    
    waiter.wait();
    ASSERT(success, "My feature should work");
}
```

