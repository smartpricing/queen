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
                "sub_mode": "new",
                "sub_from": "now",
                "worker_id": "worker-123-abc"
            }])"
        },        
    }, [&queen](std::string result) {
        std::cout << "Result POP: " << result << std::endl;
    
        // Parse the POP result to get message info
        auto json = nlohmann::json::parse(result);
        // Result format: [{"idx":0,"result":{"messages":[...],"leaseId":"...","partitionId":"..."}}]
        
        if (!json.empty() && json[0].contains("result")) {
            auto& res = json[0]["result"];
            auto& messages = res["messages"];
            std::string leaseId = res["leaseId"];
            std::string partitionId = res["partitionId"];
            std::string consumerGroup = res.value("consumerGroup", "my_consumer");
            
            // ACK each message
            nlohmann::json acks = nlohmann::json::array();
            int idx = 0;
            for (auto& msg : messages) {
                acks.push_back({
                    {"index", idx++},
                    {"transactionId", msg["transactionId"]},
                    {"partitionId", partitionId},
                    {"consumerGroup", consumerGroup},
                    {"leaseId", leaseId},
                    {"status", "completed"}
                });
            }
            
            if (!acks.empty()) {
                queen.submit(queen::JobRequest{
                    .op_type = queen::JobType::ACK,
                    .params = { acks.dump() },
                }, [](std::string result) {
                    std::cout << "Result ACK: " << result << std::endl;
                });
            }
        }
    }); 


    // Wait to see the timer fire (100ms = ~10 timer ticks)
    std::this_thread::sleep_for(std::chrono::milliseconds(100000));
    
    return 0;
}