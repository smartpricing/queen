#include "queen/server.hpp"
#include <spdlog/spdlog.h>
#include <iostream>
#include <csignal>
#include <memory>

// Global server instance for signal handling
std::unique_ptr<queen::QueenServer> g_server;

void signal_handler(int signal) {
    spdlog::info("Received signal {}, shutting down gracefully...", signal);
    if (g_server) {
        g_server->stop();
    }
    std::exit(0);
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  --port PORT        Server port (default: 6632)\n"
              << "  --host HOST        Server host (default: 0.0.0.0)\n"
              << "  --dev              Enable development mode\n"
              << "  --help             Show this help message\n"
              << "\n"
              << "Environment variables:\n"
              << "  PG_HOST            PostgreSQL host (default: localhost)\n"
              << "  PG_PORT            PostgreSQL port (default: 5432)\n"
              << "  PG_DB              PostgreSQL database (default: postgres)\n"
              << "  PG_USER            PostgreSQL user (default: postgres)\n"
              << "  PG_PASSWORD        PostgreSQL password (default: postgres)\n"
              << "  DB_POOL_SIZE       Database pool size (default: 150)\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    // Set up logging
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    
    // Parse command line arguments
    queen::Config config = queen::Config::load();
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--port" && i + 1 < argc) {
            config.server.port = std::atoi(argv[++i]);
        } else if (arg == "--host" && i + 1 < argc) {
            config.server.host = argv[++i];
        } else if (arg == "--dev") {
            config.server.dev_mode = true;
            spdlog::set_level(spdlog::level::debug);
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Set up signal handlers for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    try {
        // Create and initialize server
        g_server = std::make_unique<queen::QueenServer>(config);
        
        spdlog::info("🚀 Starting Queen C++ Message Queue Server...");
        spdlog::info("📊 Configuration:");
        spdlog::info("   - Host: {}", config.server.host);
        spdlog::info("   - Port: {}", config.server.port);
        spdlog::info("   - Database: {}:{}/{}", config.database.host, config.database.port, config.database.database);
        spdlog::info("   - Pool Size: {}", config.database.pool_size);
        spdlog::info("   - Worker ID: {}", config.server.worker_id);
        spdlog::info("   - Dev Mode: {}", config.server.dev_mode ? "enabled" : "disabled");
        
        if (!g_server->initialize()) {
            spdlog::error("❌ Failed to initialize server");
            return 1;
        }
        
        spdlog::info("✅ Server initialized successfully");
        
        // Start the server (this will block)
        if (!g_server->start()) {
            spdlog::error("❌ Failed to start server");
            return 1;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("❌ Server error: {}", e.what());
        return 1;
    } catch (...) {
        spdlog::error("❌ Unknown server error");
        return 1;
    }
    
    return 0;
}
