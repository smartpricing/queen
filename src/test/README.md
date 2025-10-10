# Queen Message Queue Test Suite

This directory contains the comprehensive test suite for the Queen Message Queue system, using the new minimalist `Queen` client interface.

## Test Structure

The test suite is organized into focused, modular files:

### Core Files

- **`test-new.js`** - Main test runner that orchestrates all tests
- **`utils.js`** - Shared utilities (logging, database helpers, result tracking)

### Test Categories

#### 1. Core Features (`core-tests.js`)
- Queue creation policy
- Single/batch message push
- Queue configuration
- Take and acknowledgment
- Delayed processing
- FIFO ordering within partitions

#### 2. Partition Locking (`partition-locking-tests.js`)
- Partition locking in queue mode
- Partition locking in bus mode
- Specific partition requests with locking
- Namespace/task filtering with partition locking

#### 3. Enterprise Features (`enterprise-tests.js`)
- Message encryption/decryption
- Retention policies (pending & completed messages)
- Message eviction
- Combined enterprise features
- Enterprise error handling

#### 4. Bus Mode Features (`bus-mode-tests.js`)
- Consumer groups
- Mixed mode (queue + bus)
- Subscription modes (all vs new messages)
- Consumer group isolation

#### 5. Edge Cases (`edge-case-tests.js`)
- Empty and null payloads
- Very large payloads
- Concurrent push/take operations
- Retry limit exhaustion
- Lease expiration and redelivery
- SQL injection prevention
- XSS prevention

#### 6. Advanced Patterns (`advanced-pattern-tests.js`)
- Multi-stage pipeline workflow
- Fan-out/fan-in pattern
- Dead letter queue pattern
- Circuit breaker pattern
- Message deduplication

## New vs Old Interface

### Old Interface (test.js)
```javascript
import { createQueenClient } from '../client/queenClient.js';

const client = createQueenClient({ baseUrls: [...] });

// Configure
await client.configure({ queue: 'myqueue', options: {} });

// Push
await client.push({ items: [{ queue: 'myqueue', partition: 'Default', payload: {...} }] });

// Pop
const result = await client.pop({ queue: 'myqueue', batch: 10 });

// Ack
await client.ack(transactionId, 'completed', null, consumerGroup);
```

### New Interface (test-new.js)
```javascript
import { Queen } from '../client/client.js';

const client = new Queen({ baseUrls: [...] });

// Configure
await client.queue('myqueue', {}, { namespace, task });

// Push
await client.push('myqueue/partition', payload);

// Take (async iterator)
for await (const msg of client.take('myqueue/partition@group', { limit: 10 })) {
  // Process message
  await client.ack(msg, true, { group: 'mygroup' });
}
```

## Key Differences

1. **Address Format**: The new interface uses a unified address format:
   - `"queue"` - Simple queue
   - `"queue/partition"` - Specific partition
   - `"queue@group"` - Consumer group
   - `"queue/partition@group"` - Partition + group
   - `"namespace:name"` - Namespace filter
   - `"task:name"` - Task filter
   - `"namespace:billing/task:process"` - Combined filters

2. **Async Iterator**: `pop()` → `take()` using async iteration
   ```javascript
   for await (const msg of client.take(address, options)) {
     // Handle message
   }
   ```

3. **Simplified Methods**: 4 core methods instead of many:
   - `client.queue(name, options, metadata)` - Configure
   - `client.push(address, payload, options)` - Send
   - `client.take(address, options)` - Receive (async iterator)
   - `client.ack(message, status, context)` - Acknowledge

4. **Message Properties**: Can be in payload or options:
   ```javascript
   // In options
   await client.push('queue', { data }, { transactionId: 'txn-1' });
   
   // In payload
   await client.push('queue', { data, transactionId: 'txn-1' });
   ```

## Running Tests

```bash
# Make sure Queen servers are running first
# And database is accessible

cd /Users/alice/Work/queen

# Run all tests
nvm use 22 && node src/test/test-new.js

# Run specific test category
node src/test/test-new.js core        # Core features only
node src/test/test-new.js partition   # Partition locking tests only
node src/test/test-new.js enterprise  # Enterprise features only
node src/test/test-new.js bus         # Bus mode tests only
node src/test/test-new.js edge        # Edge cases only
node src/test/test-new.js advanced    # Advanced patterns only

# Show help
node src/test/test-new.js help
```

### Available Test Categories

- **`core`** - Core features (queue creation, push, take, ack, delayed processing, FIFO)
- **`partition`** (or `locking`) - Partition locking in queue and bus modes
- **`enterprise`** - Enterprise features (encryption, retention, eviction)
- **`bus`** - Bus mode features (consumer groups, mixed mode, subscription modes, isolation)
- **`edge`** - Edge cases (null payloads, large payloads, concurrency, SQL injection, XSS)
- **`advanced`** (or `pattern`) - Advanced patterns (pipeline, fan-out/fan-in, DLQ, circuit breaker, deduplication)

## Test Configuration

Tests use environment variables for configuration:
- `PG_HOST` - PostgreSQL host (default: localhost)
- `PG_PORT` - PostgreSQL port (default: 5432)
- `PG_DB` - Database name (default: postgres)
- `PG_USER` - Database user (default: postgres)
- `PG_PASSWORD` - Database password (default: postgres)
- `QUEEN_ENCRYPTION_KEY` - Optional encryption key for encryption tests

## Adding New Tests

1. Create a test function in the appropriate category file:
   ```javascript
   export async function testMyFeature(client) {
     startTest('My Feature Test', 'category');
     try {
       // Test code here
       passTest('Feature works correctly');
     } catch (error) {
       failTest(error);
     }
   }
   ```

2. Import and add to `test-new.js`:
   ```javascript
   import { testMyFeature } from './category-tests.js';
   
   // In runTests():
   await runTest(() => testMyFeature(client));
   ```

## Notes

- All tests automatically clean up test data before and after running
- Tests use the `test-*`, `edge-*`, `pattern-*`, and `workflow-*` queue name prefixes
- The test suite supports both single-server and multi-server configurations
- Some enterprise tests (encryption, retention) are skipped if not configured

