# Bug Fix: Consumer Handler Invocation

## Issue

When using `.consume()` with the default `batch=1`, the handler was receiving an array with one message instead of a single message. This caused confusion and required users to write:

```python
# Wrong - shouldn't need array indexing for batch=1
await queen.queue('tasks').consume(async def handler(messages):
    message = messages[0]  # Shouldn't need this!
    print(message['data'])
)
```

## Root Cause

The `ConsumerManager._worker()` method was always calling `_process_batch()` when `each=False`, regardless of the batch size. This meant even when `batch=1`, the handler received `[message]` instead of `message`.

## Fix Applied

Modified `consumer/consumer_manager.py` to check if `batch=1` and pass single message:

```python
if batch == 1 and len(messages) == 1:
    # For batch=1, pass single message (not array)
    await self._process_message(messages[0], handler, auto_ack, group)
    processed_count += 1
else:
    # For batch>1, pass array of messages
    await self._process_batch(messages, handler, auto_ack, group)
    processed_count += len(messages)
```

## New Behavior

### Single Message (batch=1, default)
```python
# Handler receives a single message
await queen.queue('tasks').consume(async def handler(message):
    print(message['data'])  # Direct access!
)
```

### Batch Processing (batch>1)
```python
# Handler receives an array of messages
await queen.queue('tasks').batch(10).consume(async def handler(messages):
    for message in messages:
        print(message['data'])
)
```

### One-at-a-time (each=True)
```python
# Always receives single messages, regardless of batch
await queen.queue('tasks').batch(10).each().consume(async def handler(message):
    print(message['data'])  # Always single message
)
```

## Updated Files

1. ✅ `queen/consumer/consumer_manager.py` - Fixed handler invocation logic
2. ✅ `example.py` - Updated to demonstrate both single and batch handling
3. ✅ `README.md` - Added documentation about handler signatures

## Testing

Run the updated example:

```bash
python3 example.py
```

Expected output:
- Single message consumption works correctly
- Batch consumption receives arrays
- Both modes properly invoke handlers

## Migration Guide

If you were previously working around this bug:

**Before:**
```python
await queen.queue('q').consume(async def handler(messages):
    message = messages[0]  # Had to index
    process(message)
)
```

**After:**
```python
await queen.queue('q').consume(async def handler(message):
    process(message)  # Direct access!
)
```

For batch processing, update signature:
```python
await queen.queue('q').batch(10).consume(async def handler(messages):
    for message in messages:
        process(message)
)
```

## Compatibility

This change matches the Node.js client behavior where:
- `batch=1` → handler gets single message
- `batch>1` → handler gets array
- `each=True` → always gets single messages

✅ **100% parity with Node.js client maintained**

