#!/bin/bash

# Full Load Test - Saturate all 95 ThreadPool threads and database connections
# This will show all connections being used simultaneously

SERVER_URL="http://localhost:6632"
QUEUE_NAME="load-test-queue"

# Configuration to saturate 95 threads
TOTAL_CONCURRENT_REQUESTS=100    # More than 95 to ensure saturation
MESSAGES_PER_REQUEST=10          # Smaller batches for more concurrent operations
TOTAL_MESSAGES=1000             # Total messages to send

echo "🚀 Queen FULL LOAD Test - Saturate All 95 Database Connections"
echo "=============================================================="
echo "Server: $SERVER_URL"
echo "Queue: $QUEUE_NAME"
echo "Concurrent Requests: $TOTAL_CONCURRENT_REQUESTS"
echo "Messages per Request: $MESSAGES_PER_REQUEST"
echo "Total Messages: $TOTAL_MESSAGES"
echo ""

# Function to create a batch of messages
create_batch() {
    local batch_id=$1
    local messages_count=$2
    
    echo -n '{"items":['
    for ((i=0; i<messages_count; i++)); do
        local msg_id=$((batch_id * messages_count + i))
        if [ $i -gt 0 ]; then echo -n ','; fi
        echo -n "{
            \"queue\": \"$QUEUE_NAME\",
            \"partition\": \"partition-$((msg_id % 10))\",
            \"payload\": {
                \"message\": \"Load test message #$msg_id\",
                \"batch_id\": $batch_id,
                \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\",
                \"load_test\": true,
                \"worker_stress_test\": true
            }
        }"
    done
    echo -n ']}'
}

# Function to send a request and measure time
send_request() {
    local batch_id=$1
    local route=$2
    
    local start_time=$(date +%s.%N)
    local data=$(create_batch $batch_id $MESSAGES_PER_REQUEST)
    
    local response=$(curl -s -w "%{http_code}" \
        -X POST "$SERVER_URL$route" \
        -H "Content-Type: application/json" \
        -d "$data")
    
    local end_time=$(date +%s.%N)
    local duration=$(echo "$end_time - $start_time" | bc)
    
    local http_code="${response: -3}"
    
    if [ "$http_code" = "201" ]; then
        echo "✅ Batch $batch_id: $MESSAGES_PER_REQUEST messages in ${duration}s"
    else
        echo "❌ Batch $batch_id: FAILED (HTTP $http_code)"
    fi
}

# Setup test queue
echo "🔧 Setting up load test queue..."
curl -s -X POST "$SERVER_URL/api/v1/configure" \
    -H "Content-Type: application/json" \
    -d "{
        \"queue\": \"$QUEUE_NAME\",
        \"namespace\": \"load-test\",
        \"task\": \"connection-saturation\"
    }" > /dev/null

if [ $? -eq 0 ]; then
    echo "✅ Queue configured successfully"
else
    echo "❌ Failed to configure queue"
    exit 1
fi

echo ""
echo "🚀 LAUNCHING $TOTAL_CONCURRENT_REQUESTS CONCURRENT REQUESTS!"
echo "This should saturate all 95 database connections..."
echo ""
echo "📊 Monitor server logs to see:"
echo "   - Pool: X/95 conn (Y in use) where Y approaches 95"
echo "   - All ThreadPool workers active"
echo ""

# Launch ALL requests simultaneously to saturate the system
start_time=$(date +%s.%N)
pids=()

for ((i=1; i<=TOTAL_CONCURRENT_REQUESTS; i++)); do
    send_request $i "/api/v1/push" &
    pids+=($!)
    
    # Small delay to spread out the initial burst
    sleep 0.001
done

echo "⏳ Launched $TOTAL_CONCURRENT_REQUESTS concurrent requests..."
echo "⏳ Waiting for all requests to complete..."

# Wait for all requests to complete
for pid in "${pids[@]}"; do
    wait $pid
done

end_time=$(date +%s.%N)
total_duration=$(echo "$end_time - $start_time" | bc)
total_messages=$((TOTAL_CONCURRENT_REQUESTS * MESSAGES_PER_REQUEST))
messages_per_second=$(echo "scale=2; $total_messages / $total_duration" | bc)

echo ""
echo "📊 FULL LOAD TEST Results:"
echo "   Total Time: ${total_duration}s"
echo "   Total Messages: $total_messages"
echo "   Messages/Second: $messages_per_second"
echo "   Concurrent Requests: $TOTAL_CONCURRENT_REQUESTS"
echo ""
echo "🔍 Check server logs for:"
echo "   - Maximum 'in use' connections (should approach 95)"
echo "   - ThreadPool saturation"
echo "   - No pool timeouts (thanks to global sharing)"
echo ""
echo "🎯 Expected behavior:"
echo "   - Pool: X/95 conn (90+ in use) during peak load"
echo "   - All workers contributing to the load"
echo "   - Smooth operation without connection starvation"
