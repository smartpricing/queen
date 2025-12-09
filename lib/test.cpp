#include "queen.hpp"
#include <chrono>

int main() {
    queen::Queen queen("postgres://postgres:postgres@localhost:5432/postgres", 30000, 10, 10);
    for (int i = 0; i < 1; i++) {
        queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = "test_queue",
        .partition_name = "test_partition",
        .params = {
            R"([{"queue": "test_queue", "partition": "test_partition", "payload": {"message": "hello world "}}])"
        },
        }, [i](std::string result) {
            std::cout << "Result: " << result << " for job " << i << std::endl;
        });
    }

    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = "test_queue",
        .partition_name = "test_partition",
        .next_check = std::chrono::steady_clock::now(),
        .wait_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60),
        .params = {
            R"([{
                "idx": 0,
                "queue_name": "test_queue",
                "partition_name": "test_partition",
                "consumer_group": "my_consumer",
                "batch_size": 10,
                "lease_seconds": 60,
                "worker_id": "worker-123-abc"
            }])"
        },        
    }, [](std::string result) {
        std::cout << "Result POP: " << result << std::endl;
    });


    // Wait to see the timer fire (100ms = ~10 timer ticks)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    return 0;
}