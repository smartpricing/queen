#include "queen/config.hpp"
#include <spdlog/spdlog.h>
#include <csignal>
#include <atomic>

namespace queen {
    extern std::atomic<bool> g_shutdown;
    bool start_acceptor_server(const Config& config);
}

void signal_handler(int signal) {
    spdlog::info("Received signal {}, shutting down...", signal);
    queen::g_shutdown.store(true);
    spdlog::info("Exiting (worker threads will be terminated by OS)");
    std::_Exit(0);
}

int main(int argc, char* argv[]) {
    // Set up logging
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    
    // Load configuration
    queen::Config config = queen::Config::load();
    
    // Parse command line
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            config.server.port = std::atoi(argv[++i]);
        } else if (arg == "--host" && i + 1 < argc) {
            config.server.host = argv[++i];
        } else if (arg == "--dev") {
            config.server.dev_mode = true;
            spdlog::set_level(spdlog::level::debug);
        }
    }
    
    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    try {
        spdlog::info("Queen C++ Message Queue Server (Acceptor/Worker Pattern)");
        spdlog::info("Configuration:");
        spdlog::info("  - Host: {}", config.server.host);
        spdlog::info("  - Port: {}", config.server.port);
        spdlog::info("  - Database: {}:{}/{}", config.database.host, config.database.port, config.database.database);
        spdlog::info("  - Pattern: Acceptor distributes to workers (cross-platform)");
        
        // Start acceptor/worker server
        if (!queen::start_acceptor_server(config)) {
            spdlog::error("Failed to start server");
            return 1;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Server error: {}", e.what());
        return 1;
    }
    
    spdlog::info("Server exited cleanly");
    return 0;
}

