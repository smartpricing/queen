# 👑 Queen C++ Client

A comprehensive C++ client for Queen Message Queue, providing a fluent API matching the Node.js client.

## Features

- ✅ **Fluent API** - Chainable methods for intuitive queue operations
- ✅ **HTTP Client** - Built-in retry logic and failover support
- ✅ **Load Balancing** - Round-robin and session-based strategies
- ✅ **Client-Side Buffering** - Automatic batching with time/count triggers
- ✅ **Consumer Groups** - Distributed message processing
- ✅ **Partitions** - Ordered message processing per partition
- ✅ **Atomic Transactions** - All-or-nothing operations
- ✅ **Lease Renewal** - Keep message locks active
- ✅ **Dead Letter Queue** - Query failed messages
- ✅ **Concurrent Consumers** - Uses `astp::ThreadPool` for parallel processing
- ✅ **UUIDv7 Generation** - Time-ordered unique identifiers
- ✅ **Graceful Shutdown** - Clean resource cleanup

## Dependencies

- **C++17 or later**
- **nlohmann/json** - JSON library (already in `server/vendor/json.hpp`)
- **cpp-httplib** - Header-only HTTP client (install separately or include)
- **astp::ThreadPool** - Thread pool library (already in `server/include/threadpool.hpp`)

### Installing cpp-httplib

```bash
# Option 1: System-wide installation (macOS with Homebrew)
brew install cpp-httplib

# Option 2: Download header-only library
curl -o httplib.h https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h
```

## Quick Start

### Basic Usage

```cpp
#include "queen_client.hpp"

using namespace queen;

int main() {
    // Connect to Queen server
    QueenClient client("http://localhost:6632");
    
    // Create a queue
    client.queue("tasks").create();
    
    // Push a message
    client.queue("tasks").push({
        {{"data", {{"job", "send-email"}, {"to", "user@example.com"}}}}
    });
    
    // Pop a message
    auto messages = client.queue("tasks")
        .batch(1)
        .wait(false)
        .pop();
    
    if (!messages.empty()) {
        std::cout << "Received: " << messages[0] << std::endl;
    }
    
    client.close();
    return 0;
}
```

### Consumer Pattern

```cpp
#include "queen_client.hpp"

using namespace queen;

int main() {
    QueenClient client("http://localhost:6632");
    
    client.queue("tasks").create();
    
    // Long-running consumer
    client.queue("tasks").consume([](const json& msg) {
        std::cout << "Processing: " << msg["data"] << std::endl;
        // Process message here
        // Auto-ack on success, auto-nack on exception
    });
    
    return 0;
}
```

### With Partitions

```cpp
// Push to specific partition
client.queue("user-events")
    .partition("user-123")
    .push({
        {{"data", {{"event", "login"}, {"timestamp", time(nullptr)}}}}
    });

// Consume from partition
client.queue("user-events")
    .partition("user-123")
    .consume([](const json& msg) {
        // Messages in same partition are processed in order
        std::cout << "User 123 event: " << msg["data"]["event"] << std::endl;
    });
```

### Consumer Groups

```cpp
// Worker 1
client.queue("emails")
    .group("processors")
    .concurrency(2)
    .consume([](const json& msg) {
        send_email(msg["data"]);
    });

// Worker 2 (shares the load with Worker 1)
client.queue("emails")
    .group("processors")
    .concurrency(2)
    .consume([](const json& msg) {
        send_email(msg["data"]);
    });
```

### Client-Side Buffering

```cpp
// Buffer messages for performance
BufferOptions buffer_opts;
buffer_opts.message_count = 100;   // Flush after 100 messages
buffer_opts.time_millis = 1000;    // Or after 1 second

for (int i = 0; i < 10000; i++) {
    client.queue("events")
        .buffer(buffer_opts)
        .push({
            {{"data", {{"id", i}, {"timestamp", time(nullptr)}}}}
        });
}

// Flush any remaining buffered messages
client.flush_all_buffers();
```

### Transactions

```cpp
// Pop from input queue
auto messages = client.queue("raw-data").batch(1).pop();

if (!messages.empty()) {
    auto input = messages[0];
    
    // Process
    json processed = {{"result", input["data"]["value"].get<int>() * 2}};
    
    // Atomic: ack input + push output
    client.transaction()
        .ack(input)
        .queue("processed-data")
        .push({
            {{"data", processed}}
        })
        .commit();
}
```

### Queue Configuration

```cpp
QueueConfig config;
config.lease_time = 600;                    // 10 minutes
config.retry_limit = 5;
config.priority = 10;
config.delayed_processing = 60;             // 1 minute delay
config.max_size = 10000;
config.retention_seconds = 86400;           // 24 hours
config.encryption_enabled = true;

client.queue("important-tasks")
    .config(config)
    .create();
```

### Dead Letter Queue

```cpp
// Query DLQ for failed messages
auto dlq = client.queue("my-queue")
    .dlq()
    .limit(100)
    .from("2025-10-01")
    .to("2025-10-31")
    .get();

std::cout << "Failed messages: " << dlq["total"] << std::endl;

for (const auto& msg : dlq["messages"]) {
    std::cout << "Error: " << msg["errorMessage"] << std::endl;
}
```

### Lease Renewal

```cpp
// Pop message
auto messages = client.queue("long-tasks").pop();
auto msg = messages[0];

// Start long processing
std::thread processing_thread([&]() {
    // Renew lease every 30 seconds
    while (processing) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        client.renew(msg);
    }
});

// Do long work...
process_large_file(msg["data"]);

processing = false;
processing_thread.join();

// Acknowledge completion
client.ack(msg, true);
```

### High Availability

```cpp
// Connect to multiple servers for failover
std::vector<std::string> urls = {
    "http://server1:6632",
    "http://server2:6632",
    "http://server3:6632"
};

ClientConfig config;
config.load_balancing_strategy = "round-robin";
config.enable_failover = true;
config.retry_attempts = 3;

QueenClient client(urls, config);

// Requests are load-balanced across servers
// Automatic failover if a server is down
```

## Building

### Compile Test Suite

```bash
cd client-cpp
make test
```

### Run Tests

```bash
# Make sure Queen server is running on localhost:6632
./bin/test_client

# Or specify custom server URL
./bin/test_client http://my-server:6632
```

### Use in Your Project

#### Option 1: Include Header

```cpp
#include "path/to/queen_client.hpp"
```

#### Option 2: Add to Your Makefile

```makefile
INCLUDES = -I/path/to/queen/client-cpp \
           -I/path/to/queen/server/vendor \
           -I/path/to/queen/server/include

LIBS = -lpthread

your_program: your_program.cpp
	g++ -std=c++17 $(INCLUDES) your_program.cpp -o your_program $(LIBS)
```

## Advanced Features

### Stop Signal for Consumers

```cpp
std::atomic<bool> stop_signal{false};

std::thread consumer_thread([&]() {
    client.queue("tasks").consume([](const json& msg) {
        // Process message
    }, &stop_signal);
});

// Later... gracefully stop consumer
stop_signal = true;
consumer_thread.join();
```

### Batch Processing

```cpp
// Process messages in batches of 10
client.queue("events")
    .batch(10)
    .consume([](const json& messages) {
        // messages is an array of 10 messages
        for (const auto& msg : messages) {
            process(msg);
        }
    });
```

### Concurrency

```cpp
// Run 5 concurrent workers
client.queue("tasks")
    .concurrency(5)
    .consume([](const json& msg) {
        // This handler runs in parallel across 5 workers
        process(msg);
    });
```

### Process Limit

```cpp
// Process exactly 100 messages then stop
client.queue("tasks")
    .limit(100)
    .consume([](const json& msg) {
        process(msg);
    });
```

### Manual ACK

```cpp
// Disable auto-ack for manual control
client.queue("tasks")
    .auto_ack(false)
    .consume([&](const json& msg) {
        try {
            process(msg);
            client.ack(msg, true);  // Manual success
        } catch (const std::exception& e) {
            client.ack(msg, false, {{"error", e.what()}});  // Manual failure
        }
    });
```

## Logging

Enable debug logging:

```bash
export QUEEN_CLIENT_LOG=true
./your_program
```

Output:
```
[2025-10-30T10:30:45.123Z] [INFO] [HttpClient.request] POST /api/v1/push
[2025-10-30T10:30:45.234Z] [INFO] [QueueBuilder.push] queue=tasks count=1
```

## API Reference

### QueenClient

- `QueenClient(url)` - Create client with single server
- `QueenClient(urls, config)` - Create client with multiple servers
- `queue(name)` - Get queue builder
- `transaction()` - Create transaction builder
- `ack(message, status, context)` - Acknowledge message(s)
- `renew(message)` - Renew message lease
- `flush_all_buffers()` - Flush all client-side buffers
- `get_buffer_stats()` - Get buffer statistics
- `close()` - Graceful shutdown

### QueueBuilder

**Configuration:**
- `namespace_name(name)` - Set namespace
- `task(name)` - Set task type
- `config(options)` - Set queue configuration
- `partition(name)` - Set partition
- `group(name)` - Set consumer group

**Operations:**
- `create()` - Create queue
- `del()` - Delete queue
- `push(messages)` - Push messages
- `pop()` - Pop messages
- `consume(handler)` - Start consumer
- `dlq()` - Query dead letter queue

**Consumer Options:**
- `concurrency(count)` - Set worker count
- `batch(size)` - Set batch size
- `limit(count)` - Set message limit
- `auto_ack(enabled)` - Enable/disable auto-ack
- `wait(enabled)` - Enable/disable long polling
- `renew_lease(enabled, interval)` - Auto-renew leases

**Buffering:**
- `buffer(options)` - Enable client-side buffering
- `flush_buffer()` - Flush queue buffer

### TransactionBuilder

- `ack(message)` - Add ack operation
- `queue(name).push(messages)` - Add push operation
- `commit()` - Execute transaction atomically

## Error Handling

```cpp
try {
    client.queue("tasks").push({
        {{"data", {{"value", 123}}}}
    });
} catch (const std::runtime_error& e) {
    std::cerr << "Push failed: " << e.what() << std::endl;
}
```

## Best Practices

1. **Use buffering for high throughput** - Batch messages for better performance
2. **Enable auto-ack for simplicity** - Let the client handle acknowledgments
3. **Use partitions for ordering** - Messages in same partition are processed in order
4. **Consumer groups for scaling** - Run multiple workers to share the load
5. **Transactions for atomicity** - Ensure all-or-nothing operations
6. **Graceful shutdown** - Always call `client.close()` before exiting

## Comparison with Node.js Client

The C++ client API closely matches the Node.js client:

**Node.js:**
```javascript
await queen.queue('tasks').push([{data: {job: 'test'}}]);
```

**C++:**
```cpp
client.queue("tasks").push({{{"data", {{"job", "test"}}}}});
```

**Node.js:**
```javascript
await queen.queue('tasks').consume(async (msg) => {
    console.log(msg.data);
});
```

**C++:**
```cpp
client.queue("tasks").consume([](const json& msg) {
    std::cout << msg["data"] << std::endl;
});
```

## License

Same as Queen Message Queue project.

## Support

For issues and questions, please refer to the main Queen repository.

