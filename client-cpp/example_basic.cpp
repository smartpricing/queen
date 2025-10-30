/**
 * Queen C++ Client - Basic Example
 * 
 * Demonstrates:
 * - Connecting to Queen server
 * - Creating a queue
 * - Pushing messages
 * - Consuming messages
 */

#include "queen_client.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace queen;
using json = nlohmann::json;

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Queen C++ Client - Basic Example" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    try {
        // Create client
        QueenClient client("http://localhost:6632");
        std::cout << "✓ Connected to Queen server" << std::endl;
        
        // Create queue
        auto create_result = client.queue("cpp-example").create();
        std::cout << "✓ Queue created: " << create_result.dump() << std::endl;
        
        // Push some messages
        std::cout << "\nPushing messages..." << std::endl;
        for (int i = 1; i <= 5; i++) {
            auto result = client.queue("cpp-example").push({
                {{"data", {
                    {"message", "Hello from C++!"},
                    {"index", i},
                    {"timestamp", std::time(nullptr)}
                }}}
            });
            std::cout << "  Message " << i << " pushed" << std::endl;
        }
        
        // Wait a moment
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Consume messages
        std::cout << "\nConsuming messages..." << std::endl;
        
        std::atomic<int> count{0};
        
        client.queue("cpp-example")
            .batch(1)
            .limit(5)
            .auto_ack(true)
            .consume([&](const json& msg) {
                count++;
                std::cout << "  [" << count << "] Received: " 
                         << msg["data"]["message"].get<std::string>() 
                         << " (index: " << msg["data"]["index"] << ")"
                         << std::endl;
            });
        
        std::cout << "\n✓ Successfully consumed " << count << " messages" << std::endl;
        
        // Cleanup
        std::cout << "\nCleaning up..." << std::endl;
        client.close();
        std::cout << "✓ Client closed" << std::endl;
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "Example completed successfully!" << std::endl;
        std::cout << "========================================" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

