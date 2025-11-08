# DEFAULT_SUBSCRIPTION_MODE Feature

**Status:** ✅ Implemented and Documented

## Summary

Added a configurable server-wide default subscription mode for consumer groups. This allows operators to control whether new consumer groups automatically process historical messages or skip them.

## Changes Made

### 1. Server Implementation

**Files Modified:**
- `server/include/queen/poll_intention_registry.hpp` - Added subscription fields to PollIntention
- `server/include/queen/config.hpp` - Added DEFAULT_SUBSCRIPTION_MODE config and FILE_BUFFER_DIR fix
- `server/src/acceptor_server.cpp` - Pass subscription options through poll intentions (3 endpoints)
- `server/src/services/poll_worker.cpp` - Use subscription options when polling
- `server/src/managers/async_queue_manager.cpp` - Apply default subscription mode when not specified
- `client-js/client-v2/consumer/ConsumerManager.js` - Clean code (removed debug logging)

**Bug Fixed:**
- `subscriptionMode` now works correctly with long polling (wait=true)
- Previously, subscription options were lost in the PollIntention registry
- Poll workers now correctly use subscription options when executing queries

### 2. Documentation Updates

**Server Documentation:**
- ✅ `server/ENV_VARIABLES.md` - Added DEFAULT_SUBSCRIPTION_MODE documentation with use cases
- ✅ `docs/SERVER_FEATURES.md` - Updated subscription modes section with accurate API
- ✅ `docs/SUBSCRIPTION_MODES.md` - **NEW** comprehensive guide

**Website Documentation:**
- ✅ `website/guide/consumer-groups.md` - Updated with DEFAULT_SUBSCRIPTION_MODE info and fixed API
- ✅ `client-js/client-v2/README.md` - Added note about server default configuration

**Project Documentation:**
- ✅ `README.md` - Added subscription modes default configuration section

**Examples:**
- ✅ `examples/22-subscription-modes.js` - **NEW** comprehensive example showing all modes
- ✅ `examples/21-consumer-group.js` - Added comments about subscription modes and server default

## Configuration

### Environment Variable

```bash
export DEFAULT_SUBSCRIPTION_MODE="new"
./bin/queen-server
```

**Valid Values:**
- `""` (empty) - Process all messages (default, backward compatible)
- `"new"` - Skip historical messages
- `"new-only"` - Same as "new"

### Client Override

Clients can always override the server default:

```javascript
// Use server default (whatever it is)
await queen.queue('events').group('processor').consume(...)

// Explicit: Skip history (even if server default is all)
await queen.queue('events').group('processor').subscriptionMode('new').consume(...)

// Explicit: Process all (even if server default is new)
await queen.queue('events').group('processor').subscriptionMode('all').consume(...)
```

## Use Cases

### Real-Time Systems

**Problem:** New consumer groups accidentally process millions of historical messages.

**Solution:**
```bash
export DEFAULT_SUBSCRIPTION_MODE="new"
./bin/queen-server
```

Now all new consumer groups automatically skip history unless explicitly overridden.

### Analytics Systems

**Problem:** Need to process all historical data for analytics.

**Solution:** Keep default as `""` (all messages), or explicitly use:
```javascript
.subscriptionMode('all')
```

### Mixed Workloads

**Problem:** Some consumer groups need history, others don't.

**Solution:** Set server default based on most common case, let clients override:
```bash
# If most consumers are real-time
export DEFAULT_SUBSCRIPTION_MODE="new"

# Analytics consumers explicitly override
.group('analytics').subscriptionMode('all').consume(...)
```

## Backward Compatibility

✅ **Fully backward compatible**

- Default is `""` (empty string) which maintains existing behavior
- Existing consumer groups are not affected
- Clients without subscription mode work exactly as before
- Only affects NEW consumer groups when server default is changed

## Testing

### Verify Bug Fix (subscriptionMode with long polling)

```bash
# Start server
./bin/queen-server

# Run test
node examples/22-subscription-modes.js
```

**Expected:** Example 2 should receive only 2 "new" messages, skipping 5 historical messages.

### Verify Server Default

```bash
# Start server with default
DEFAULT_SUBSCRIPTION_MODE="new" ./bin/queen-server

# Create new consumer group without explicit subscriptionMode
node -e '
import("./client-js/client-v2/index.js").then(async ({ Queen }) => {
  const q = new Queen("http://localhost:6632");
  await q.queue("test").push([{data:{id:1}}]);
  await new Promise(r => setTimeout(r, 100));
  let count = 0;
  await q.queue("test").group("new-group").limit(1).consume(async () => count++);
  console.log("Received:", count, "(expected: 0 if default works)");
  await q.close();
});
'
```

**Expected:** Should receive 0 messages (skipped the historical message pushed before subscription).

## Documentation Locations

Quick links to updated documentation:

- **Server Config:** [server/ENV_VARIABLES.md](server/ENV_VARIABLES.md) - Lines 180-199
- **Complete Guide:** [docs/SUBSCRIPTION_MODES.md](docs/SUBSCRIPTION_MODES.md) - New comprehensive guide
- **Server Features:** [docs/SERVER_FEATURES.md](docs/SERVER_FEATURES.md) - Lines 162-227
- **Consumer Groups:** [website/guide/consumer-groups.md](website/guide/consumer-groups.md) - Lines 95-156
- **Client API:** [client-js/client-v2/README.md](client-js/client-v2/README.md) - Lines 373-380
- **Main README:** [README.md](README.md) - Lines 90-97
- **Examples:** 
  - [examples/22-subscription-modes.js](examples/22-subscription-modes.js) - Comprehensive examples
  - [examples/21-consumer-group.js](examples/21-consumer-group.js) - Updated with comments

## Related Issues

This feature solves the original problem:

**Question:** "What happens if I delete a partition consumer?"

**Answer:** With `DEFAULT_SUBSCRIPTION_MODE="new"` configured, you can safely delete stuck consumer groups without worrying about reprocessing all historical messages when they reconnect. They will automatically skip history and only process new messages.

**Example:**
```sql
-- Remove stuck consumer group
DELETE FROM queen.partition_consumers 
WHERE consumer_group = 'wa-lite-393406147751';
```

If the consumer reconnects with server configured as `DEFAULT_SUBSCRIPTION_MODE="new"`, it will only process new messages going forward (assuming the client code doesn't explicitly override).

