"""
Queue operation tests
"""

import pytest


@pytest.mark.asyncio
async def test_create_queue(client):
    """Test queue creation"""
    res = await client.queue("test-queue-v2").create()
    assert res.get("configured") is True


@pytest.mark.asyncio
async def test_delete_queue(client):
    """Test queue deletion"""
    # First create it
    await client.queue("test-queue-v2").create()
    # Then delete it
    res = await client.queue("test-queue-v2").delete()
    assert res.get("deleted") is True


@pytest.mark.asyncio
async def test_configure_queue(client):
    """Test queue configuration"""
    config = {
        "lease_time": 60,  # Use a distinct non-default value
        "retry_limit": 5,  # Non-default
        "priority": 7,     # Non-default
        "max_size": 5000,  # Non-default
        "encryption_enabled": True,
        "dlq_after_max_retries": True,
    }
    
    res = await client.queue("test-queue-v2-config").config(config).create()
    
    assert res.get("configured") is True
    
    # Server returns camelCase
    options = res.get("options", {})
    
    # Verify key configurations were applied
    assert options.get("leaseTime") == 60, f"Expected leaseTime=60, got {options.get('leaseTime')}"
    assert options.get("retryLimit") == 5, f"Expected retryLimit=5, got {options.get('retryLimit')}"
    assert options.get("priority") == 7, f"Expected priority=7, got {options.get('priority')}"
    assert options.get("maxSize") == 5000, f"Expected maxSize=5000, got {options.get('maxSize')}"

