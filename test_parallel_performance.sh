#!/bin/bash

# TRUE PARALLEL Performance test - Launch ALL requests simultaneously
# This will show the real difference between sync (blocking) and async (non-blocking)

SERVER_URL="http://localhost:6632"
QUEUE_NAME="test-parallel-queue"
TOTAL_MESSAGES=500  # Reduced to avoid overwhelming
BATCH_SIZE=5        # Smaller batches
MAX_PARALLEL=100    # Launch up to 100 requests simultaneously!

echo "🚀 Queen TRUE PARALLEL Performance Test"
echo "======================================="
echo "Server: $SERVER_URL"
echo "Queue: $QUEUE_NAME"
echo "Total Messages: $TOTAL_MESSAGES"
echo "Batch Size: $BATCH_SIZE"
echo "Max Parallel Requests: $MAX_PARALLEL"
echo ""

# Function to create a batch of messages
create_batch() {
    local start_id=$1
    local batch_size=$2
    local route_type=$3
    
    echo -n '{"items":['
    for ((i=0; i<batch_size; i++)); do
        local msg_id=$((start_id + i))
        if [ $i -gt 0 ]; then echo -n ','; fi
        echo -n "{
            \"queue\": \"$QUEUE_NAME\",
            \"partition\": \"Default\",
            \"payload\": {
                \"message\": \"Parallel test message #$msg_id\",
                \"route_type\": \"$route_type\",
                \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\",
                \"batch_id\": \"$start_id\",
                \"parallel_test\": true
            }
        }"
    done
    echo -n ']}'
}

# Function to send a single batch request (simplified)
send_batch_simple() {
    local route=$1
    local data=$2
    local batch_id=$3
    
    curl -s -X POST "$SERVER_URL$route" \
        -H "Content-Type: application/json" \
        -d "$data" > /dev/null 2>&1
    
    if [ $? -eq 0 ]; then
        echo "✅ Batch $batch_id completed"
    else
        echo "❌ Batch $batch_id failed"
    fi
}

# TRUE PARALLEL test function - launch ALL requests at once!
run_parallel_test() {
    local route=$1
    local route_name=$2
    local total_batches=$((TOTAL_MESSAGES / BATCH_SIZE))
    
    echo "🧪 Testing $route_name Route: $route"
    echo "🚀 Launching ALL $total_batches requests in parallel (no waiting!)"
    echo ""
    
    local start_time=$(date +%s.%N)
    
    # Launch ALL requests simultaneously - no waiting!
    local pids=()
    for ((batch=0; batch<total_batches; batch++)); do
        local start_id=$((batch * BATCH_SIZE + 1))
        local batch_data=$(create_batch $start_id $BATCH_SIZE $route_name)
        
        # Launch in background - NO WAITING!
        send_batch_simple "$route" "$batch_data" "$((batch + 1))" &
        pids+=($!)
        
        # Only limit if we hit the max parallel limit
        if [ ${#pids[@]} -ge $MAX_PARALLEL ]; then
            echo "⏳ Reached $MAX_PARALLEL parallel requests, waiting for some to complete..."
            # Wait for half of them to complete
            for ((i=0; i<$((MAX_PARALLEL/2)); i++)); do
                wait ${pids[i]}
            done
            # Remove the waited PIDs
            pids=("${pids[@]:$((MAX_PARALLEL/2))}")
        fi
    done
    
    echo "⏳ Waiting for all remaining $total_batches requests to complete..."
    
    # Wait for ALL remaining requests
    for pid in "${pids[@]}"; do
        wait $pid
    done
    
    local end_time=$(date +%s.%N)
    local total_duration=$(echo "$end_time - $start_time" | bc)
    local messages_per_second=$(echo "scale=2; $TOTAL_MESSAGES / $total_duration" | bc)
    
    echo ""
    echo "📊 $route_name PARALLEL Results:"
    echo "   Total Time: ${total_duration}s"
    echo "   Messages/Second: $messages_per_second"
    echo "   Total Messages: $TOTAL_MESSAGES"
    echo "   Parallel Requests: $total_batches"
    echo ""
}

# Ensure queue exists
echo "🔧 Setting up test queue..."
curl -s -X POST "$SERVER_URL/api/v1/configure" \
    -H "Content-Type: application/json" \
    -d "{
        \"queue\": \"$QUEUE_NAME\",
        \"namespace\": \"parallel-test\",
        \"task\": \"async-vs-sync\"
    }" > /dev/null

if [ $? -eq 0 ]; then
    echo "✅ Queue configured successfully"
else
    echo "❌ Failed to configure queue - is server running?"
    exit 1
fi

echo ""

# Test 1: Synchronous Route (should struggle with parallel requests)
echo "🐌 Phase 1: Testing SYNCHRONOUS route with PARALLEL requests"
echo "Expected: Should struggle because each request blocks the worker thread"
run_parallel_test "/api/v1/push" "SYNC"

echo "⏳ Waiting 10 seconds between tests..."
sleep 10

# Test 2: Asynchronous Route (should handle parallel requests much better)
echo "🚀 Phase 2: Testing ASYNCHRONOUS route with PARALLEL requests"  
echo "Expected: Should handle parallel requests much better!"
run_parallel_test "/api/v1/push-async-experiment" "ASYNC"

echo "🏁 PARALLEL Performance test completed!"
echo ""
echo "🔍 Key Differences to Observe:"
echo "   1. SYNC route: Worker threads block, requests queue up"
echo "   2. ASYNC route: Worker threads continue, database work happens in parallel"
echo "   3. ASYNC should show MUCH better performance with parallel load"
echo "   4. Check server logs - ASYNC should show many 'continues immediately!' messages"
echo ""
echo "📊 Expected Results:"
echo "   - SYNC: Limited by worker thread blocking (~300-500 msg/s)"
echo "   - ASYNC: Limited by database connections, not threads (~1000+ msg/s)"
echo ""
echo "📋 Monitor server logs:"
echo "   tail -f server/server.log"
