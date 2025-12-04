# Python Client Examples

Comprehensive examples for the Queen MQ Python client.

## Basic Push and Pop

```python
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        # Create queue
        await queen.queue('tasks').create()
        
        # Push messages
        await queen.queue('tasks').push([
            {'data': {'task': 'send-email', 'to': 'user@example.com'}},
            {'data': {'task': 'generate-report', 'id': 123}}
        ])
        
        # Pop messages
        messages = await queen.queue('tasks').pop()
        
        for message in messages:
            print('Processing:', message['data'])
            await queen.ack(message, True)

asyncio.run(main())
```

## Simple Consumer

```python
async def handler(message):
    print('Got task:', message['data'])
    await process_task(message['data'])
    # Automatically ACK'd on success

await queen.queue('tasks').consume(handler)
```

## Consumer with Concurrency

```python
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        # Process 10 messages in parallel
        async def handler(message):
            print(f'Processing: {message["data"]}')
            await asyncio.sleep(1)  # Simulate work
            print(f'Done: {message["data"]}')
        
        await queen.queue('tasks').concurrency(10).consume(handler)

asyncio.run(main())
```

## Batch Processing

```python
async def batch_handler(messages):
    """Handler receives array when batch > 1"""
    print(f'Processing batch of {len(messages)} messages')
    
    for message in messages:
        await process(message['data'])

# Fetch and process 20 messages at a time
await queen.queue('tasks').batch(20).consume(batch_handler)
```

## Consumer Groups

```python
import asyncio
from queen import Queen

async def worker1():
    """Worker 1 in consumer group"""
    async with Queen('http://localhost:6632') as queen:
        async def handler(message):
            print('Worker 1:', message['data'])
        
        await queen.queue('emails').group('processors').consume(handler)

async def worker2():
    """Worker 2 in same consumer group"""
    async with Queen('http://localhost:6632') as queen:
        async def handler(message):
            print('Worker 2:', message['data'])
        
        await queen.queue('emails').group('processors').consume(handler)

# Run both workers - they share the load
asyncio.run(asyncio.gather(worker1(), worker2()))
```

## Partitions for Ordering

```python
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        # Create queue
        await queen.queue('user-events').create()
        
        # Push events for user-123 (will be processed in order)
        await queen.queue('user-events').partition('user-123').push([
            {'data': {'event': 'login', 'timestamp': '10:00:00'}},
            {'data': {'event': 'view-page', 'timestamp': '10:00:05'}},
            {'data': {'event': 'logout', 'timestamp': '10:00:30'}}
        ])
        
        # Consume events for this user in order
        async def handler(message):
            print(f"User 123: {message['data']['event']} at {message['data']['timestamp']}")
        
        await queen.queue('user-events').partition('user-123').limit(3).consume(handler)

asyncio.run(main())
```

## Client-Side Buffering

```python
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        # Create queue
        await queen.queue('events').create()
        
        # Push 10,000 messages with buffering (super fast!)
        for i in range(10000):
            await queen.queue('events').buffer({
                'message_count': 500,
                'time_millis': 1000
            }).push([
                {'data': {'id': i, 'value': i * 2}}
            ])
        
        # Flush remaining buffered messages
        await queen.flush_all_buffers()
        
        print('✅ Pushed 10,000 messages with buffering!')

asyncio.run(main())
```

## Transactions

```python
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        # Create queues
        await queen.queue('input').create()
        await queen.queue('output').create()
        
        # Push to input
        await queen.queue('input').push([
            {'data': {'value': 42}}
        ])
        
        # Pop from input
        messages = await queen.queue('input').pop()
        
        if messages:
            message = messages[0]
            
            # Process
            processed = {'value': message['data']['value'] * 2}
            
            # Atomically: ack input AND push output
            await (queen.transaction()
                .ack(message)
                .queue('output')
                .push([{'data': processed}])
                .commit())
            
            print('✅ Transaction committed!')

asyncio.run(main())
```

## Subscription Modes

```python
import asyncio
from queen import Queen

async def batch_analytics():
    """Process ALL messages including historical"""
    async with Queen('http://localhost:6632') as queen:
        async def handler(message):
            await generate_report(message['data'])
        
        await queen.queue('events').group('batch-analytics').consume(handler)

async def realtime_monitoring():
    """Process only NEW messages"""
    async with Queen('http://localhost:6632') as queen:
        async def handler(message):
            await send_alert(message['data'])
        
        await (queen.queue('events')
            .group('realtime-monitor')
            .subscription_mode('new')
            .consume(handler))

async def replay_debug():
    """Process from specific timestamp"""
    async with Queen('http://localhost:6632') as queen:
        async def handler(message):
            await debug_message(message['data'])
        
        await (queen.queue('events')
            .group('debug-replay')
            .subscription_from('2025-10-28T15:30:00.000Z')
            .consume(handler))

# Run all three groups
asyncio.run(asyncio.gather(
    batch_analytics(),
    realtime_monitoring(),
    replay_debug()
))
```

## Dead Letter Queue

```python
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        # Create queue with DLQ enabled
        await queen.queue('risky').config({
            'retryLimit': 3,
            'dlqAfterMaxRetries': True
        }).create()
        
        # Push some messages
        for i in range(10):
            await queen.queue('risky').push([
                {'data': {'value': i}}
            ])
        
        # Consume (some will fail)
        processed = 0
        async def handler(message):
            nonlocal processed
            if message['data']['value'] % 3 == 0:
                raise Exception('Simulated failure')
            processed += 1
        
        await queen.queue('risky').limit(10).consume(handler)
        
        # Query DLQ
        dlq = await queen.queue('risky').dlq().limit(10).get()
        
        print(f'Processed: {processed}')
        print(f'Failed (in DLQ): {dlq["total"]}')
        
        for msg in dlq['messages']:
            print(f'  - Value: {msg["data"]["value"]}, Error: {msg.get("errorMessage")}')

asyncio.run(main())
```

## Message Tracing

```python
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        await queen.queue('orders').create()
        
        # Push an order
        await queen.queue('orders').push([
            {'data': {'orderId': 12345, 'amount': 99.99}}
        ])
        
        # Process with tracing
        async def handler(msg):
            order_id = msg['data']['orderId']
            
            # Record trace
            await msg['trace']({
                'traceName': f"order-{order_id}",
                'eventType': 'info',
                'data': {'text': 'Order processing started'}
            })
            
            # Do work
            await process_order(msg['data'])
            
            # Record completion
            await msg['trace']({
                'traceName': f"order-{order_id}",
                'eventType': 'processing',
                'data': {
                    'text': 'Order completed',
                    'amount': msg['data']['amount']
                }
            })
        
        await queen.queue('orders').limit(1).consume(handler)
        print('✅ Order processed with tracing!')

async def process_order(data):
    await asyncio.sleep(0.5)

asyncio.run(main())
```

## Lease Renewal

```python
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        await queen.queue('long-tasks').create()
        
        # Push a task
        await queen.queue('long-tasks').push([
            {'data': {'task': 'process-video', 'videoId': 123}}
        ])
        
        # Consume with auto-renewal (every 60 seconds)
        async def handler(message):
            print('Starting long task...')
            
            # This can take minutes - lease keeps renewing!
            await asyncio.sleep(180)  # 3 minutes
            
            print('Long task completed!')
        
        await (queen.queue('long-tasks')
            .renew_lease(True, 60000)  # Renew every 60 seconds
            .limit(1)
            .consume(handler))

asyncio.run(main())
```

## Error Handling with Callbacks

```python
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        await queen.queue('tasks').create()
        
        # Push messages
        for i in range(5):
            await queen.queue('tasks').push([
                {'data': {'id': i, 'risky': i % 2 == 0}}
            ])
        
        # Consume with error handling
        async def handler(message):
            if message['data']['risky']:
                raise Exception('Risky operation failed!')
            return message['data']
        
        async def on_success(message, result):
            print('✅ Success:', result)
        
        async def on_error(message, error):
            print('❌ Error:', error)
            # Custom retry logic
            if 'temporary' in str(error):
                await queen.ack(message, False)  # Retry
            else:
                await queen.ack(message, 'failed', {'error': str(error)})
        
        await (queen.queue('tasks')
            .auto_ack(False)
            .limit(5)
            .consume(handler)
            .on_success(on_success)
            .on_error(on_error))

asyncio.run(main())
```

## Multiple Queues with Namespace

```python
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        # Create multiple queues in 'accounting' namespace
        await queen.queue('invoices').namespace('accounting').create()
        await queen.queue('receipts').namespace('accounting').create()
        await queen.queue('payments').namespace('accounting').create()
        
        # Push to different queues
        await queen.queue('invoices').push([{'data': {'invoice': 'INV-001'}}])
        await queen.queue('receipts').push([{'data': {'receipt': 'RCP-001'}}])
        await queen.queue('payments').push([{'data': {'payment': 'PAY-001'}}])
        
        # Consume from ALL queues in namespace
        async def handler(message):
            print(f'Accounting message from {message.get("queue")}: {message["data"]}')
        
        await queen.queue().namespace('accounting').limit(3).consume(handler)
        
        print('✅ Processed messages from all accounting queues!')

asyncio.run(main())
```

## Streaming with Windows

```python
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        # Create source queue
        await queen.queue('sensor-data').create()
        
        # Define stream
        await (queen.stream('sensor-analytics', 'iot')
            .sources(['sensor-data'])
            .tumbling_time(10)     # 10 second windows
            .grace_period(5)       # 5 second late arrival grace
            .define())
        
        # Push sensor data
        for i in range(100):
            await queen.queue('sensor-data').push([
                {'data': {'sensorId': i % 10, 'temperature': 20 + i % 10, 'humidity': 50 + i % 20}}
            ])
            await asyncio.sleep(0.1)
        
        # Consume windows
        consumer = queen.consumer('sensor-analytics', 'analytics-group')
        
        async def process_window(window):
            print(f'Window: {window.size()} messages')
            
            # Aggregate
            stats = window.aggregate({
                'count': True,
                'avg': ['data.temperature', 'data.humidity'],
                'min': ['data.temperature'],
                'max': ['data.temperature']
            })
            
            print('Temperature:', stats)
            
            # Group by sensor
            groups = window.group_by('data.sensorId')
            print(f'Sensors: {len(groups)}')
        
        # Process 3 windows then stop
        windows_processed = 0
        async def wrapped_process(window):
            nonlocal windows_processed
            await process_window(window)
            windows_processed += 1
            if windows_processed >= 3:
                consumer.stop()
        
        await consumer.process(wrapped_process)

asyncio.run(main())
```

## High-Throughput Pipeline

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
            print('✅ Ingestion complete!')
        
        # Stage 2: Process with transactions
        async def process():
            async def handler(messages):
                processed = [{
                    'userId': m['data']['userId'],
                    'processed': True,
                    'timestamp': m['data']['timestamp']
                } for m in messages]
                
                # Atomic: ack inputs and push outputs
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
        
        # Stage 3: Monitor (only new messages)
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
        
        # Process and monitor concurrently
        await asyncio.gather(
            asyncio.create_task(process()),
            asyncio.create_task(monitor())
        )

asyncio.run(main())
```

## Request-Reply Pattern

```python
import asyncio
import uuid
from queen import Queen

async def requester():
    """Make a request and wait for reply"""
    async with Queen('http://localhost:6632') as queen:
        correlation_id = str(uuid.uuid4())
        
        # Send request
        await queen.queue('requests').push([{
            'data': {
                'action': 'calculate',
                'value': 42,
                'correlationId': correlation_id
            }
        }])
        
        print(f'📤 Request sent: {correlation_id}')
        
        # Wait for reply
        reply_received = False
        async def reply_handler(message):
            nonlocal reply_received
            if message['data'].get('correlationId') == correlation_id:
                print(f'📥 Reply received: {message["data"]["result"]}')
                reply_received = True
        
        await queen.queue('replies').consume(reply_handler)

async def worker():
    """Process requests and send replies"""
    async with Queen('http://localhost:6632') as queen:
        async def handler(message):
            correlation_id = message['data']['correlationId']
            value = message['data']['value']
            
            # Process
            result = value * 2
            
            # Send reply
            await queen.queue('replies').push([{
                'data': {
                    'correlationId': correlation_id,
                    'result': result
                }
            }])
            
            print(f'✅ Processed request {correlation_id}: {value} -> {result}')
        
        await queen.queue('requests').consume(handler)

# Run worker in background, then make request
asyncio.run(asyncio.gather(
    worker(),
    requester()
))
```

## Manual Ack with Pop

```python
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        # Create and populate queue
        await queen.queue('manual-tasks').create()
        
        for i in range(5):
            await queen.queue('manual-tasks').push([
                {'data': {'id': i, 'risky': i % 2 == 0}}
            ])
        
        # Pop with long polling
        while True:
            messages = await queen.queue('manual-tasks').batch(5).wait(True).pop()
            
            if not messages:
                break
            
            for message in messages:
                try:
                    # Process
                    if message['data']['risky']:
                        raise Exception('Risky operation failed')
                    
                    print(f'✅ Processed: {message["data"]["id"]}')
                    await queen.ack(message, True)
                    
                except Exception as error:
                    print(f'❌ Failed: {message["data"]["id"]} - {error}')
                    await queen.ack(message, False, {'error': str(error)})

asyncio.run(main())
```

## Graceful Shutdown

```python
import asyncio
import signal
from queen import Queen

# Using context manager (automatic cleanup)
async def main():
    async with Queen('http://localhost:6632') as queen:
        async def handler(message):
            await process(message['data'])
        
        await queen.queue('tasks').consume(handler)
    # Queen automatically closes and flushes buffers on exit

# Using abort signal
async def main_with_signal():
    queen = Queen('http://localhost:6632')
    
    # Create abort signal
    stop_signal = asyncio.Event()
    
    # Setup signal handler
    def signal_handler(signum, frame):
        print('\nReceived interrupt, stopping...')
        stop_signal.set()
    
    signal.signal(signal.SIGINT, signal_handler)
    
    try:
        # Start consumer with signal
        async def handler(message):
            await process(message['data'])
        
        await queen.queue('tasks').consume(handler, signal=stop_signal)
    finally:
        await queen.close()

asyncio.run(main())
```

## Type Hints

```python
import asyncio
from queen import Queen, Message
from typing import Dict, Any

async def main():
    queen: Queen = Queen('http://localhost:6632')
    
    async def handler(message: Message) -> None:
        data: Dict[str, Any] = message['data']
        order_id: int = data['orderId']
        amount: float = data['amount']
        
        print(f'Processing order {order_id}: ${amount}')
    
    await queen.queue('orders').consume(handler)

asyncio.run(main())
```

## Production Example

```python
import asyncio
import logging
from queen import Queen

# Setup logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

async def main():
    # Production configuration
    queen = Queen({
        'urls': [
            'http://queen1.prod.internal:6632',
            'http://queen2.prod.internal:6632',
            'http://queen3.prod.internal:6632'
        ],
        'load_balancing_strategy': 'affinity',
        'affinity_hash_ring': 128,
        'enable_failover': True,
        'timeout_millis': 30000,
        'retry_attempts': 3
    })
    
    try:
        # Setup queues
        await queen.queue('orders').config({
            'leaseTime': 300,
            'retryLimit': 3,
            'priority': 10,
            'dlqAfterMaxRetries': True,
            'encryptionEnabled': True
        }).create()
        
        # Process orders
        async def handler(message):
            order_id = message['data']['orderId']
            logger.info(f'Processing order {order_id}')
            
            try:
                # Trace start
                await message['trace']({
                    'traceName': f"order-{order_id}",
                    'eventType': 'info',
                    'data': {'text': 'Started processing'}
                })
                
                # Process
                result = await process_order(message['data'])
                
                # Trace completion
                await message['trace']({
                    'traceName': f"order-{order_id}",
                    'eventType': 'processing',
                    'data': {'text': 'Completed', 'result': result}
                })
                
                logger.info(f'Completed order {order_id}')
                
            except Exception as e:
                logger.error(f'Failed order {order_id}: {e}')
                await message['trace']({
                    'traceName': f"order-{order_id}",
                    'eventType': 'error',
                    'data': {'text': 'Failed', 'error': str(e)}
                })
                raise
        
        # Run consumer with monitoring
        await (queen.queue('orders')
            .group('order-processors')
            .concurrency(10)
            .batch(20)
            .renew_lease(True, 60000)
            .consume(handler))
            
    finally:
        await queen.close()

async def process_order(data):
    # Your business logic here
    await asyncio.sleep(1)
    return {'status': 'completed'}

if __name__ == '__main__':
    asyncio.run(main())
```

## More Examples

For more examples and advanced patterns:
- [Batch Operations](./batch.md)
- [Consumer Groups](./consumer-groups.md)
- [Transactions](./transactions.md)
- [Python Client GitHub](https://github.com/smartpricing/queen/tree/master/client-py)

