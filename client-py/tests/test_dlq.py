"""
Dead Letter Queue tests
"""

import asyncio
import pytest
import time


@pytest.mark.asyncio
async def test_dlq(client):
    """Test DLQ functionality"""
    queue_name = "test-queue-v2-dlq"
    
    # Create queue with DLQ enabled and low retry limit
    queue = await client.queue(queue_name).config({
        "retry_limit": 2,  # 2 retries before DLQ (total 3 attempts)
        "dlq_after_max_retries": True
    }).create()
    
    assert queue.get("configured")
    
    # Push test message
    test_message = {"message": "Test DLQ message", "timestamp": time.time()}
    await client.queue(queue_name).push([{"data": test_message}])
    
    # Consume and always fail (will exhaust retries and go to DLQ)
    attempt_count = 0
    
    async def handler(msg):
        nonlocal attempt_count
        attempt_count += 1
        print(f"Attempt {attempt_count}: Processing message (will fail)")
        # Always fail
        raise Exception("Test error - triggering DLQ")
    
    async def on_error(msg, error):
        # Ack as failed to trigger retry/DLQ
        await client.ack(msg, False, {"error": str(error)})
    
    await (client.queue(queue_name)
        .batch(1)
        .wait(False)
        .idle_millis(2000)  # Stop after 2 seconds of no messages
        .each()
        .consume(handler)
        .on_error(on_error))
    
    print(f"Total attempts: {attempt_count}")
    
    # Wait for DLQ processing
    await asyncio.sleep(2)
    
    # Check DLQ
    dlq_result = await client.queue(queue_name).dlq().limit(10).get()
    
    print(f"DLQ result: {dlq_result}")
    print(f"DLQ total: {dlq_result.get('total', 0)}")
    print(f"DLQ messages: {len(dlq_result.get('messages', []))}")
    
    assert dlq_result, "Failed to query DLQ"
    assert "messages" in dlq_result
    
    # Test passes if we can query DLQ (even if empty)
    # DLQ population depends on server retry logic which may vary
    print(f"DLQ test completed: {dlq_result.get('total', 0)} messages in DLQ after {attempt_count} attempts")
    
    # Verify message content
    dlq_message = dlq_result["messages"][0]
    
    assert "data" in dlq_message
    assert dlq_message["data"]["message"] == test_message["message"]
    assert "errorMessage" in dlq_message
    assert "Test error" in dlq_message["errorMessage"]

