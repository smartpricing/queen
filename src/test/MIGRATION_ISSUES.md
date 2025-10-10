# Test Migration Issues and Fixes

## Summary

When migrating tests from the old `queenClient` interface to the new minimalist `Queen` client interface, we discovered client bugs that needed fixing. The system is transactional - ACKs immediately release partition locks when awaited.

## Client Bugs Fixed

### 1. Null Payload Handling in `push()`
**Issue**: `typeof null === 'object'` is `true` in JavaScript, causing the code to try to access properties on `null` payloads.

**Location**: `src/client/client.js` line 153

**Fix**:
```javascript
// Before
const isMessageObject = typeof item === 'object' && 
  (item._payload || item._transactionId || item._traceId);

// After  
const isMessageObject = typeof item === 'object' && item !== null &&
  (item._payload || item._transactionId || item._traceId);
```

**Why**: Tests correctly validate edge cases including null payloads. The client must handle them gracefully.

### 2. Null Handling in `ack()`
**Issue**: Same null-checking issue when extracting transaction ID from message object.

**Location**: `src/client/client.js` line 330

**Fix**:
```javascript
// Before
const transactionId = typeof message === 'object' ? 
  (message.transactionId || message.id) : 
  message;

// After
const transactionId = typeof message === 'object' && message !== null ? 
  (message.transactionId || message.id) : 
  message;
```

### 3. Consumer Group Parsing for Namespace/Task Addresses
**Issue**: The `#parseAddress()` method didn't extract consumer groups from namespace/task patterns like `namespace:billing@group-a`.

**Location**: `src/client/client.js` line 77-101

**Fix**: Extract `@group` portion before parsing namespace/task segments:
```javascript
#parseAddress(address) {
  if (address.includes('namespace:') || address.includes('task:')) {
    const parts = {};
    
    // Extract consumer group if present (after @)
    let workingAddress = address;
    const atIndex = address.lastIndexOf('@');
    if (atIndex > 0) {
      parts.consumerGroup = address.substring(atIndex + 1);
      workingAddress = address.substring(0, atIndex);
    }
    
    const segments = workingAddress.split('/');
    for (const segment of segments) {
      if (segment.startsWith('namespace:')) {
        parts.namespace = segment.substring(10);
      } else if (segment.startsWith('task:')) {
        parts.task = segment.substring(5);
      }
    }
    return parts;
  }
  // ...
}
```

### 4. Defensive Filtering of Null Messages
**Issue**: Server might return null values in the `messages` array response.

**Location**: `src/client/client.js` in `take()` method

**Fix**: Filter out null/undefined messages when yielding:
```javascript
// Yield each message (filter out any null/undefined values)
for (const message of result.messages) {
  if (message) { // Skip null/undefined messages
    yield message;
    count++;
    if (limit && count >= limit) return;
  }
}
```

## Test Fixes

### 1. Consumer Group Isolation Test
**Problem**: Test was ACKing all messages as completed for both groups, but the test expects group2 to have some failed messages to verify isolation.

**Solution**: Actually fail the first 2 messages for group2:
```javascript
// Fail first 2 messages for group2, complete the rest
for (let i = 0; i < group2Messages.length; i++) {
  if (i < 2) {
    await client.ack(group2Messages[i], false, { 
      error: 'Test failure', 
      group: 'group2' 
    });
  } else {
    await client.ack(group2Messages[i], true, { group: 'group2' });
  }
}
```

### 2. Empty and Null Payloads Test
**Problem**: If server returns null message objects in the array, the test would crash.

**Solution**: Add defensive check (though client now filters these):
```javascript
for await (const msg of client.take(queue, { limit: 10 })) {
  if (!msg) {
    throw new Error('Received null/undefined message from take()');
  }
  messages.push(msg);
  await client.ack(msg);
}
```

## Key Learnings

### System is Transactional ✅

The Queen system is **transactional**. When you `await client.ack(msg)`:
- The ACK completes atomically
- The partition lock is released immediately
- No "timing issues" or delays needed

### Correct Pattern
```javascript
// ✅ This works correctly:
for await (const msg of client.take(queue, { limit: 10 })) {
  await client.ack(msg); // Partition unlocked when this completes
}

// Next consumer can immediately access the partition:
for await (const msg of client.take(queue, { limit: 10 })) {
  // Gets messages right away
}
```

### Edge Case Handling

1. **Null payloads are valid**: Tests should validate that the system handles `null`, `undefined`, empty objects, and empty strings as payloads.

2. **Client must be defensive**: Check for `typeof x === 'object' && x !== null` when you need to access properties.

3. **Server responses should be filtered**: If server returns null messages in an array, filter them out client-side.

### Test Design Principles

1. **Test real edge cases**: null, undefined, empty strings, large payloads, etc.

2. **Don't add artificial delays**: The system is transactional. If you need a delay, something is wrong.

3. **Verify isolation**: When testing consumer groups, actually test that different groups can have different statuses for the same messages.

4. **Use appropriate batch sizes**: Match batch size to expected message count for efficient testing.

## Final Results

After fixing client bugs and test issues:
- 33/33 tests should pass
- No timing sensitivity
- Clean, transactional behavior throughout
