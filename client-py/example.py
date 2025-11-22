"""
Simple example demonstrating Queen MQ client usage
"""

import asyncio
from queen import Queen


async def main():
    """Main example function"""
    # Connect to Queen server
    async with Queen('http://localhost:6632') as queen:
        print("Connected to Queen MQ")

        # Create a queue
        print("\nCreating queue 'tasks'...")
        await queen.queue('tasks').config({
            'leaseTime': 300,
            'retryLimit': 3,
            'priority': 5
        }).create()
        print("Queue created!")

        # Push messages with buffering for better performance
        print("\nPushing messages...")
        for i in range(10):
            await queen.queue('tasks').push([{
                'data': {'task': f'send-email-{i}', 'to': f'user{i}@example.com'}
            }])
        print("Messages pushed!")

        # Pop and process messages manually
        print("\nPopping messages...")
        messages = await queen.queue('tasks').batch(5).wait(True).pop()
        print(f"Popped {len(messages)} messages")

        for message in messages:
            print(f"Processing: {message['data']}")
            # Acknowledge message
            await queen.ack(message, True)

        # Consume remaining messages (auto-ack)
        # When batch=1 (default), handler receives single message
        print("\nConsuming remaining messages (one at a time)...")
        message_count = 0

        async def handler(message):
            nonlocal message_count
            print(f"Consumed: {message['data']}")
            message_count += 1

        await queen.queue('tasks').limit(5).consume(handler)
        print(f"Consumed {message_count} messages")
        
        # Push a few more for batch demo
        print("\nPushing 3 more messages for batch demo...")
        for i in range(10, 13):
            await queen.queue('tasks').push([{
                'data': {'task': f'batch-task-{i}', 'to': f'user{i}@example.com'}
            }])
        
        # Consume in batches (handler receives array of messages)
        print("\nConsuming in batches of 3...")
        batch_count = 0
        
        async def batch_handler(messages):
            nonlocal batch_count
            print(f"Batch received {len(messages)} messages:")
            for msg in messages:
                print(f"  - {msg['data']}")
            batch_count += len(messages)
        
        await queen.queue('tasks').batch(3).limit(3).consume(batch_handler)
        print(f"Consumed {batch_count} messages in batch")

        print("\n✅ Example completed successfully!")


if __name__ == '__main__':
    asyncio.run(main())

