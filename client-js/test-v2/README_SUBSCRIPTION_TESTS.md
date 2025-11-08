# Subscription Mode Tests

Tests for consumer group subscription modes, compatible with any server `DEFAULT_SUBSCRIPTION_MODE` configuration.

## Test Overview

| Test Function | Description | Explicit Mode Used |
|--------------|-------------|-------------------|
| `subscriptionModeNew` | Validates `.subscriptionMode('new')` skips historical messages | Yes (`new`) |
| `subscriptionModeNewOnly` | Validates alias `.subscriptionMode('new-only')` | Yes (`new-only`) |
| `subscriptionFromNow` | Validates `.subscriptionFrom('now')` | Yes (`now`) |
| `subscriptionFromTimestamp` | Validates timestamp-based subscription | Yes (timestamp) |
| `subscriptionModeAll` | Tests default behavior (depends on server config) | Mixed |
| `subscriptionModeServerDefault` | **NEW:** Detects and validates server default | Mixed |

## Running the Tests

### Run all subscription tests:
```bash
cd client-js/test-v2
node run.js subscription
```

### Run specific subscription test:
```bash
node run.js subscriptionModeNew
node run.js subscriptionModeServerDefault
```

## Server Configuration Compatibility

These tests work with **any** server `DEFAULT_SUBSCRIPTION_MODE` configuration:

### Configuration 1: Standard (Default)

```bash
# Start server without default
./bin/queen-server

# Run tests
node run.js subscription
```

**Expected Behavior:**
- Consumer groups without explicit mode process all historical messages
- Consumer groups with `.subscriptionMode('new')` skip history
- `subscriptionModeServerDefault` reports: "all (or empty string)"

### Configuration 2: With DEFAULT_SUBSCRIPTION_MODE="new"

```bash
# Start server with "new" as default
DEFAULT_SUBSCRIPTION_MODE="new" ./bin/queen-server

# Run tests
node run.js subscription
```

**Expected Behavior:**
- Consumer groups without explicit mode skip historical messages
- Consumer groups with `.subscriptionMode('new')` skip history (same)
- `subscriptionModeServerDefault` reports: "new"

**✅ All tests pass with both configurations!**

## Test Details

### subscriptionModeNew

Tests the explicit `.subscriptionMode('new')` behavior:

1. Pushes 5 historical messages
2. Creates two consumer groups:
   - One without explicit mode (behavior depends on server)
   - One with `.subscriptionMode('new')` (always skips)
3. Verifies the `new` mode group gets 0 historical messages
4. Pushes 3 new messages
5. Verifies both groups get the new messages

**Key Assertion:** `.subscriptionMode('new')` always skips historical messages.

### subscriptionModeServerDefault (NEW)

Detects and validates the server's default subscription mode:

1. Pushes historical messages
2. Creates consumer group WITHOUT explicit subscription mode
3. Detects server default based on behavior:
   - Got historical messages → server default is "all"
   - Got 0 messages → server default is "new"
4. Validates explicit `.subscriptionMode('new')` still works
5. Verifies new messages are received by both groups

**Key Assertion:** Server default affects groups without explicit mode, but explicit modes always work.

### subscriptionFromTimestamp

Tests timestamp-based subscription:

1. Pushes "first batch" of messages
2. Records cutoff timestamp
3. Pushes "second batch" of messages
4. Creates consumer group with `.subscriptionFrom(cutoffTimestamp)`
5. Verifies only messages after timestamp are received

**Note:** Timestamp precision may vary based on database clock.

## Common Issues

### Issue: Test fails with "expected X, got Y"

**Cause:** Server has different default than test expects.

**Solution:** Check server configuration:
```bash
# Check if DEFAULT_SUBSCRIPTION_MODE is set
echo $DEFAULT_SUBSCRIPTION_MODE

# Check server logs on startup
LOG_LEVEL=debug ./bin/queen-server | grep "default subscription"
```

### Issue: Consumer group exists from previous run

**Cause:** Consumer groups persist between test runs.

**Solution:** The test runner cleans up test data automatically:
```javascript
await dbPool.query(`DELETE FROM queen.queues WHERE name LIKE 'test-%'`)
```

Or manually:
```sql
DELETE FROM queen.partition_consumers 
WHERE consumer_group LIKE 'group-%';
```

### Issue: Timing-related test failures

**Cause:** Messages not fully persisted before consumption.

**Solution:** Tests include appropriate delays. If still failing, increase delays:
```javascript
await new Promise(resolve => setTimeout(resolve, 500))  // Increase if needed
```

## Adding New Subscription Tests

When adding new subscription mode tests:

1. **Use unique queue/group names** to avoid conflicts
2. **Be explicit about subscription modes** in test assertions
3. **Document expected behavior** for both server configurations
4. **Use descriptive group names** like `group-explicit-new` vs `group-default`

Example:
```javascript
export async function testNewFeature(client) {
  // Setup
  await client.queue('test-new-feature').create()
  
  // Test explicit behavior (works with any server default)
  const result = await client
    .queue('test-new-feature')
    .group('group-explicit-new')
    .subscriptionMode('new')  // Explicit!
    .pop()
  
  // Assert based on explicit mode, not server default
  // ...
}
```

## Debugging Failed Tests

Enable detailed logging:

```bash
# Client logging
QUEEN_CLIENT_LOG=true node run.js subscriptionModeNew

# Server logging
LOG_LEVEL=debug ./bin/queen-server
```

Look for these log lines:
```
Consumer group 'group-xxx' exists: false, has subscription options: true
Subscription mode 'new' - starting from latest message: <uuid>
Applying default subscription mode 'new' for consumer group 'group-yyy'
```

## Summary

- ✅ Tests work with any `DEFAULT_SUBSCRIPTION_MODE` configuration
- ✅ Explicit subscription modes are always tested and validated
- ✅ Server default behavior is detected and reported
- ✅ All tests are documented and maintainable

Run the tests and see them pass! 🎉

