# Maintenance Mode Test

## Overview

Comprehensive test for the maintenance mode feature with file buffer.

## Test Scenario

1. **Start consumer** - Begins consuming messages from queue
2. **Start producer** - Produces 10 messages/second
3. **Enable maintenance** - Triggers maintenance mode via API
4. **Verify buffering** - Consumer stops receiving (messages go to file buffer)
5. **Continue producing** - Producer keeps running for 10 seconds during maintenance
6. **Disable maintenance** - Deactivates maintenance mode via API
7. **Verify drain** - Consumer resumes receiving messages
8. **Stop producer** - Halts message production
9. **Wait for completion** - Waits 10 seconds for file buffer to drain
10. **Verify counts** - Ensures all produced messages were received

## Running the Test

### Standalone

```bash
cd /Users/alice/Work/queen/client-js/test-v2
node maintenance.js
```

### Via Test Runner

```bash
# Run just this test
node run.js test_maintenance_mode

# Run all human tests (includes this one)
node run.js human

# Run all tests
node run.js
```

## Expected Output

```
🧪 Testing Maintenance Mode with File Buffer

📋 Step 1: Configuring queue "test-maintenance-queue"...
✅ Queue configured

📥 Step 2: Starting consumer...
✅ Consumer started

📤 Step 3: Starting producer (10 msgs/sec)...
✅ Producer started

⏱️  Step 4: Waiting 3 seconds for normal message flow...
  📨 Received 10 messages (total: 10)
  📨 Received 10 messages (total: 20)
  📨 Received 10 messages (total: 30)
   Produced: 30, Received: 30

🔧 Step 5: Enabling MAINTENANCE MODE...
   Response: { maintenanceMode: true, bufferedMessages: 0, ... }
✅ Maintenance mode enabled

⏱️  Step 6: Waiting 2 seconds - consumer should stop receiving...
   Messages received during maintenance: 0
   Total: Produced=50, Received=30

⏱️  Step 7: Producing during maintenance for 10 seconds...
   (Messages should go to file buffer)

   Maintenance period complete:
   - Total produced: 150
   - Total received: 30
   - Buffered (should be ~100): 120

📊 Step 8: Checking maintenance status...
   Status: { maintenanceMode: true, bufferedMessages: 120, ... }

✅ Step 9: Disabling MAINTENANCE MODE...
   Response: { maintenanceMode: false, bufferedMessages: 120, ... }
   (File buffer should start draining to database)

⏱️  Step 10: Waiting for messages to resume...
  📨 Received 10 messages (total: 40)
   Messages received after resuming: 10
✅ Messages are flowing again!

🛑 Step 11: Stopping producer...
   Final produced count: 150

⏱️  Step 12: Waiting 10 seconds for file buffer to drain...
   1s - Received: 50/150 (33.3%)
  📨 Received 10 messages (total: 60)
   2s - Received: 70/150 (46.7%)
  📨 Received 10 messages (total: 80)
   3s - Received: 90/150 (60.0%)
  📨 Received 10 messages (total: 100)
   4s - Received: 110/150 (73.3%)
  📨 Received 10 messages (total: 120)
   5s - Received: 130/150 (86.7%)
  📨 Received 10 messages (total: 140)
   6s - Received: 150/150 (100.0%)
   ✅ All messages received!

🛑 Step 13: Stopping consumer...
✅ Consumer stopped

📊 Final Verification:

   Total Produced: 150
   Total Received: 150
   Difference: 0

✅ SUCCESS: All messages accounted for!
   Maintenance mode works correctly with file buffer.

👋 Queen client closed

✅ All tests passed!
```

## What It Tests

✅ **Maintenance mode activation** - API responds correctly
✅ **Message buffering** - Messages go to file buffer (consumer stops receiving)
✅ **Continued production** - Can push messages during maintenance
✅ **Maintenance mode deactivation** - API responds correctly
✅ **Automatic drain** - File buffer drains to database
✅ **Message ordering** - All messages received in order (FIFO)
✅ **No message loss** - Produced count == Received count
✅ **Multi-instance support** - Uses database for state (works across restarts)

## Success Criteria

- All produced messages are eventually received
- Consumer stops receiving during maintenance
- Consumer resumes receiving after maintenance is disabled
- No duplicates or lost messages
- Drain completes within 10 seconds

## Notes

- Uses `axios` for direct API calls (maintenance endpoints)
- Uses Queen client for queue operations (push/pop/ack)
- Test takes ~25 seconds total (3s warmup + 10s maintenance + 10s drain)

