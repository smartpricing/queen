"""
Pytest configuration and fixtures for Queen client tests
"""

import asyncio
import os
import pytest
import asyncpg
from typing import AsyncGenerator

from queen import Queen


# Test configuration
TEST_CONFIG = {
    "base_urls": ["http://localhost:6632"],
    "db_config": {
        "host": os.environ.get("PG_HOST", "localhost"),
        "port": int(os.environ.get("PG_PORT", 5432)),
        "database": os.environ.get("PG_DB", "postgres"),
        "user": os.environ.get("PG_USER", "postgres"),
        "password": os.environ.get("PG_PASSWORD", "postgres"),
    },
}


@pytest.fixture(scope="session")
def event_loop():
    """Create an instance of the default event loop for the test session."""
    loop = asyncio.get_event_loop_policy().new_event_loop()
    yield loop
    loop.close()


@pytest.fixture(scope="session")
async def db_pool():
    """Create database pool for tests"""
    pool = await asyncpg.create_pool(**TEST_CONFIG["db_config"])
    yield pool
    await pool.close()


@pytest.fixture
async def client():
    """Create Queen client for tests"""
    queen = Queen(TEST_CONFIG["base_urls"][0])
    yield queen
    await queen.close()


@pytest.fixture(scope="session", autouse=True)
async def cleanup_test_data(db_pool):
    """Cleanup test data before and after test run"""
    
    async def cleanup():
        try:
            await db_pool.execute(
                """DELETE FROM queen.queues 
                   WHERE name LIKE 'test-%' 
                   OR name LIKE 'edge-%' 
                   OR name LIKE 'pattern-%' 
                   OR name LIKE 'workflow-%'"""
            )
            print("Test data cleaned up")
        except Exception as error:
            print(f"Cleanup error: {error}")
    
    # Cleanup before tests
    await cleanup()
    
    yield
    
    # Cleanup after tests (commented out for debugging)
    # await cleanup()

