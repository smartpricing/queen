"""
Test runner for Queen Python client
Equivalent to the Node.js test-v2/run.js
"""

import asyncio
import os
import sys
from datetime import datetime
from typing import List, Dict, Any, Callable
import asyncpg

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

# Global database pool
db_pool = None


async def init_db():
    """Initialize database pool"""
    global db_pool
    db_pool = await asyncpg.create_pool(**TEST_CONFIG["db_config"])
    await db_pool.fetchval("SELECT 1")
    return db_pool


async def close_db():
    """Close database pool"""
    global db_pool
    if db_pool:
        await db_pool.close()


def log(success: bool, *args):
    """Log test result"""
    timestamp = datetime.utcnow().isoformat() + "Z"
    icon = "✅" if success else "❌"
    print(timestamp, icon, *args)


class TestResults:
    """Track test results"""
    
    def __init__(self):
        self.results: List[Dict[str, Any]] = []
    
    def add(self, success: bool, test_name: str, message: str):
        """Add test result"""
        self.results.append({"success": success, "test_name": test_name, "message": message})
    
    def print_results(self):
        """Print all results"""
        print("=" * 80)
        print("Results:")
        for result in self.results:
            icon = "✅" if result["success"] else "❌"
            print(f"{icon} {result['test_name']}: {result['message']}")
        
        passed = sum(1 for r in self.results if r["success"])
        failed = sum(1 for r in self.results if not r["success"])
        total = len(self.results)
        
        print("=" * 80)
        print(f"Overall Results: {passed}/{total} tests passed, {failed}/{total} tests failed")
        print("=" * 80)


async def cleanup_test_data():
    """Cleanup test data"""
    try:
        await db_pool.execute(
            """DELETE FROM queen.queues 
               WHERE name LIKE 'test-%' 
               OR name LIKE 'edge-%' 
               OR name LIKE 'pattern-%' 
               OR name LIKE 'workflow-%'"""
        )
        log(True, "Test data cleaned up")
    except Exception as error:
        log(False, f"Cleanup error: {error}")


async def run_test(test_func: Callable, client: Queen, results: TestResults):
    """Run a single test"""
    test_name = test_func.__name__
    try:
        print(f"Running test: {test_name}")
        result = await test_func(client)
        
        if isinstance(result, dict):
            success = result.get("success", False)
            message = result.get("message", "Test completed")
        else:
            # If test doesn't return dict, assume it passed if no exception
            success = True
            message = "Test completed successfully"
        
        results.add(success, test_name, message)
        log(success, test_name, message)
    except Exception as error:
        results.add(False, test_name, f"Test threw error: {error}")
        log(False, test_name, "Test failed:", str(error))


async def main():
    """Main test runner"""
    # Import test modules
    from . import (
        test_queue,
        test_push,
        test_pop,
        test_consume,
        test_transaction,
        test_subscription,
        test_dlq,
        test_complete,
    )
    
    # Initialize
    client = Queen(TEST_CONFIG["base_urls"][0])
    await init_db()
    results = TestResults()
    
    # Collect all test functions
    human_tests = [
        # Queue tests
        test_queue.test_create_queue,
        test_queue.test_delete_queue,
        test_queue.test_configure_queue,
        
        # Push tests
        test_push.test_push_message,
        test_push.test_push_duplicate_message,
        test_push.test_push_duplicate_message_on_specific_partition,
        test_push.test_push_duplicate_message_on_different_partition,
        test_push.test_push_message_with_transaction_id,
        test_push.test_push_buffered_message,
        test_push.test_push_max_queue_size,
        test_push.test_push_delayed_message,
        test_push.test_push_window_buffer,
        test_push.test_push_large_payload,
        test_push.test_push_null_payload,
        test_push.test_push_empty_payload,
        test_push.test_push_encrypted_payload,
        
        # Pop tests
        test_pop.test_pop_empty_queue,
        test_pop.test_pop_non_empty_queue,
        test_pop.test_pop_with_wait,
        test_pop.test_pop_with_ack,
        test_pop.test_pop_with_ack_reconsume,
        
        # Consume tests
        test_consume.test_consumer,
        test_consume.test_consumer_trace,
        test_consume.test_consumer_namespace,
        test_consume.test_consumer_task,
        test_consume.test_consumer_with_partition,
        test_consume.test_consumer_batch_consume,
        test_consume.test_consumer_ordering,
        test_consume.test_consumer_ordering_batch,
        test_consume.test_consumer_ordering_concurrency,
        test_consume.test_consumer_ordering_concurrency_with_buffered_push,
        test_consume.test_consumer_group,
        test_consume.test_consumer_group_with_partition,
        test_consume.test_manual_ack,
        test_consume.test_retries,
        test_consume.test_retries_consumer_group,
        test_consume.test_auto_renew_lease,
        
        # Transaction tests
        test_transaction.test_transaction_basic_push_ack,
        test_transaction.test_transaction_multiple_pushes,
        test_transaction.test_transaction_multiple_acks,
        test_transaction.test_transaction_ack_with_status,
        test_transaction.test_transaction_atomicity,
        test_transaction.test_transaction_chained_processing,
        test_transaction.test_transaction_batch_push,
        test_transaction.test_transaction_with_partitions,
        test_transaction.test_transaction_with_consumer,
        test_transaction.test_transaction_empty_commit,
        test_transaction.test_transaction_large_payload,
        test_transaction.test_transaction_multiple_queues,
        test_transaction.test_transaction_rollback,
        
        # Subscription tests
        test_subscription.test_subscription_mode_new,
        test_subscription.test_subscription_mode_new_only,
        test_subscription.test_subscription_from_now,
        test_subscription.test_subscription_from_timestamp,
        test_subscription.test_subscription_mode_all,
        test_subscription.test_subscription_mode_server_default,
        
        # DLQ tests
        test_dlq.test_dlq,
        
        # Complete workflow tests
        test_complete.test_complete,
    ]
    
    # Parse command line arguments
    if len(sys.argv) > 1:
        arg = sys.argv[1]
        if arg == "human":
            tests_to_run = human_tests
            log(True, f"Running human-written tests only ({len(human_tests)} tests)...")
        elif arg in [func.__name__ for func in human_tests]:
            # Run specific test
            test_func = next(func for func in human_tests if func.__name__ == arg)
            tests_to_run = [test_func]
            log(True, f"Running single test: {arg}")
        else:
            print(f"❌ Test '{arg}' not found")
            print("\nUsage:")
            print("  python run_tests.py              # Run all tests")
            print("  python run_tests.py human        # Run human-written tests")
            print("  python run_tests.py <testName>   # Run specific test")
            print("\nAvailable tests:")
            print("\n👤 Human-written tests:")
            for func in human_tests:
                print(f"  - {func.__name__}")
            await close_db()
            sys.exit(1)
    else:
        tests_to_run = human_tests
        log(True, f"Running all tests ({len(human_tests)} tests)...")
    
    # Cleanup before tests
    await cleanup_test_data()
    
    # Run tests
    for test_func in tests_to_run:
        await run_test(test_func, client, results)
    
    # Print results
    results.print_results()
    
    # Cleanup after tests
    await close_db()
    await client.close()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except Exception as error:
        log(False, "Main error:", str(error))
        sys.exit(1)

