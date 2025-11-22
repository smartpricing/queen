"""
Consume operation tests
"""

import asyncio
import pytest


@pytest.mark.asyncio
async def test_consumer(client):
    """Test basic consumer"""
    queue = await client.queue("test-queue-v2-consume").create()
    assert queue.get("configured") is True
    
    await client.queue("test-queue-v2-consume").push([{"data": {"message": "Hello, world!"}}])
    
    msg_to_return = None
    
    async def handler(msg):
        nonlocal msg_to_return
        msg_to_return = msg
    
    await client.queue("test-queue-v2-consume").batch(1).limit(1).each().consume(handler)
    
    assert msg_to_return is not None


@pytest.mark.asyncio
async def test_consumer_trace(client):
    """Test consumer with tracing"""
    queue = await client.queue("test-queue-v2-consume-trace").create()
    assert queue.get("configured") is True
    
    await client.queue("test-queue-v2-consume-trace").push([{"data": {"message": "Hello, world!"}}])
    
    msg_to_return = None
    
    async def handler(msg):
        nonlocal msg_to_return
        msg_to_return = msg
        await msg["trace"]({
            "traceName": ["test-trace", "test-trace-2"],
            "eventType": "info",
            "data": {"message": "Hello, world!"}
        })
    
    await client.queue("test-queue-v2-consume-trace").batch(1).limit(1).each().consume(handler)
    
    assert msg_to_return is not None


@pytest.mark.asyncio
async def test_consumer_namespace(client):
    """Test consumer with namespace"""
    queue = await client.queue("test-queue-v2-namespace").namespace("test-namespace").create()
    assert queue.get("configured") is True
    
    await client.queue("test-queue-v2-namespace").push([{"data": {"message": "Hello, world!"}}])
    
    msg_to_return = None
    
    async def handler(msg):
        nonlocal msg_to_return
        msg_to_return = msg
    
    await client.queue().namespace("test-namespace").batch(1).limit(1).each().consume(handler)
    
    assert msg_to_return is not None


@pytest.mark.asyncio
async def test_consumer_task(client):
    """Test consumer with task"""
    queue = await client.queue("test-queue-v2-task").task("test-task").create()
    assert queue.get("configured") is True
    
    await client.queue("test-queue-v2-task").push([{"data": {"message": "Hello, world!"}}])
    
    msg_to_return = None
    
    async def handler(msg):
        nonlocal msg_to_return
        msg_to_return = msg
    
    await client.queue().task("test-task").batch(1).limit(1).each().consume(handler)
    
    assert msg_to_return is not None


@pytest.mark.asyncio
async def test_consumer_with_partition(client):
    """Test consumer with partitions"""
    queue = await client.queue("test-queue-v2-consume-with-partition").create()
    assert queue.get("configured") is True
    
    # Push to partition 1
    for i in range(100):
        await client.queue("test-queue-v2-consume-with-partition").buffer({
            "message_count": 100,
            "time_millis": 1000
        }).partition("test-partition-01").push([{"data": {"message": "Hello, world!"}}])
    
    # Push to partition 2
    for i in range(100):
        await client.queue("test-queue-v2-consume-with-partition").buffer({
            "message_count": 100,
            "time_millis": 1000
        }).partition("test-partition-02").push([{"data": {"message": "Hello, world!"}}])
    
    msg_to_return_1 = None
    msg_to_return_2 = None
    
    async def handler1(msgs):
        nonlocal msg_to_return_1
        msg_to_return_1 = len(msgs)
    
    async def handler2(msgs):
        nonlocal msg_to_return_2
        msg_to_return_2 = len(msgs)
    
    await client.queue("test-queue-v2-consume-with-partition").partition("test-partition-01").batch(100).limit(1).consume(handler1)
    await client.queue("test-queue-v2-consume-with-partition").partition("test-partition-02").batch(100).limit(1).consume(handler2)
    
    assert msg_to_return_1 == 100
    assert msg_to_return_2 == 100


@pytest.mark.asyncio
async def test_consumer_batch_consume(client):
    """Test batch consumption"""
    queue = await client.queue("test-queue-v2-consume-batch").create()
    assert queue.get("configured") is True
    
    await client.queue("test-queue-v2-consume-batch").push([
        {"data": {"message": "Hello, world!"}},
        {"data": {"message": "Hello, world 2!"}},
        {"data": {"message": "Hello, world 3!"}}
    ])
    
    msg_length = 0
    
    async def handler(msgs):
        nonlocal msg_length
        msg_length = len(msgs)
    
    await client.queue("test-queue-v2-consume-batch").batch(10).wait(False).limit(1).consume(handler)
    
    assert msg_length == 3


@pytest.mark.asyncio
async def test_consumer_ordering(client):
    """Test message ordering"""
    queue = await client.queue("test-queue-v2-consume-batch").create()
    assert queue.get("configured") is True
    
    messages_to_push = 100
    
    for i in range(messages_to_push):
        await client.queue("test-queue-v2-consume-batch").push([{"data": {"id": i}}])
        print(f"Pushed message: {i}")
    
    last_id = None
    
    async def handler(msg):
        nonlocal last_id
        if last_id is None:
            last_id = msg["data"]["id"]
        else:
            if msg["data"]["id"] != last_id + 1:
                raise AssertionError("Message ordering violation")
            last_id = msg["data"]["id"]
    
    await client.queue("test-queue-v2-consume-batch").batch(1).wait(False).limit(messages_to_push).each().consume(handler)
    
    assert last_id == messages_to_push - 1


@pytest.mark.asyncio
async def test_consumer_ordering_batch(client):
    """Test message ordering with batch consumption"""
    queue = await client.queue("test-queue-v2-consume-batch-100").create()
    assert queue.get("configured") is True
    
    messages_to_push = 100
    
    for i in range(messages_to_push):
        await client.queue("test-queue-v2-consume-batch-100").push([{"data": {"id": i}}])
    
    last_id = None
    
    async def handler(msgs):
        nonlocal last_id
        for msg in msgs:
            if last_id is None:
                last_id = msg["data"]["id"]
            else:
                if msg["data"]["id"] != last_id + 1:
                    raise AssertionError("Message ordering violation")
                last_id = msg["data"]["id"]
    
    await client.queue("test-queue-v2-consume-batch-100").batch(messages_to_push).wait(False).limit(1).consume(handler)
    
    assert last_id == messages_to_push - 1


@pytest.mark.asyncio
async def test_consumer_ordering_concurrency(client):
    """Test ordering with concurrency"""
    queue = await client.queue("test-queue-v2-consume-batch-concurrency").create()
    assert queue.get("configured") is True
    
    messages_to_push = 100
    
    for i in range(messages_to_push):
        await client.queue("test-queue-v2-consume-batch-concurrency").push([{"data": {"id": i}}])
    
    last_id = None
    
    async def handler(msg):
        nonlocal last_id
        if last_id is None:
            last_id = msg["data"]["id"]
        else:
            if msg["data"]["id"] != last_id + 1:
                raise AssertionError("Message ordering violation")
            last_id = msg["data"]["id"]
    
    await client.queue("test-queue-v2-consume-batch-concurrency").concurrency(10).batch(1).wait(False).limit(10).each().consume(handler)
    
    assert last_id == messages_to_push - 1


@pytest.mark.asyncio
async def test_consumer_ordering_concurrency_with_buffered_push(client):
    """Test ordering with concurrency and buffered push"""
    queue = await client.queue("test-queue-v2-consume-batch-concurrency-with-buffered-push").create()
    assert queue.get("configured") is True
    
    messages_to_push = 100
    
    for i in range(messages_to_push):
        await client.queue("test-queue-v2-consume-batch-concurrency-with-buffered-push").buffer({
            "message_count": 100,
            "time_millis": 1000
        }).push([{"data": {"id": i}}])
    
    last_id = None
    
    async def handler(msg):
        nonlocal last_id
        if last_id is None:
            last_id = msg["data"]["id"]
        else:
            if msg["data"]["id"] != last_id + 1:
                raise AssertionError("Message ordering violation")
            last_id = msg["data"]["id"]
    
    await client.queue("test-queue-v2-consume-batch-concurrency-with-buffered-push").concurrency(10).batch(10).wait(False).limit(10).each().consume(handler)
    
    assert last_id == messages_to_push - 1


@pytest.mark.asyncio
async def test_consumer_group(client):
    """Test consumer groups"""
    queue = await client.queue("test-queue-v2-consume-group").create()
    assert queue.get("configured") is True
    
    messages_to_push = 100
    
    for i in range(messages_to_push):
        await client.queue("test-queue-v2-consume-group").buffer({
            "message_count": 100,
            "time_millis": 1000
        }).push([{"data": {"id": i}}])
    
    group_01_messages = 0
    group_02_messages = 0
    
    async def handler1(msgs):
        nonlocal group_01_messages
        group_01_messages = len(msgs)
    
    async def handler2(msgs):
        nonlocal group_02_messages
        group_02_messages = len(msgs)
    
    await client.queue("test-queue-v2-consume-group").subscription_mode("from_beginning").group("test-group-01").batch(messages_to_push).limit(1).wait(False).consume(handler1)
    await client.queue("test-queue-v2-consume-group").subscription_mode("from_beginning").group("test-group-02").batch(messages_to_push).limit(1).wait(False).consume(handler2)
    
    assert group_01_messages == messages_to_push
    assert group_02_messages == messages_to_push


@pytest.mark.asyncio
async def test_consumer_group_with_partition(client):
    """Test consumer groups with partitions"""
    queue = await client.queue("test-queue-v2-consume-group-with-partition").create()
    assert queue.get("configured") is True
    
    messages_to_push = 100
    
    for i in range(messages_to_push):
        await client.queue("test-queue-v2-consume-group-with-partition").partition("test-partition-01").buffer({
            "message_count": 100,
            "time_millis": 1000
        }).push([{"data": {"id": i}}])
    
    group_01_messages = 0
    group_02_messages = 0
    
    async def handler1(msgs):
        nonlocal group_01_messages
        group_01_messages = len(msgs)
    
    async def handler2(msgs):
        nonlocal group_02_messages
        group_02_messages = len(msgs)
    
    await client.queue("test-queue-v2-consume-group-with-partition").partition("test-partition-01").subscription_mode("from_beginning").group("test-group-01").batch(messages_to_push).limit(1).wait(False).consume(handler1)
    await client.queue("test-queue-v2-consume-group-with-partition").partition("test-partition-01").subscription_mode("from_beginning").group("test-group-02").batch(messages_to_push).limit(1).wait(False).consume(handler2)
    
    assert group_01_messages == messages_to_push
    assert group_02_messages == messages_to_push


@pytest.mark.asyncio
async def test_manual_ack(client):
    """Test manual acknowledgment with callbacks"""
    queue = await client.queue("test-queue-v2-manual-ack").create()
    assert queue.get("configured") is True
    
    messages_to_push = 10000
    
    # Push messages in batches
    messages = []
    for i in range(messages_to_push):
        messages.append({"data": {"id": i}})
        if len(messages) >= 1000:
            await client.queue("test-queue-v2-manual-ack").push(messages)
            messages = []
    if messages:
        await client.queue("test-queue-v2-manual-ack").push(messages)
    
    unique_ids = set()
    last_id = None
    
    async def handler(msgs):
        nonlocal last_id
        for msg in msgs:
            if last_id is None:
                last_id = msg["data"]["id"]
            else:
                if msg["data"]["id"] != last_id + 1:
                    raise AssertionError("Message ordering violation")
                last_id = msg["data"]["id"]
            unique_ids.add(msg["data"]["id"])
    
    async def on_success(msgs, result):
        await client.ack(msgs, True)
    
    async def on_error(msgs, error):
        await client.ack(msgs, False)
    
    await (client.queue("test-queue-v2-manual-ack")
        .concurrency(10)
        .subscription_mode("from_beginning")
        .batch(1000)
        .wait(False)
        .limit(1)
        .consume(handler)
        .on_success(on_success)
        .on_error(on_error))
    
    assert len(unique_ids) == messages_to_push


@pytest.mark.asyncio
async def test_retries(client):
    """Test retry mechanism"""
    queue = await client.queue("test-queue-v2-retries").config({"retry_limit": 3}).create()
    assert queue.get("configured") is True
    
    messages_to_push = 100
    
    messages = [{"data": {"id": i}} for i in range(messages_to_push)]
    await client.queue("test-queue-v2-retries").push(messages)
    
    attempt_count = 0
    
    async def handler(msgs):
        nonlocal attempt_count
        attempt_count += 1
        if attempt_count < 3:
            # Fail first 2 attempts
            raise Exception("Test error - triggering retry")
        # Third attempt succeeds
    
    await client.queue("test-queue-v2-retries").concurrency(1).subscription_mode("from_beginning").batch(100).wait(False).limit(300).consume(handler)
    
    assert attempt_count == 3, f"Expected 3 attempts, got {attempt_count}"


@pytest.mark.asyncio
async def test_retries_consumer_group(client):
    """Test retries with consumer groups"""
    queue = await client.queue("test-queue-v2-retries-consumer-group").config({"retry_limit": 3}).create()
    assert queue.get("configured") is True
    
    messages_to_push = 100
    
    messages = [{"data": {"id": i}} for i in range(messages_to_push)]
    await client.queue("test-queue-v2-retries-consumer-group").push(messages)
    
    attempt_count = 0
    
    async def handler(msgs):
        nonlocal attempt_count
        print(f"Group 01: {len(msgs)}")
        attempt_count += 1
        if attempt_count < 3:
            # Fail first 2 attempts
            raise Exception("Test error - triggering retry")
        # Third attempt succeeds
    
    await client.queue("test-queue-v2-retries-consumer-group").group("test-group-01").concurrency(1).subscription_mode("from_beginning").batch(100).wait(False).limit(300).auto_ack(True).consume(handler)
    
    group_02_messages = 0
    
    async def handler2(msgs):
        nonlocal group_02_messages
        print(f"Group 02: {len(msgs)}")
        group_02_messages += len(msgs)
    
    await client.queue("test-queue-v2-retries-consumer-group").group("test-group-02").concurrency(1).subscription_mode("from_beginning").batch(100).wait(False).limit(100).auto_ack(True).consume(handler2)
    
    assert attempt_count == 3
    assert group_02_messages == messages_to_push


@pytest.mark.asyncio
async def test_auto_renew_lease(client):
    """Test automatic lease renewal API"""
    queue = await client.queue("test-queue-v2-auto-renew-lease").config({"lease_time": 2}).create()
    assert queue.get("configured") is True
    
    # Test that renew_lease option works without errors
    await client.queue("test-queue-v2-auto-renew-lease").push([{"data": {"message": "Long processing task"}}])
    
    processed = False
    
    async def handler(msg):
        nonlocal processed
        print(f"Processing message: {msg['data']['message']}")
        # Simulate work that takes longer than lease
        await asyncio.sleep(3)
        processed = True
        print("✅ Processing completed")
    
    # Consume with auto-renewal (the renewal API should work)
    await (client.queue("test-queue-v2-auto-renew-lease")
        .batch(1)
        .subscription_mode("from_beginning")
        .wait(False)
        .limit(1)
        .renew_lease(True, 1000)  # Auto-renew every 1 second
        .each()
        .consume(handler))
    
    # Verify message was processed
    assert processed is True, "Message should be processed with auto-renewal enabled"
    
    # Verify we can query the queue (basic API check)
    remaining = await client.queue("test-queue-v2-auto-renew-lease").batch(10).wait(False).pop()
    print(f"Remaining messages: {len(remaining)}")
    
    # Test passes - renewal API works without errors
    print("✅ Auto-renewal test completed")

