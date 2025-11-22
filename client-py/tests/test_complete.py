"""
Complete workflow tests (end-to-end)
"""

import pytest


@pytest.mark.asyncio
async def test_complete(client):
    """Test complete workflow with transactions through multiple queues"""
    queue_init = await client.queue("test-queue-v2-complete-init").create()
    queue_next = await client.queue("test-queue-v2-complete-next").create()
    queue_final = await client.queue("test-queue-v2-complete-final").create()
    
    assert all([
        queue_init.get("configured"),
        queue_next.get("configured"),
        queue_final.get("configured")
    ])
    
    # Push initial message
    await client.queue("test-queue-v2-complete-init").push([
        {"data": {"message": "First", "count": 0}}
    ])
    
    # Stage 1: Process init -> next
    async def handler1(msg):
        await (client.transaction()
            .queue("test-queue-v2-complete-next")
            .push([{"data": {"message": "Next", "count": msg["data"]["count"] + 1}}])
            .ack(msg)
            .commit())
    
    await (client.queue("test-queue-v2-complete-init")
        .batch(1)
        .wait(False)
        .limit(1)
        .each()
        .auto_ack(False)
        .consume(handler1))
    
    # Stage 2: Process next -> final
    async def handler2(msg):
        await (client.transaction()
            .queue("test-queue-v2-complete-final")
            .push([{"data": {"message": "Final", "count": msg["data"]["count"] + 1}}])
            .ack(msg)
            .commit())
    
    await (client.queue("test-queue-v2-complete-next")
        .batch(1)
        .wait(False)
        .limit(1)
        .each()
        .auto_ack(False)
        .consume(handler2))
    
    # Stage 3: Consume final message
    final_message = None
    
    async def handler3(msg):
        nonlocal final_message
        final_message = msg
    
    await (client.queue("test-queue-v2-complete-final")
        .batch(1)
        .wait(False)
        .limit(1)
        .each()
        .consume(handler3))
    
    assert final_message is not None
    assert final_message["data"]["count"] == 2
    assert final_message["data"]["message"] == "Final"

