# Python Client Guide

Complete guide for the Queen MQ Python client library.

## Installation

```bash
pip install queen-mq
```

**Requirements:** Python 3.8+

## Table of Contents

[[toc]]

## Getting Started

### Import and Connect

```python
import asyncio
from queen import Queen

async def main():
    # Single server
    queen = Queen('http://localhost:6632')
    
    # Multiple servers (high availability)
    queen = Queen([
        'http://server1:6632',
        'http://server2:6632'
    ])
    
    # Full configuration
    queen = Queen({
        'urls': ['http://server1:6632', 'http://server2:6632', 'http://server3:6632'],
        'timeout_millis': 30000,
        'retry_attempts': 3,
        'load_balancing_strategy': 'affinity',  # 'affinity', 'round-robin', or 'session'
        'affinity_hash_ring': 128,              # Virtual nodes per server (for affinity)
        'enable_failover': True
    })
    
    # Recommended: Use async context manager for automatic cleanup
    async with Queen('http://localhost:6632') as queen:
        # Your code here
        pass

asyncio.run(main())
```

## Load Balancing & Affinity Routing

When connecting to multiple Queen servers, you can choose how requests are distributed. The client supports three load balancing strategies.

### Affinity Mode (Recommended for Production)

**Best for:** Production deployments with 3+ servers and multiple consumer groups.

Uses consistent hashing with virtual nodes to route consumer groups to the same backend server. This optimizes database queries by consolidating poll intentions.

```python
queen = Queen({
    'urls': ['http://server1:6632', 'http://server2:6632', 'http://server3:6632'],
    'load_balancing_strategy': 'affinity',
    'affinity_hash_ring': 128  # Virtual nodes per server (default: 128)
})
```

**Benefits:**
- ✅ Same consumer group always routes to same server
- ✅ Poll intentions consolidated → optimized DB queries
- ✅ Graceful failover (only ~33% of keys move if server fails)
- ✅ Works great with 3-server HA setup

**How it works:**
1. Each consumer group generates an affinity key: `queue:partition:consumerGroup`
2. Key is hashed (FNV-1a) and mapped to a virtual node on the ring
3. Virtual node maps to a real backend server
4. Same key always routes to same server

### Round-Robin Mode

Cycles through servers in order. Simple but doesn't optimize for poll intention consolidation.

```python
queen = Queen({
    'urls': ['http://server1:6632', 'http://server2:6632'],
    'load_balancing_strategy': 'round-robin'
})
```

### Session Mode

Sticky sessions - each client instance sticks to one server.

```python
queen = Queen({
    'urls': ['http://server1:6632', 'http://server2:6632'],
    'load_balancing_strategy': 'session'
})
```

## Quick Start Examples

### Basic Push and Consume

```python
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        # Create a queue
        await queen.queue('tasks').create()
        
        # Push messages
        await queen.queue('tasks').push([
            {'data': {'task': 'send-email', 'to': 'alice@example.com'}}
        ])
        
        # Consume messages
        async def handler(message):
            print('Processing:', message['data'])
            # Auto-ack on success, auto-retry on error
        
        await queen.queue('tasks').consume(handler)

asyncio.run(main())
```

### With Partitions

```python
# Push to specific partition
await queen.queue('user-events').partition('user-123').push([
    {'data': {'event': 'login', 'timestamp': '2025-11-22T10:00:00Z'}}
])

# Consume from specific partition
async def handler(message):
    print('User 123 event:', message['data'])

await queen.queue('user-events').partition('user-123').consume(handler)
```

### Consumer Groups

```python
# Worker 1 in group "processors"
async def worker1():
    await queen.queue('emails').group('processors').consume(async def handler(msg):
        print('Worker 1 processing:', msg['data'])
    )

# Worker 2 in the SAME group (shares the load)
async def worker2():
    await queen.queue('emails').group('processors').consume(async def handler(msg):
        print('Worker 2 processing:', msg['data'])
    )

# Run both workers concurrently
await asyncio.gather(worker1(), worker2())
```

## Queue Operations

### Creating Queues

```python
# Simple queue
await queen.queue('my-tasks').create()

# With configuration
await queen.queue('orders').config({
    'leaseTime': 300,              # 5 minutes
    'retryLimit': 3,
    'priority': 5,
    'dlqAfterMaxRetries': True,
    'encryptionEnabled': False
}).create()
```

### Deleting Queues

```python
await queen.queue('my-tasks').delete()
```

### Queue Configuration Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `leaseTime` | int | 300 | Lease duration in seconds |
| `retryLimit` | int | 3 | Max retry attempts |
| `priority` | int | 0 | Queue priority (0-10) |
| `delayedProcessing` | int | 0 | Delay before messages become available (seconds) |
| `windowBuffer` | int | 0 | Server-side batching window (seconds) |
| `maxSize` | int | 0 | Max queue size (0 = unlimited) |
| `retentionSeconds` | int | 0 | Message retention time (0 = forever) |
| `completedRetentionSeconds` | int | 0 | Completed message retention |
| `encryptionEnabled` | bool | False | Enable payload encryption |
| `dlqAfterMaxRetries` | bool | False | Move to DLQ after max retries |

## Pushing Messages

### Basic Push

```python
# Single message
await queen.queue('tasks').push([
    {'data': {'job': 'resize-image', 'imageId': 123}}
])

# Multiple messages
await queen.queue('tasks').push([
    {'data': {'job': 'send-email', 'to': 'alice@example.com'}},
    {'data': {'job': 'send-email', 'to': 'bob@example.com'}},
    {'data': {'job': 'resize-image', 'id': 123}}
])
```

### With Partitions

```python
await queen.queue('user-events').partition('user-123').push([
    {'data': {'event': 'login'}},
    {'data': {'event': 'view-page'}},
    {'data': {'event': 'logout'}}
])
```

### With Custom Transaction IDs (Exactly-Once)

```python
await queen.queue('orders').push([
    {'transactionId': 'order-12345', 'data': {'orderId': 12345, 'amount': 99.99}}
])
```

### Client-Side Buffering (High Performance)

For high-throughput scenarios, buffer messages client-side before sending:

```python
# Buffer up to 500 messages OR 1 second (whichever comes first)
for i in range(10000):
    await queen.queue('events').buffer({
        'message_count': 500,
        'time_millis': 1000
    }).push([
        {'data': {'id': i, 'timestamp': time.time()}}
    ])

# Flush remaining buffered messages
await queen.flush_all_buffers()

# Result: 10x-100x faster than individual pushes
```

### Push with Callbacks

```python
await (queen.queue('tasks')
    .push([{'data': {'value': 1}}])
    .on_success(async def on_success(messages):
        print('Push successful!')
    )
    .on_error(async def on_error(messages, error):
        print(f'Push failed: {error}')
    )
    .on_duplicate(async def on_duplicate(messages, error):
        print('Duplicate transaction IDs detected')
    ))
```

## Consuming Messages

### Pop vs Consume

**Pop** - Manual control, one-shot retrieval:
```python
messages = await queen.queue('tasks').batch(10).pop()
for message in messages:
    try:
        await process_message(message['data'])
        await queen.ack(message, True)
    except Exception as e:
        await queen.ack(message, False)
```

**Consume** - Long-running workers, automatic retry:
```python
# Runs forever, processing messages as they arrive
async def handler(message):
    await process_task(message['data'])
    # Auto-ack on success, auto-retry on error

await queen.queue('tasks').consume(handler)
```

### Handler Signatures

**Important:** The handler signature depends on batch size:

```python
# batch=1 (default): handler receives single message
async def handler(message):
    print(message['data'])

await queen.queue('tasks').consume(handler)

# batch>1: handler receives array of messages
async def batch_handler(messages):
    for message in messages:
        print(message['data'])

await queen.queue('tasks').batch(10).consume(batch_handler)

# each=True: always receives single messages
async def each_handler(message):
    print(message['data'])

await queen.queue('tasks').batch(10).each().consume(each_handler)
```

### Consume Options

```python
await (queen.queue('tasks')
    .concurrency(5)          # 5 parallel workers
    .batch(20)               # Fetch 20 at a time
    .limit(100)              # Stop after 100 messages
    .idle_millis(5000)       # Stop after 5s of no messages
    .auto_ack(True)          # Auto-ack (default)
    .wait(True)              # Long polling (default)
    .consume(handler))
```

### Pop Options

```python
messages = await (queen.queue('tasks')
    .batch(10)               # Fetch 10 at a time
    .wait(True)              # Long polling (wait for messages)
    .pop())
```

## Partitions

Partitions provide ordered message processing within a queue.

### Creating Partitioned Messages

```python
# Messages in same partition are processed in order
await queen.queue('user-events').partition('user-123').push([
    {'data': {'event': 'login'}},
    {'data': {'event': 'view-page'}},
    {'data': {'event': 'logout'}}
])
```

### Consuming from Partitions

```python
# Process only messages from user-123's partition
async def handler(message):
    print('User 123 did:', message['data']['event'])

await queen.queue('user-events').partition('user-123').consume(handler)
```

## Consumer Groups

Consumer groups enable multiple workers to share message processing while ensuring each message is processed exactly once per group.

### Basic Consumer Groups

```python
# Worker 1 in group "processors"
async def worker1():
    async def handler(message):
        print('Worker 1:', message['data'])
    
    await queen.queue('emails').group('processors').consume(handler)

# Worker 2 in SAME group (shares the load)
async def worker2():
    async def handler(message):
        print('Worker 2:', message['data'])
    
    await queen.queue('emails').group('processors').consume(handler)

# Run both workers
await asyncio.gather(worker1(), worker2())
```

### Multiple Consumer Groups (Fan-Out)

```python
# Group 1: Send emails
async def email_worker():
    async def handler(message):
        await send_email(message['data'])
    
    await queen.queue('notifications').group('email-sender').consume(handler)

# Group 2: Log analytics (processes THE SAME messages)
async def analytics_worker():
    async def handler(message):
        await track_event(message['data'])
    
    await queen.queue('notifications').group('analytics').consume(handler)

# Both groups process every message independently
await asyncio.gather(email_worker(), analytics_worker())
```

## Subscription Modes

Control whether consumer groups process historical messages or only new messages.

### Default Behavior (All Messages)

By default, consumer groups start from the beginning and process all messages:

```python
# Processes ALL messages, including historical ones
async def handler(message):
    print('Processing:', message['data'])

await queen.queue('events').group('new-analytics').consume(handler)
```

### Subscription Mode: 'new'

Skip historical messages and process only messages that arrive after subscription:

```python
# Skip history, only process new messages
async def handler(message):
    print('New event:', message['data'])

await (queen.queue('events')
    .group('realtime-monitor')
    .subscription_mode('new')
    .consume(handler))
```

### Subscription From Timestamp

Start consuming from a specific point in time:

```python
# Start from specific timestamp
start_time = '2025-10-28T10:00:00.000Z'

async def handler(message):
    print('Processing:', message['data'])

await (queen.queue('events')
    .group('replay-from-10am')
    .subscription_from(start_time)
    .consume(handler))

# Start from now
await (queen.queue('events')
    .group('from-now')
    .subscription_from('now')
    .consume(handler))

# Start from 1 hour ago
from datetime import datetime, timedelta
one_hour_ago = (datetime.utcnow() - timedelta(hours=1)).isoformat() + 'Z'

await (queen.queue('events')
    .group('last-hour')
    .subscription_from(one_hour_ago)
    .consume(handler))
```

### Real-World Example

```python
# Group 1: Process ALL messages (batch processing)
async def batch_analytics():
    async def handler(message):
        await generate_full_report(message['data'])
    
    await queen.queue('user-actions').group('batch-analytics').consume(handler)

# Group 2: Only NEW messages (real-time monitoring)
async def realtime_alerts():
    async def handler(message):
        await send_realtime_alert(message['data'])
    
    await (queen.queue('user-actions')
        .group('realtime-alerts')
        .subscription_mode('new')
        .consume(handler))

# Group 3: Replay from specific time
async def debug_replay():
    async def handler(message):
        await debug_specific_timeframe(message['data'])
    
    await (queen.queue('user-actions')
        .group('debug-replay')
        .subscription_from('2025-10-28T15:30:00.000Z')
        .consume(handler))

# Run all three groups concurrently
await asyncio.gather(batch_analytics(), realtime_alerts(), debug_replay())
```

## Acknowledgment

### Manual Acknowledgment

```python
# Success
await queen.ack(message, True)

# Failure (will retry)
await queen.ack(message, False)

# With error context
await queen.ack(message, False, {'error': 'Invalid data format'})

# Batch acknowledgment
await queen.ack([msg1, msg2, msg3], True)
```

### Auto-Acknowledgment

```python
# Auto-ack enabled (default for consume)
async def handler(message):
    await process(message['data'])
    # Automatically acked on success
    # Automatically nacked on exception

await queen.queue('tasks').consume(handler)

# Disable auto-ack for manual control
async def handler(message):
    result = await process(message['data'])
    if result.success:
        await queen.ack(message, True)
    else:
        await queen.ack(message, False)

await queen.queue('tasks').auto_ack(False).consume(handler)
```

### Batch Ack with Mixed Results

```python
messages = await queen.queue('tasks').batch(10).pop()

# Process and mark each message individually
for message in messages:
    try:
        await process_message(message['data'])
        message['_status'] = True  # Mark as success
    except Exception as error:
        message['_status'] = False  # Mark as failure
        message['_error'] = str(error)

# Batch ack with individual statuses
await queen.ack(messages)
# Queen will ack some and nack others based on _status
```

## Transactions

Transactions provide atomic operations across acknowledgments and pushes.

### Basic Transaction

```python
# Pop from input queue
messages = await queen.queue('raw-data').pop()

if messages:
    message = messages[0]
    
    # Process it
    processed = await transform_data(message['data'])
    
    # Atomically: ack the input AND push the output
    await (queen.transaction()
        .ack(message)
        .queue('processed-data')
        .push([{'data': processed}])
        .commit())
```

### Multi-Queue Pipeline

```python
# Pop from queue A
messages = await queen.queue('queue-a').pop()

# Transaction: ack from A, push to B and C
await (queen.transaction()
    .ack(messages[0])
    .queue('queue-b')
    .push([{'data': {'step': 2}}])
    .queue('queue-c')
    .push([{'data': {'step': 2}}])
    .commit())
```

### Batch Transaction

```python
# Pop multiple messages
messages = await queen.queue('inputs').batch(10).pop()

# Process them
results = [await process(m['data']) for m in messages]

# Atomically ack all inputs and push all outputs
txn = queen.transaction()

# Ack all inputs
for message in messages:
    txn.ack(message)

# Push all outputs
txn.queue('outputs').push([{'data': r} for r in results])

await txn.commit()
```

### Transaction with Consumer

```python
async def handler(message):
    # Process
    result = await process_message(message['data'])
    
    # Transactionally ack and push result
    await (queen.transaction()
        .ack(message)
        .queue('destination')
        .push([{'data': result}])
        .commit())

# Must disable auto-ack for manual transaction
await queen.queue('source').auto_ack(False).consume(handler)
```

## Client-Side Buffering

Boost throughput by 10x-100x with client-side buffering.

### How It Works

Instead of sending messages immediately:
1. Messages collect in a local buffer
2. Buffer flushes when it reaches **count** or **time** threshold
3. All buffered messages sent in one HTTP request

### Basic Buffering

```python
# Buffer up to 100 messages OR 1 second
await queen.queue('logs').buffer({
    'message_count': 100,
    'time_millis': 1000
}).push([
    {'data': {'level': 'info', 'message': 'User logged in'}}
])
```

### High-Throughput Example

```python
import time

# Send 10,000 messages super fast
for i in range(10000):
    await queen.queue('events').buffer({
        'message_count': 500,
        'time_millis': 100
    }).push([
        {'data': {'id': i, 'timestamp': time.time()}}
    ])

# Flush any remaining buffered messages
await queen.flush_all_buffers()

# Performance: Seconds instead of minutes!
```

### Manual Flush

```python
# Flush all buffers for all queues
await queen.flush_all_buffers()

# Flush a specific queue's buffer
await queen.queue('my-queue').flush_buffer()

# Get buffer statistics
stats = queen.get_buffer_stats()
print('Buffers:', stats)
# Example: {'activeBuffers': 2, 'totalBufferedMessages': 145, ...}
```

## Dead Letter Queue (DLQ)

Handle failed messages gracefully.

### Enable DLQ

```python
await queen.queue('risky-business').config({
    'retryLimit': 3,
    'dlqAfterMaxRetries': True
}).create()
```

### Query DLQ

```python
# Get failed messages
dlq = await queen.queue('risky-business').dlq().limit(10).get()

print(f"Found {dlq['total']} failed messages")

for message in dlq['messages']:
    print('Failed message:', message['data'])
    print('Error:', message.get('errorMessage'))
    print('Failed at:', message.get('dlqTimestamp'))
```

### DLQ with Consumer Groups

```python
# Check DLQ for specific consumer group
dlq = await (queen.queue('risky-business')
    .dlq('my-consumer-group')
    .limit(100)
    .get())
```

### Advanced DLQ Queries

```python
# Query with time range and pagination
dlq = await (queen.queue('risky-business')
    .dlq()
    .from_('2025-01-01')  # Note: from_ (underscore to avoid Python keyword)
    .to('2025-01-31')
    .limit(100)
    .offset(0)
    .get())
```

## Lease Renewal

### Why Lease Renewal?

When processing takes longer than the lease time, you must renew the lease to prevent the message from being redelivered.

### Automatic Renewal (Recommended)

```python
# Auto-renew every 60 seconds
async def handler(message):
    # Can take hours - lease keeps renewing!
    await process_very_long_task(message['data'])

await (queen.queue('long-tasks')
    .renew_lease(True, 60000)
    .consume(handler))
```

### Manual Renewal

```python
# Pop a message
messages = await queen.queue('long-tasks').pop()
message = messages[0]

# Create renewal task
async def renew_task():
    while True:
        await asyncio.sleep(30)  # Every 30 seconds
        await queen.renew(message)
        print('Lease renewed!')

renewal = asyncio.create_task(renew_task())

try:
    await process_very_long_task(message['data'])
    await queen.ack(message, True)
finally:
    renewal.cancel()
```

### Batch Renewal

```python
messages = await queen.queue('tasks').batch(10).pop()

# Renew all at once
await queen.renew(messages)

# Or renew by lease IDs
await queen.renew([msg['leaseId'] for msg in messages])
```

## Message Tracing

Debug distributed workflows by recording trace events as messages are processed.

### Basic Tracing

```python
async def handler(msg):
    # Record a trace event
    await msg['trace']({
        'data': {'text': 'Order processing started'}
    })
    
    order = await process_order(msg['data'])
    
    await msg['trace']({
        'data': {
            'text': 'Order processed successfully',
            'orderId': order['id'],
            'total': order['total']
        }
    })

await queen.queue('orders').consume(handler)
```

### Trace Names - Cross-Service Correlation

Link traces across multiple services:

```python
# Service 1: Order Service
async def order_handler(msg):
    order_id = msg['data']['orderId']
    
    await msg['trace']({
        'traceName': f"order-{order_id}",
        'data': {'text': 'Order created', 'service': 'orders'}
    })
    
    # Push to inventory queue
    await queen.queue('inventory').push([{
        'data': {'orderId': order_id, 'items': msg['data']['items']}
    }])

await queen.queue('orders').consume(order_handler)

# Service 2: Inventory Service
async def inventory_handler(msg):
    order_id = msg['data']['orderId']
    
    await msg['trace']({
        'traceName': f"order-{order_id}",  # Same name = connected!
        'data': {'text': 'Stock checked', 'service': 'inventory'}
    })

await queen.queue('inventory').consume(inventory_handler)

# View in webapp: Traces → Search "order-12345" → See entire workflow!
```

### Multi-Dimensional Tracing

```python
async def handler(msg):
    tenant_id = msg['data']['tenantId']
    room_id = msg['data']['roomId']
    user_id = msg['data']['userId']
    
    await msg['trace']({
        'traceName': [
            f"tenant-{tenant_id}",
            f"room-{room_id}",
            f"user-{user_id}"
        ],
        'data': {'text': 'Message sent'}
    })

await queen.queue('chat-messages').consume(handler)

# Now you can search by:
# - tenant-acme (all tenant activity)
# - room-123 (all room activity)
# - user-456 (all user activity)
```

### Event Types

```python
async def handler(msg):
    await msg['trace']({
        'eventType': 'info',
        'data': {'text': 'Started processing'}
    })
    
    await msg['trace']({
        'eventType': 'step',
        'data': {'text': 'Validated data'}
    })
    
    await msg['trace']({
        'eventType': 'processing',
        'data': {'text': 'Sending email'}
    })
    
    # Available types: info, error, step, processing, warning

await queen.queue('analytics').consume(handler)
```

### Error-Safe Tracing

Traces **never crash** your consumer - errors are logged but don't throw:

```python
async def handler(msg):
    try:
        await msg['trace']({'data': {'text': 'Job started'}})
        result = await compute_analytics(msg['data'])
        await msg['trace']({
            'data': {
                'text': 'Job completed',
                'recordsProcessed': result['count']
            }
        })
    except Exception as error:
        # Record error (won't crash!)
        await msg['trace']({
            'eventType': 'error',
            'data': {
                'text': 'Job failed',
                'error': str(error)
            }
        })
        raise  # Still fail the message for retry

await queen.queue('jobs').consume(handler)
```

## Namespaces & Tasks

Process messages across multiple queues using wildcards.

### Namespaces

```python
# Create queues with namespaces
await queen.queue('billing-invoices').namespace('accounting').create()
await queen.queue('billing-receipts').namespace('accounting').create()

# Consume from ALL queues in 'accounting' namespace
async def handler(message):
    print('Accounting message:', message['data'])

await queen.queue().namespace('accounting').consume(handler)
```

### Tasks

```python
# Create queues with tasks
await queen.queue('video-uploads').task('video-processing').create()
await queen.queue('image-uploads').task('image-processing').create()

# Consume by task type
async def handler(message):
    await process_video(message['data'])

await queen.queue().task('video-processing').consume(handler)
```

### Combining Namespace + Task

```python
# Super specific filtering
async def handler(message):
    await process_urgent_media(message['data'])

await (queen.queue()
    .namespace('media')
    .task('urgent-processing')
    .consume(handler))
```

## Priority Queues

Higher priority queues are processed first when consuming from multiple queues.

```python
# Create queues with different priorities
await queen.queue('urgent-alerts').config({'priority': 10}).create()
await queen.queue('regular-tasks').config({'priority': 5}).create()
await queen.queue('background-jobs').config({'priority': 1}).create()

# Consumer processes by priority: urgent first, then regular, then background
async def handler(message):
    print('Processing:', message)

await queen.queue().namespace('all').consume(handler)
```

## Delayed Processing

Messages don't become available until the delay passes.

```python
# Messages invisible for 60 seconds
await queen.queue('scheduled-tasks').config({
    'delayedProcessing': 60
}).create()

# Push a message
await queen.queue('scheduled-tasks').push([
    {'data': {'task': 'send-reminder'}}
])

# Pop immediately: gets nothing
now = await queen.queue('scheduled-tasks').pop()
print(now)  # []

# Wait 60 seconds...
await asyncio.sleep(61)

# Pop again: now we get the message!
later = await queen.queue('scheduled-tasks').pop()
print(later)  # [{'data': {'task': 'send-reminder'}}]
```

## Callbacks & Error Handling

### Success and Error Callbacks

```python
async def handler(message):
    return await process_message(message['data'])

async def on_success(message, result):
    print('Success! Result:', result)

async def on_error(message, error):
    print('Failed:', str(error))
    # Custom retry logic
    if 'temporary' in str(error):
        await queen.ack(message, False)  # Retry
    else:
        await queen.ack(message, 'failed', {'error': str(error)})  # DLQ

await (queen.queue('tasks')
    .auto_ack(False)
    .consume(handler)
    .on_success(on_success)
    .on_error(on_error))
```

## Consumer Group Management

### Delete Consumer Group

```python
# Delete consumer group (including metadata)
await queen.delete_consumer_group('my-group')

# Delete but keep subscription metadata
await queen.delete_consumer_group('my-group', delete_metadata=False)
```

### Update Subscription Timestamp

```python
# Reset consumer group to start from specific time
await queen.update_consumer_group_timestamp(
    'my-group',
    '2025-11-10T10:00:00Z'
)
```

## Graceful Shutdown

### Async Context Manager (Recommended)

```python
async with Queen('http://localhost:6632') as queen:
    # Your code here
    pass
# Automatically flushes buffers and closes on exit
```

### Manual Shutdown

```python
queen = Queen('http://localhost:6632')

try:
    # Your code here
    pass
finally:
    await queen.close()
```

### With AbortController

Stop consumers gracefully:

```python
import asyncio

# Create abort signal
signal = asyncio.Event()

# Start consumer with signal
consumer_task = asyncio.create_task(
    queen.queue('tasks').consume(handler, signal=signal)
)

# Later... stop the consumer
signal.set()

# Wait for consumer to finish current message
await consumer_task

# Close Queen
await queen.close()
```

## Streaming (Windowed Processing)

Process messages in time-based windows with aggregation.

### Define a Stream

```python
# Define stream
await (queen.stream('user-events', 'analytics')
    .sources(['events', 'actions', 'clicks'])
    .partitioned()
    .tumbling_time(60)        # 60 second windows
    .grace_period(30)         # 30 second late arrival grace
    .lease_timeout(60)        # 60 second lease
    .define())
```

### Consume Windows

```python
consumer = queen.consumer('user-events', 'my-group')

async def process_window(window):
    # Get all messages
    print(f"Window has {window.size()} messages")
    
    # Filter messages
    window.filter(lambda msg: msg['data']['value'] > 100)
    
    # Group by key
    groups = window.group_by('data.userId')
    print(f"Found {len(groups)} users")
    
    # Aggregate
    stats = window.aggregate({
        'count': True,
        'sum': ['data.amount'],
        'avg': ['data.duration'],
        'min': ['data.price'],
        'max': ['data.price']
    })
    
    print('Stats:', stats)
    # {'count': 42, 'sum': {'data.amount': 1234.56}, ...}

await consumer.process(process_window)
```

### Window Methods

```python
# Filter
window.filter(lambda msg: msg['data']['status'] == 'active')

# Group by (supports dot notation)
groups = window.group_by('data.userId')
for user_id, messages in groups.items():
    print(f"User {user_id}: {len(messages)} messages")

# Aggregate
stats = window.aggregate({
    'count': True,
    'sum': ['data.amount', 'data.quantity'],
    'avg': ['data.price'],
    'min': ['data.timestamp'],
    'max': ['data.timestamp']
})

# Reset to original messages
window.reset()

# Get sizes
original = window.original_size()
current = window.size()
```

## Concurrency Patterns

### Parallel Workers

```python
async def worker(worker_id):
    async def handler(message):
        print(f'Worker {worker_id}:', message['data'])
    
    await queen.queue('tasks').group('workers').consume(handler)

# Launch 5 workers
workers = [worker(i) for i in range(5)]
await asyncio.gather(*workers)
```

### Built-In Concurrency

```python
# Launch 5 workers with a single call
async def handler(message):
    await process_task(message['data'])

await queen.queue('tasks').concurrency(5).consume(handler)
```

## Advanced Patterns

### Request-Reply Pattern

```python
import uuid

# Requester
async def make_request():
    correlation_id = str(uuid.uuid4())
    
    # Push request with correlation ID
    await queen.queue('requests').push([{
        'data': {'action': 'process', 'correlationId': correlation_id}
    }])
    
    # Wait for reply
    async def reply_handler(message):
        if message['data'].get('correlationId') == correlation_id:
            print('Got reply:', message['data'])
            return message['data']
    
    await queen.queue('replies').limit(1).consume(reply_handler)

# Worker
async def worker():
    async def handler(message):
        correlation_id = message['data']['correlationId']
        
        # Process
        result = await process(message['data'])
        
        # Send reply
        await queen.queue('replies').push([{
            'data': {'correlationId': correlation_id, 'result': result}
        }])
    
    await queen.queue('requests').consume(handler)
```

### Pipeline Pattern

```python
# Stage 1: Raw events → Processed events
async def stage1():
    async def handler(messages):
        results = [await process(m['data']) for m in messages]
        
        txn = queen.transaction()
        for msg in messages:
            txn.ack(msg)
        txn.queue('processed-events').push([{'data': r} for r in results])
        await txn.commit()
    
    await (queen.queue('raw-events')
        .group('processors')
        .batch(10)
        .auto_ack(False)
        .consume(handler))

# Stage 2: Processed events → Notifications
async def stage2():
    async def handler(message):
        await queen.queue('notifications').push([{
            'data': {'userId': message['data']['userId'], 'message': 'Done!'}
        }])
    
    await queen.queue('processed-events').group('notifiers').consume(handler)

# Run pipeline
await asyncio.gather(stage1(), stage2())
```

## Configuration Reference

### Client Configuration

```python
{
    'timeout_millis': 30000,               # 30 seconds
    'retry_attempts': 3,
    'retry_delay_millis': 1000,
    'load_balancing_strategy': 'affinity', # or 'round-robin', 'session'
    'affinity_hash_ring': 128,
    'enable_failover': True,
    'health_retry_after_millis': 5000
}
```

### Queue Configuration

```python
{
    'leaseTime': 300,                      # 5 minutes
    'retryLimit': 3,
    'priority': 0,
    'delayedProcessing': 0,
    'windowBuffer': 0,
    'maxSize': 0,                          # Unlimited
    'retentionSeconds': 0,                 # Keep forever
    'completedRetentionSeconds': 0,
    'encryptionEnabled': False
}
```

### Consume Configuration

```python
{
    'concurrency': 1,
    'batch': 1,
    'auto_ack': True,
    'wait': True,                          # Long polling
    'timeout_millis': 30000,
    'limit': None,                         # Run forever
    'idle_millis': None,
    'renew_lease': False
}
```

### Pop Configuration

```python
{
    'batch': 1,
    'wait': False,                         # No long polling
    'timeout_millis': 30000,
    'auto_ack': False                      # Manual ack required
}
```

## Logging

Enable detailed logging for debugging:

```bash
export QUEEN_CLIENT_LOG=true
python your_app.py
```

Example output:
```
[2025-10-28T10:30:45.123Z] [INFO] [Queen.constructor] {"status":"initialized","urls":1}
[2025-10-28T10:30:45.234Z] [INFO] [QueueBuilder.push] {"queue":"tasks","partition":"Default","count":5}
[2025-10-28T10:30:46.789Z] [INFO] [HttpClient.request] {"method":"POST","url":"http://localhost:6632/api/v1/push"}
```

## Type Hints

Full type hints included for IDE support:

```python
from queen import Queen, Message
from typing import Dict, Any

queen: Queen = Queen('http://localhost:6632')

async def handler(message: Message) -> None:
    data: Dict[str, Any] = message['data']
    print(data)

await queen.queue('orders').consume(handler)
```

## Error Handling

### Network Errors

The client automatically handles network errors with retry and failover:

```python
# Client retries 3 times with exponential backoff
# Then fails over to other servers if available
queen = Queen({
    'urls': ['http://server1:6632', 'http://server2:6632'],
    'retry_attempts': 3,
    'enable_failover': True
})
```

### Processing Errors

```python
async def handler(message):
    try:
        await risky_operation(message['data'])
    except TemporaryError as e:
        # Will retry automatically (auto_ack=True)
        raise
    except PermanentError as e:
        # Handle explicitly
        await queen.ack(message, 'failed', {'error': str(e)})

await queen.queue('tasks').consume(handler)
```

## Complete Example

```python
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        # Create queues
        await queen.queue('raw-events').config({'priority': 5}).create()
        await queen.queue('processed-events').config({'priority': 10}).create()
        
        # Stage 1: Ingest with buffering
        async def ingest():
            for i in range(10000):
                await (queen.queue('raw-events')
                    .partition(f"user-{i % 100}")
                    .buffer({'message_count': 500, 'time_millis': 1000})
                    .push([{
                        'data': {
                            'userId': i % 100,
                            'event': 'page_view',
                            'timestamp': i
                        }
                    }]))
            
            await queen.flush_all_buffers()
            print('Ingestion complete!')
        
        # Stage 2: Process with transactions
        async def process():
            async def handler(messages):
                processed = [{
                    'userId': m['data']['userId'],
                    'processed': True
                } for m in messages]
                
                txn = queen.transaction()
                for msg in messages:
                    txn.ack(msg)
                txn.queue('processed-events').push([{'data': p} for p in processed])
                await txn.commit()
            
            await (queen.queue('raw-events')
                .group('processors')
                .concurrency(5)
                .batch(10)
                .auto_ack(False)
                .consume(handler))
        
        # Stage 3: Monitor with tracing
        async def monitor():
            async def handler(message):
                await message['trace']({
                    'traceName': f"user-{message['data']['userId']}",
                    'data': {'text': 'Event processed'}
                })
            
            await (queen.queue('processed-events')
                .group('monitors')
                .subscription_mode('new')
                .consume(handler))
        
        # Run pipeline
        await ingest()
        await asyncio.gather(process(), monitor())

if __name__ == '__main__':
    asyncio.run(main())
```

## API Reference

### Queen Class

```python
# Initialize
queen = Queen(config)

# Queue operations
queen.queue(name: str) -> QueueBuilder

# Transactions
queen.transaction() -> TransactionBuilder

# Acknowledgment
await queen.ack(message, status, context)

# Lease renewal
await queen.renew(message_or_lease_id)

# Buffering
await queen.flush_all_buffers()
queen.get_buffer_stats()

# Consumer groups
await queen.delete_consumer_group(group, delete_metadata)
await queen.update_consumer_group_timestamp(group, timestamp)

# Streaming
queen.stream(name, namespace) -> StreamBuilder
queen.consumer(stream_name, consumer_group) -> StreamConsumer

# Shutdown
await queen.close()
```

### QueueBuilder Class

```python
# Configuration
.namespace(name: str)
.task(name: str)
.config(options: dict)
.create()
.delete()

# Push
.partition(name: str)
.buffer(options: dict)
.push(payload) -> PushBuilder

# Consume configuration
.group(name: str)
.concurrency(count: int)
.batch(size: int)
.limit(count: int)
.idle_millis(millis: int)
.auto_ack(enabled: bool)
.renew_lease(enabled: bool, interval_millis: int)
.subscription_mode(mode: str)
.subscription_from(from_: str)
.each()

# Consume
.consume(handler, signal) -> ConsumeBuilder

# Pop
.wait(enabled: bool)
.pop() -> List[Message]

# Buffer management
.flush_buffer()

# DLQ
.dlq(consumer_group: str) -> DLQBuilder
```

## Best Practices

1. ✅ **Use `async with`** - Ensures proper cleanup and buffer flushing
2. ✅ **Use `consume()` for workers** - Simpler API, handles retries automatically
3. ✅ **Use `pop()` for control** - When you need precise control over acking
4. ✅ **Buffer for speed** - Always use buffering when pushing many messages
5. ✅ **Partitions for order** - Use partitions when message order matters
6. ✅ **Consumer groups for scale** - Run multiple workers in the same group
7. ✅ **Transactions for consistency** - Use transactions for atomic operations
8. ✅ **Enable DLQ** - Always enable DLQ in production
9. ✅ **Renew long leases** - Use auto-renewal for long-running tasks
10. ✅ **Monitor DLQ** - Regularly check for failed messages
11. ✅ **Type hints** - Use type hints for better IDE support and fewer bugs

## Common Patterns

### High-Throughput Ingestion

```python
async def ingest(items):
    for item in items:
        await (queen.queue('events')
            .buffer({'message_count': 500, 'time_millis': 100})
            .push([{'data': item}]))
    
    await queen.flush_all_buffers()
```

### Scalable Processing

```python
async def process():
    await (queen.queue('tasks')
        .group('workers')
        .concurrency(10)
        .batch(20)
        .consume(handler))
```

### Ordered Processing

```python
async def process_user_events(user_id):
    async def handler(message):
        await process_in_order(message['data'])
    
    await queen.queue('events').partition(f"user-{user_id}").consume(handler)
```

### Fan-Out Pattern

```python
# Multiple groups process same messages
async def email_worker():
    await queen.queue('events').group('emailer').consume(send_email_handler)

async def analytics_worker():
    await queen.queue('events').group('analytics').consume(analytics_handler)

await asyncio.gather(email_worker(), analytics_worker())
```

## Migration from Node.js

The Python client API is nearly identical to the Node.js client:

### Syntax Differences

| Node.js | Python |
|---------|--------|
| `const queen = new Queen(...)` | `queen = Queen(...)` |
| `await queen.queue('q').create()` | `await queen.queue('q').create()` |
| `async (message) => { ... }` | `async def handler(message): ...` |
| `camelCase` | `snake_case` (for kwargs) |
| `.then()` | `await` |

### Key Differences

1. **Async context managers:** Use `async with Queen(...) as queen:`
2. **Parameter naming:** Use `snake_case` for keyword arguments
3. **Abort signals:** Use `asyncio.Event` instead of `AbortController`
4. **DLQ from:** Use `.from_()` instead of `.from()` (Python keyword)

### Example Migration

**Node.js:**
```javascript
const queen = new Queen('http://localhost:6632')

await queen.queue('tasks')
  .concurrency(5)
  .batch(10)
  .consume(async (message) => {
    await processTask(message.data)
  })
```

**Python:**
```python
async with Queen('http://localhost:6632') as queen:
    async def handler(message):
        await process_task(message['data'])
    
    await (queen.queue('tasks')
        .concurrency(5)
        .batch(10)
        .consume(handler))
```

## Troubleshooting

### Import Errors

```bash
# Make sure httpx is installed
pip install httpx

# Or reinstall the package
pip install -e .
```

### Type Checking

```bash
# Run mypy for type checking
pip install mypy
mypy your_app.py
```

### Connection Issues

```python
# Enable logging
import os
os.environ['QUEEN_CLIENT_LOG'] = 'true'

# Then run your app
```

### Performance Issues

```python
# Use buffering for high throughput
await queen.queue('q').buffer({'message_count': 500}).push([...])

# Use batching for consumption
await queen.queue('q').batch(50).concurrency(10).consume(handler)

# Monitor buffer stats
stats = queen.get_buffer_stats()
print(stats)
```

## Links

- [Python Client on GitHub](https://github.com/smartpricing/queen/tree/master/client-py)
- [PyPI Package](https://pypi.org/project/queen-mq/)
- [JavaScript Client](./javascript.md)
- [C++ Client](./cpp.md)
- [HTTP API Reference](../api/http.md)
- [Examples](./examples/basic.md)

