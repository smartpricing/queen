# Getting Started with AI-Generated Tests

## Quick Start

The AI has identified and created tests for **7 major gaps** in your test coverage:

### 1. **Error Handling** (`ai_error_handling.js`) - 10 tests
Critical edge cases like invalid inputs, expired leases, and system limits.

### 2. **Lease Renewal** (`ai_lease_renewal.js`) - 5 tests  
Manual lease extension API for long-running message processing.

### 3. **Resources/Status** (`ai_resources.js`) - 8 tests
Administrative APIs for monitoring queues, namespaces, tasks, and system health.

### 4. **Client Buffering** (`ai_buffering.js`) - 6 tests
Client-side message buffering for high-throughput scenarios.

### 5. **Priority Queues** (`ai_priority.js`) - 4 tests
Message priority handling across queues, namespaces, and tasks.

### 6. **TTL & Retention** (`ai_ttl_retention.js`) - 5 tests
Message expiration, retention policies, and DLQ based on wait time.

### 7. **Mixed Scenarios** (`ai_mixed_scenarios.js`) - 7 tests
Complex real-world scenarios combining multiple features.

## Running the New Tests

### Run ONLY AI-generated tests:
```bash
cd client-js/test-v2
node run.js ai
```

### Run ONLY human-written tests:
```bash
node run.js human
```

### Run all tests (both AI and human):
```bash
node run.js
# or
node run.js all
```

### Run a specific test:
```bash
node run.js testManualLeaseRenewal
node run.js testInvalidQueueName
node run.js testListQueues
```

### List all available tests:
```bash
# Run with invalid argument to see help
node run.js help
```

## What Was Missing Before?

| Area | Status Before | Status Now |
|------|--------------|------------|
| Error Handling | ❌ No coverage | ✅ 10 tests |
| Lease Renewal API | ❌ Only auto-renewal | ✅ Manual renewal covered |
| Resource APIs | ❌ Not tested | ✅ All endpoints covered |
| Buffer Management | ❌ Basic buffering only | ✅ Full coverage |
| Priority Queues | ❌ Config only | ✅ Behavior validated |
| TTL/Retention | ❌ Not tested | ✅ Complete lifecycle |
| Complex Scenarios | ⚠️ Partial | ✅ Real-world workflows |

## Key Test Highlights

### Most Important Tests

1. **`testManualLeaseRenewal`** - Essential for long-running tasks
2. **`testAckExpiredLease`** - Prevents data loss from expired leases
3. **`testCrossQueueWorkflow`** - Validates transactional pipelines
4. **`testBufferStatistics`** - Monitors client-side batching
5. **`testPriorityWithNamespace`** - Ensures priority ordering works

### Production-Critical Tests

- `testAckWithoutPartitionId` - Prevents acknowledging wrong messages
- `testBatchAckMixedResults` - Handles partial batch failures
- `testHighConcurrencyMixedOperations` - Validates concurrency handling
- `testEncryptedWithPartitionAndGroup` - Complex security scenario

## File Structure

```
test-v2/
├── queue.js                    # Original: Queue CRUD
├── push.js                     # Original: Push operations  
├── pop.js                      # Original: Pop operations
├── consume.js                  # Original: Consumer patterns
├── load.js                     # Original: Load testing
├── dlq.js                      # Original: Dead letter queue
├── complete.js                 # Original: Workflows
├── transaction.js              # Original: Transactions
├── ai_error_handling.js        # NEW: Error & edge cases
├── ai_lease_renewal.js         # NEW: Lease management
├── ai_resources.js             # NEW: Admin APIs
├── ai_buffering.js             # NEW: Buffer management
├── ai_priority.js              # NEW: Priority behavior
├── ai_ttl_retention.js         # NEW: Message lifecycle
├── ai_mixed_scenarios.js       # NEW: Complex scenarios
├── run.js                      # Test runner (updated)
├── AI_TEST_SUMMARY.md          # Detailed documentation
└── GETTING_STARTED.md          # This file
```

## Before Running Tests

Make sure:
1. ✅ Queen server is running on `http://localhost:6632`
2. ✅ PostgreSQL database is accessible
3. ✅ Environment variables are set (if needed)
4. ✅ No production data in test database

## Testing with Different Server Configurations

### Standard Configuration (Default)

Run server with default settings:
```bash
./bin/queen-server
node test-v2/run.js
```

**Expected:** Consumer groups without explicit `.subscriptionMode()` process all historical messages.

### With DEFAULT_SUBSCRIPTION_MODE="new"

Run server with "new" as default:
```bash
DEFAULT_SUBSCRIPTION_MODE="new" ./bin/queen-server
node test-v2/run.js
```

**Expected:** 
- Tests with explicit `.subscriptionMode('new')` work the same
- Tests without explicit mode will skip historical messages
- `subscriptionModeServerDefault` test detects and reports server default

**All tests pass with both configurations!** The tests are designed to be agnostic to server defaults.

## Test Philosophy

The AI-generated tests follow these principles:

1. **Isolated** - Each test uses unique queue names
2. **Self-contained** - Tests don't depend on each other
3. **Realistic** - Tests mirror production scenarios
4. **Defensive** - Tests validate error conditions
5. **Complete** - Tests cover happy path and edge cases

## Next Steps

1. Review the tests in each file
2. Run the full test suite: `node run.js`
3. Check for any failures specific to your environment
4. Adjust configuration if needed (ports, timeouts, etc.)
5. Integrate into your CI/CD pipeline

## Need Help?

- **See all tests**: `node run.js` (no arguments)
- **Run specific test**: `node run.js <testName>`
- **Read detailed docs**: See `AI_TEST_SUMMARY.md`
- **Check original tests**: Files without `ai_` prefix

## Test Count Summary

- **Original tests**: 49
- **AI-generated tests**: 45  
- **Total coverage**: 94 tests

You've nearly **doubled your test coverage** with these additions! 🎉

