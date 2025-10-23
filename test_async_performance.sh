#!/bin/bash

# Performance test script for async vs sync push routes
# This script sends 1000+ messages to test the difference

SERVER_URL="http://localhost:6632"
QUEUE_NAME="test-async-queue"
TOTAL_MESSAGES=1000
BATCH_SIZE=10
CONCURRENT_REQUESTS=5

echo "🚀 Queen Async Performance Test"
echo "================================"
echo "Server: $SERVER_URL"
echo "Queue: $QUEUE_NAME"
echo "Total Messages: $TOTAL_MESSAGES"
echo "Batch Size: $BATCH_SIZE"
echo "Concurrent Requests: $CONCURRENT_REQUESTS"
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
                \"message\": \"Performance test message #$msg_id\",
                \"route_type\": \"$route_type\",
                \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\",
                \"batch_id\": \"$start_id\",
                \"worker_test\": true
            }
        }"
    done
    echo -n ']}'
}

# Function to send a batch request
send_batch() {
    local route=$1
    local data=$2
    local batch_id=$3
    local route_type=$4
    
    local start_time=$(date +%s.%N)
    
    local response=$(curl -s -w "\n%{http_code}\n%{time_total}" \
        -X POST "$SERVER_URL$route" \
        -H "Content-Type: application/json" \
        -d "$data")
    
    local end_time=$(date +%s.%N)
    local duration=$(echo "$end_time - $start_time" | bc)
    
    local http_code=$(echo "$response" | tail -n 2 | head -n 1)
    local curl_time=$(echo "$response" | tail -n 1)

    if [ "$http_code" = "200" ]; then
        echo "✅ [$route_type] Batch $batch_id: ${BATCH_SIZE} messages in ${duration}s (HTTP: ${curl_time}s)"
    else
        echo "❌ [$route_type] Batch $batch_id: FAILED (HTTP $http_code)"
    fi
}

# Test function
run_test() {
    local route=$1
    local route_name=$2
    local total_batches=$((TOTAL_MESSAGES / BATCH_SIZE))
    
    echo "🧪 Testing $route_name Route: $route"
    echo "Sending $total_batches batches of $BATCH_SIZE messages each..."
    echo ""
    
    local start_time=$(date +%s.%N)
    
    # Send batches with some concurrency
    local pids=()
    for ((batch=0; batch<total_batches; batch++)); do
        local start_id=$((batch * BATCH_SIZE + 1))
        local batch_data=$(create_batch $start_id $BATCH_SIZE $route_name)
        
        # Send request in background for concurrency
        send_batch "$route" "$batch_data" "$((batch + 1))" "$route_name" &
        pids+=($!)
        
        # Limit concurrent requests
        if [ ${#pids[@]} -ge $CONCURRENT_REQUESTS ]; then
            wait ${pids[0]}
            pids=("${pids[@]:1}")
        fi
        
        # Small delay to avoid overwhelming
        sleep 0.01
    done
    
    # Wait for remaining requests
    for pid in "${pids[@]}"; do
        wait $pid
    done
    
    local end_time=$(date +%s.%N)
    local total_duration=$(echo "$end_time - $start_time" | bc)
    local messages_per_second=$(echo "scale=2; $TOTAL_MESSAGES / $total_duration" | bc)
    
    echo ""
    echo "📊 $route_name Results:"
    echo "   Total Time: ${total_duration}s"
    echo "   Messages/Second: $messages_per_second"
    echo "   Total Messages: $TOTAL_MESSAGES"
    echo ""
}

# Ensure queue exists
echo "🔧 Setting up test queue..."
curl -s -X POST "$SERVER_URL/api/v1/configure" \
    -H "Content-Type: application/json" \
    -d "{
        \"queue\": \"$QUEUE_NAME\",
        \"namespace\": \"performance-test\",
        \"task\": \"async-comparison\"
    }" > /dev/null

if [ $? -eq 0 ]; then
    echo "✅ Queue configured successfully"
else
    echo "❌ Failed to configure queue"
    exit 1
fi

echo ""

# Test 1: Synchronous Route
echo "🔄 Phase 1: Testing SYNCHRONOUS route (baseline)"
run_test "/api/v1/push" "SYNC"

echo "⏳ Waiting 5 seconds between tests..."
sleep 5

# Test 2: Asynchronous Route  
echo "🚀 Phase 2: Testing ASYNCHRONOUS route (experimental)"
run_test "/api/v1/push-async-experiment" "ASYNC"

echo "🏁 Performance test completed!"
echo ""
echo "💡 Key Observations to Look For:"
echo "   1. ASYNC route should show 'Main uWebSockets thread continues immediately!' logs"
echo "   2. ASYNC route should handle concurrent requests better"
echo "   3. Server should remain responsive during ASYNC test"
echo "   4. Check server logs for threading behavior differences"
echo ""
echo "📋 To monitor server logs in real-time:"
echo "   tail -f server.log"
