#include "queen.hpp"
#include <chrono>

int main() {
    queen::Queen queen("postgres://postgres:postgres@localhost:5432/postgres", 30000, 10, 10);
    for (int i = 0; i < 10; i++) {
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = "test_queue",
        .partition_name = "test_partition",
        .params = {
            R"([{"queue": "test_queue", "partition": "test_partition", "payload": {"message": "hello world"}}])"
        },
    }, [](std::string result) {
        std::cout << "Result: " << result << std::endl;
    });
}
    // Wait to see the timer fire (100ms = ~10 timer ticks)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    return 0;
}