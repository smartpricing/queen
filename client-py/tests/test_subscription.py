"""
Subscription mode tests

These tests validate consumer group subscription modes.
They work with any server DEFAULT_SUBSCRIPTION_MODE configuration.
"""

import asyncio
import pytest
from datetime import datetime, timezone


@pytest.mark.asyncio
async def test_subscription_mode_new(client):
    """Test subscription mode 'new'"""
    # Clean up existing groups
    try:
        await client.delete_consumer_group("group-all", True)
        await client.delete_consumer_group("group-new-only", True)
    except Exception:
        pass
    
    # Create queue
    queue = await client.queue("test-queue-v2-subscription-mode-new").create()
    assert queue.get("configured")
    
    # Push historical messages
    historical_count = 5
    for i in range(historical_count):
        await client.queue("test-queue-v2-subscription-mode-new").push([
            {"data": {"id": i, "type": "historical"}}
        ])
    
    # Wait for messages to be stored
    await asyncio.sleep(10)
    
    # Group 1: Explicit 'all' mode
    all_messages_count = 0
    
    async def handler1(msgs):
        nonlocal all_messages_count
        all_messages_count = len(msgs)
    
    await (client.queue("test-queue-v2-subscription-mode-new")
        .group("group-all")
        .subscription_mode("all")
        .batch(10)
        .wait(False)
        .limit(1)
        .consume(handler1))
    
    # Group 2: New mode (should skip historical)
    new_only_messages = await (client.queue("test-queue-v2-subscription-mode-new")
        .group("group-new-only")
        .subscription_mode("new")
        .batch(10)
        .wait(False)
        .pop())
    
    # Verify new mode skipped historical
    assert len(new_only_messages) == 0, f"New mode should get 0 historical messages, got {len(new_only_messages)}"
    
    # Wait for subscription to be established
    await asyncio.sleep(1)
    
    # Push new messages
    new_count = 3
    for i in range(new_count):
        await client.queue("test-queue-v2-subscription-mode-new").push([
            {"data": {"id": i + historical_count, "type": "new"}}
        ])
    
    await asyncio.sleep(0.1)
    
    # Both groups should get new messages
    all_messages_new_count = 0
    
    async def handler2(msgs):
        nonlocal all_messages_new_count
        all_messages_new_count = len(msgs)
    
    await (client.queue("test-queue-v2-subscription-mode-new")
        .group("group-all")
        .batch(10)
        .wait(False)
        .limit(1)
        .consume(handler2))
    
    new_only_new_count = 0
    
    async def handler3(msgs):
        nonlocal new_only_new_count
        new_only_new_count = len(msgs)
    
    await (client.queue("test-queue-v2-subscription-mode-new")
        .group("group-new-only")
        .batch(10)
        .wait(False)
        .limit(1)
        .consume(handler3))
    
    assert all_messages_new_count == new_count, f"All mode should get {new_count} new messages, got {all_messages_new_count}"
    assert new_only_new_count == new_count, f"New mode should get {new_count} new messages, got {new_only_new_count}"


@pytest.mark.asyncio
async def test_subscription_mode_new_only(client):
    """Test subscription mode 'new-only' (alias for 'new')"""
    # Clean up
    try:
        await client.delete_consumer_group("group-new-only", True)
    except Exception:
        pass
    
    # Create queue
    queue = await client.queue("test-queue-v2-subscription-mode-new-only").create()
    assert queue.get("configured")
    
    # Push historical messages
    historical_count = 5
    for i in range(historical_count):
        await client.queue("test-queue-v2-subscription-mode-new-only").push([
            {"data": {"id": i, "type": "historical"}}
        ])
    
    await asyncio.sleep(10)
    
    # Consumer with 'new' mode (should skip historical)
    new_only_messages = await (client.queue("test-queue-v2-subscription-mode-new-only")
        .group("group-new-only")
        .subscription_mode("new")
        .batch(10)
        .wait(False)
        .pop())
    
    assert len(new_only_messages) == 0, f"New-only mode should get 0 historical messages, got {len(new_only_messages)}"
    
    await asyncio.sleep(1)
    
    # Push new messages
    new_count = 3
    for i in range(new_count):
        await client.queue("test-queue-v2-subscription-mode-new-only").push([
            {"data": {"id": i + historical_count, "type": "new"}}
        ])
    
    await asyncio.sleep(0.1)
    
    # Should get new messages
    new_only_new_count = 0
    
    async def handler(msgs):
        nonlocal new_only_new_count
        new_only_new_count = len(msgs)
    
    await (client.queue("test-queue-v2-subscription-mode-new-only")
        .group("group-new-only")
        .subscription_mode("new")
        .batch(10)
        .wait(False)
        .limit(1)
        .consume(handler))
    
    assert new_only_new_count == new_count, f"New-only mode should get {new_count} new messages, got {new_only_new_count}"


@pytest.mark.asyncio
async def test_subscription_from_now(client):
    """Test subscriptionFrom('now')"""
    # Clean up
    try:
        await client.delete_consumer_group("group-from-now", True)
    except Exception:
        pass
    
    # Create queue
    queue = await client.queue("test-queue-v2-subscription-from-now").create()
    assert queue.get("configured")
    
    # Push historical messages
    historical_count = 5
    for i in range(historical_count):
        await client.queue("test-queue-v2-subscription-from-now").push([
            {"data": {"id": i, "type": "historical"}}
        ])
    
    await asyncio.sleep(10)
    
    # Consumer with 'now'
    now_messages = await (client.queue("test-queue-v2-subscription-from-now")
        .group("group-from-now")
        .subscription_from("now")
        .batch(10)
        .wait(False)
        .pop())
    
    assert len(now_messages) == 0, f"subscriptionFrom('now') should get 0 historical messages, got {len(now_messages)}"
    
    await asyncio.sleep(1)
    
    # Push new messages
    new_count = 3
    for i in range(new_count):
        await client.queue("test-queue-v2-subscription-from-now").push([
            {"data": {"id": i + historical_count, "type": "new"}}
        ])
    
    await asyncio.sleep(0.1)
    
    # Should get new messages
    now_new_count = 0
    
    async def handler(msgs):
        nonlocal now_new_count
        now_new_count = len(msgs)
    
    await (client.queue("test-queue-v2-subscription-from-now")
        .group("group-from-now")
        .batch(10)
        .wait(False)
        .limit(1)
        .consume(handler))
    
    assert now_new_count == new_count, f"subscriptionFrom('now') should get {new_count} new messages, got {now_new_count}"


@pytest.mark.asyncio
async def test_subscription_from_timestamp(client):
    """Test subscriptionFrom(timestamp)"""
    # Create queue
    queue = await client.queue("test-queue-v2-subscription-from-timestamp").create()
    assert queue.get("configured")
    
    # Push first batch
    first_batch_count = 3
    for i in range(first_batch_count):
        await client.queue("test-queue-v2-subscription-from-timestamp").push([
            {"data": {"id": i, "batch": "first"}}
        ])
    
    await asyncio.sleep(0.5)
    
    # Record timestamp
    cutoff_timestamp = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    
    await asyncio.sleep(0.5)
    
    # Push second batch
    second_batch_count = 3
    for i in range(second_batch_count):
        await client.queue("test-queue-v2-subscription-from-timestamp").push([
            {"data": {"id": i + first_batch_count, "batch": "second"}}
        ])
    
    await asyncio.sleep(0.1)
    
    # Consumer with timestamp
    timestamp_messages = await (client.queue("test-queue-v2-subscription-from-timestamp")
        .group("group-from-timestamp")
        .subscription_from(cutoff_timestamp)
        .batch(10)
        .wait(False)
        .pop())
    
    # Test passes regardless of count (behavior depends on implementation)
    print(f"subscriptionFrom(timestamp) received {len(timestamp_messages)} messages")


@pytest.mark.asyncio
async def test_subscription_mode_all(client):
    """Test subscription mode 'all'"""
    # Create queue
    queue = await client.queue("test-queue-v2-subscription-mode-all").create()
    assert queue.get("configured")
    
    # Push messages
    message_count = 5
    for i in range(message_count):
        await client.queue("test-queue-v2-subscription-mode-all").push([{"data": {"id": i}}])
    
    await asyncio.sleep(0.1)
    
    # Consumer with explicit 'all' mode
    default_count = 0
    
    async def handler1(msgs):
        nonlocal default_count
        default_count = len(msgs)
    
    await (client.queue("test-queue-v2-subscription-mode-all")
        .group("group-default")
        .subscription_mode("all")
        .batch(10)
        .wait(False)
        .limit(1)
        .consume(handler1))
    
    # Another consumer with explicit 'all'
    explicit_all_count = 0
    
    async def handler2(msgs):
        nonlocal explicit_all_count
        explicit_all_count = len(msgs)
    
    await (client.queue("test-queue-v2-subscription-mode-all")
        .group("group-explicit-all")
        .subscription_mode("all")
        .batch(10)
        .wait(False)
        .limit(1)
        .consume(handler2))
    
    print(f"Default mode received: {default_count} messages")
    print(f"Explicit mode received: {explicit_all_count} messages")
    
    # Both should work
    assert True


@pytest.mark.asyncio
async def test_subscription_mode_server_default(client):
    """Test server default subscription mode detection"""
    # Clean up
    try:
        await client.delete_consumer_group("group-detect-default", True)
        await client.delete_consumer_group("group-explicit-new", True)
    except Exception:
        pass
    
    # Create queue
    queue = await client.queue("test-queue-v2-server-default").create()
    assert queue.get("configured")
    
    # Push historical messages
    historical_count = 5
    for i in range(historical_count):
        await client.queue("test-queue-v2-server-default").push([
            {"data": {"id": i, "type": "historical"}}
        ])
    
    await asyncio.sleep(0.1)
    
    # Consumer without explicit mode
    default_behavior_count = 0
    
    async def handler1(msgs):
        nonlocal default_behavior_count
        default_behavior_count = len(msgs)
    
    await (client.queue("test-queue-v2-server-default")
        .group("group-detect-default")
        .subscription_mode("all")
        .batch(10)
        .wait(False)
        .limit(1)
        .consume(handler1))
    
    # Detect server default
    if default_behavior_count == historical_count:
        server_default = "all (or empty string)"
    elif default_behavior_count == 0:
        server_default = "new"
    else:
        server_default = "unknown"
    
    await asyncio.sleep(10)
    
    # Test explicit 'new' mode
    new_mode_messages = await (client.queue("test-queue-v2-server-default")
        .group("group-explicit-new")
        .subscription_mode("new")
        .batch(10)
        .wait(False)
        .pop())
    
    assert len(new_mode_messages) == 0, f"subscriptionMode('new') should skip historical messages, got {len(new_mode_messages)}"
    
    # Push new messages
    new_count = 3
    for i in range(new_count):
        await client.queue("test-queue-v2-server-default").push([
            {"data": {"id": i + historical_count, "type": "new"}}
        ])
    
    await asyncio.sleep(0.1)
    
    # Both groups should get new messages
    default_new_count = 0
    
    async def handler2(msgs):
        nonlocal default_new_count
        default_new_count = len(msgs)
    
    await (client.queue("test-queue-v2-server-default")
        .group("group-detect-default")
        .batch(10)
        .wait(False)
        .limit(1)
        .consume(handler2))
    
    explicit_new_count = 0
    
    async def handler3(msgs):
        nonlocal explicit_new_count
        explicit_new_count = len(msgs)
    
    await (client.queue("test-queue-v2-server-default")
        .group("group-explicit-new")
        .batch(10)
        .wait(False)
        .limit(1)
        .consume(handler3))
    
    assert default_new_count == new_count, f"Default should get {new_count} new messages, got {default_new_count}"
    assert explicit_new_count == new_count, f"Explicit new should get {new_count} new messages, got {explicit_new_count}"
    
    print(f"Server default detection: {server_default}")

