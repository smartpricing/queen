# C++ Client

High-performance C++ client library for Queen MQ.

## Installation

```bash
cd client-cpp
make
```

**Requirements:**
- C++17 or higher
- libcurl
- OpenSSL

## Quick Start

```cpp
#include "queen_client.hpp"

int main() {
    // Connect
    queen::Client client("http://localhost:6632");
    
    // Create queue
    client.create_queue("tasks", {{"leaseTime", "60"}});
    
    // Push message
    client.push("tasks", {{"data", "process-order"}});
    
    // Pop message
    auto messages = client.pop("tasks");
    
    // Process
    for (const auto& msg : messages) {
        std::cout << "Processing: " << msg["data"] << std::endl;
        client.ack(msg["leaseId"], true);
    }
    
    return 0;
}
```

## Building

```bash
# Build client
make

# Run example
./bin/example_basic

# Run tests
./bin/test_client
```

## API Reference

### Creating Client

```cpp
queen::Client client("http://localhost:6632");

// Multiple servers
queen::Client client({
    "http://server1:6632",
    "http://server2:6632"
});
```

### Push

```cpp
json message = {
    {"data", {{"orderId", 123}, {"amount", 99.99}}}
};

client.push("orders", message, "customer-123");  // With partition
```

### Pop

```cpp
auto messages = client.pop("orders", "customer-123", 10);  // Batch of 10

for (const auto& msg : messages) {
    process(msg);
    client.ack(msg["leaseId"], true);
}
```

### Consumer

```cpp
client.consume("tasks", [](const json& message) {
    std::cout << "Processing: " << message << std::endl;
    return true;  // ACK
}, 10);  // 10 concurrent
```

## Complete Documentation

See:
- [C++ Client README](https://github.com/smartpricing/queen/blob/master/client-cpp/README.md)
- [Implementation Summary](https://github.com/smartpricing/queen/blob/master/client-cpp/IMPLEMENTATION_SUMMARY.md)
- [Examples](https://github.com/smartpricing/queen/tree/master/client-cpp)

## Performance

- **Push**: 7,000-90,000 msg/s (depends on batch size)
- **Pop**: 4,000-488,000 msg/s (peak with large batches)
- **Latency**: <50ms

See [Benchmarks](/server/benchmarks) for detailed performance data.

## Support

[GitHub Repository](https://github.com/smartpricing/queen)
