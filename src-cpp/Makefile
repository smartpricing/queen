# Queen Message Queue - C++ Implementation
# Requires C++17 or later

CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -pthread -DWITH_OPENSSL=1
# Auto-detect include and library paths
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS - check for Homebrew paths
    BREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo /usr/local)
    PG_CONFIG := $(shell which pg_config 2>/dev/null || echo "")
    ifneq ($(PG_CONFIG),)
        PG_INCLUDES := $(shell $(PG_CONFIG) --includedir)
        PG_LIBS := $(shell $(PG_CONFIG) --libdir)
    else
        # Check for versioned PostgreSQL (e.g., postgresql@14)
        PG_VERSION_DIR := $(shell find $(BREW_PREFIX)/include -name "postgresql@*" -type d 2>/dev/null | head -1)
        ifneq ($(PG_VERSION_DIR),)
            PG_INCLUDES := $(PG_VERSION_DIR) $(BREW_PREFIX)/include/postgresql $(BREW_PREFIX)/opt/postgresql/include
            PG_VERSION := $(shell basename $(PG_VERSION_DIR))
            PG_LIBS := $(BREW_PREFIX)/lib $(BREW_PREFIX)/opt/$(PG_VERSION)/lib $(BREW_PREFIX)/opt/postgresql/lib
        else
            PG_INCLUDES := $(BREW_PREFIX)/include/postgresql $(BREW_PREFIX)/opt/postgresql/include
            PG_LIBS := $(BREW_PREFIX)/lib $(BREW_PREFIX)/opt/postgresql/lib
        endif
    endif
    SSL_INCLUDES := $(BREW_PREFIX)/include $(BREW_PREFIX)/opt/openssl/include
    SSL_LIBS := $(BREW_PREFIX)/lib $(BREW_PREFIX)/opt/openssl/lib
    INCLUDES = -I./include -I./vendor -I./vendor/uSockets/src $(addprefix -I,$(PG_INCLUDES)) $(addprefix -I,$(SSL_INCLUDES))
    LIBS = $(addprefix -L,$(PG_LIBS)) $(addprefix -L,$(SSL_LIBS)) -lpq -lssl -lcrypto -lz
else
    # Linux
    INCLUDES = -I./include -I./vendor -I./vendor/uSockets/src -I/usr/include/postgresql -I/usr/local/include/postgresql
    LIBS = -lpq -lssl -lcrypto -lz
endif

# Directories
SRC_DIR = src
INCLUDE_DIR = include
VENDOR_DIR = vendor
BUILD_DIR = build
BIN_DIR = bin

# Source files
SOURCES = $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/**/*.cpp)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

# uSockets source files (C and C++ files)
USOCKETS_C_SOURCES = $(wildcard $(VENDOR_DIR)/uSockets/src/*.c) $(wildcard $(VENDOR_DIR)/uSockets/src/**/*.c)
USOCKETS_CPP_SOURCES = $(wildcard $(VENDOR_DIR)/uSockets/src/*.cpp) $(wildcard $(VENDOR_DIR)/uSockets/src/**/*.cpp)
USOCKETS_C_OBJECTS = $(USOCKETS_C_SOURCES:$(VENDOR_DIR)/uSockets/src/%.c=$(BUILD_DIR)/usockets/%.o)
USOCKETS_CPP_OBJECTS = $(USOCKETS_CPP_SOURCES:$(VENDOR_DIR)/uSockets/src/%.cpp=$(BUILD_DIR)/usockets/%.o)
USOCKETS_OBJECTS = $(USOCKETS_C_OBJECTS) $(USOCKETS_CPP_OBJECTS)

# Target executable
TARGET = $(BIN_DIR)/queen-server

# Header-only dependencies (will be downloaded)
UWS_URL = https://github.com/uNetworking/uWebSockets/archive/refs/heads/master.zip
USOCKETS_URL = https://github.com/uNetworking/uSockets/archive/refs/heads/master.zip
NLOHMANN_JSON_URL = https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
SPDLOG_URL = https://github.com/gabime/spdlog/archive/refs/heads/v1.x.zip

.PHONY: all clean deps setup test

all: setup deps $(TARGET)

# Create directory structure
setup:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR) $(INCLUDE_DIR) $(VENDOR_DIR)
	@mkdir -p $(SRC_DIR)/database $(SRC_DIR)/managers $(SRC_DIR)/routes $(SRC_DIR)/utils

# Download and setup dependencies
deps: $(VENDOR_DIR)/.deps_ready

$(VENDOR_DIR)/.deps_ready: setup
	@echo "Setting up header-only dependencies..."
	
	# Download nlohmann/json (single header)
	@curl -L $(NLOHMANN_JSON_URL) -o $(VENDOR_DIR)/json.hpp
	
	# Download uWebSockets
	@cd $(VENDOR_DIR) && \
		curl -L $(UWS_URL) -o uws.zip && \
		unzip -q uws.zip && \
		rm -rf uWebSockets && \
		mv uWebSockets-master uWebSockets && \
		rm uws.zip
	
	# Download uSockets (required by uWebSockets)
	@cd $(VENDOR_DIR) && \
		curl -L $(USOCKETS_URL) -o usockets.zip && \
		unzip -q usockets.zip && \
		rm -rf uSockets && \
		mv uSockets-master uSockets && \
		rm usockets.zip
	
	# Download spdlog
	@cd $(VENDOR_DIR) && \
		curl -L $(SPDLOG_URL) -o spdlog.zip && \
		unzip -q spdlog.zip && \
		rm -rf spdlog && \
		mv spdlog-1.x spdlog && \
		rm spdlog.zip
	
	@touch $(VENDOR_DIR)/.deps_ready
	@echo "Dependencies ready!"

# Build target
$(TARGET): $(OBJECTS) $(USOCKETS_OBJECTS)
	@echo "Linking $(TARGET)..."
	@$(CXX) $(OBJECTS) $(USOCKETS_OBJECTS) -o $@ $(LIBS)
	@echo "Build complete!"

# Compile C++ source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -I$(VENDOR_DIR)/uWebSockets/src -I$(VENDOR_DIR)/spdlog/include -I$(VENDOR_DIR) -c $< -o $@

# Compile uSockets C files
$(BUILD_DIR)/usockets/%.o: $(VENDOR_DIR)/uSockets/src/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling uSockets $<..."
	@gcc -O3 -c $(INCLUDES) -DWITH_OPENSSL=1 -DLIBUS_USE_OPENSSL $< -o $@

# Compile uSockets C++ files
$(BUILD_DIR)/usockets/%.o: $(VENDOR_DIR)/uSockets/src/%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling uSockets C++ $<..."
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -DWITH_OPENSSL=1 -DLIBUS_USE_OPENSSL -c $< -o $@

# Test with existing Node.js test suite
test: $(TARGET)
	@echo "Starting C++ Queen server for testing..."
	@./$(BIN_DIR)/queen-server --port 6633 &
	@SERVER_PID=$$!; \
	sleep 2; \
	echo "Running Node.js test suite against C++ server..."; \
	cd .. && QUEEN_TEST_PORT=6633 node src/test/test-new.js; \
	TEST_RESULT=$$?; \
	kill $$SERVER_PID 2>/dev/null || true; \
	exit $$TEST_RESULT

# Clean build artifacts
clean:
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Clean complete!"

# Clean everything including dependencies
distclean: clean
	@rm -rf $(VENDOR_DIR)
	@echo "Full clean complete!"

# Development helpers
dev: $(TARGET)
	@echo "Starting development server..."
	@./$(BIN_DIR)/queen-server --port 6632 --dev

install-deps-ubuntu:
	@sudo apt-get update
	@sudo apt-get install -y build-essential libpq-dev libssl-dev zlib1g-dev curl unzip

install-deps-macos:
	@brew install postgresql openssl curl unzip
	@echo "Note: You may need to set PKG_CONFIG_PATH for OpenSSL"

debug-paths:
	@echo "=== Build Configuration Debug ==="
	@echo "Operating System: $(UNAME_S)"
ifeq ($(UNAME_S),Darwin)
	@echo "Homebrew prefix: $(BREW_PREFIX)"
	@echo "pg_config: $(PG_CONFIG)"
	@echo "PostgreSQL includes: $(PG_INCLUDES)"
	@echo "PostgreSQL libs: $(PG_LIBS)"
	@echo "OpenSSL includes: $(SSL_INCLUDES)"
	@echo "OpenSSL libs: $(SSL_LIBS)"
endif
	@echo "Final INCLUDES: $(INCLUDES)"
	@echo "Final LIBS: $(LIBS)"
	@echo "================================="

help:
	@echo "Queen C++ Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build everything (default)"
	@echo "  deps         - Download header-only dependencies"
	@echo "  test         - Run Node.js test suite against C++ server"
	@echo "  clean        - Remove build artifacts"
	@echo "  distclean    - Remove build artifacts and dependencies"
	@echo "  dev          - Start development server"
	@echo "  debug-paths  - Show detected include/library paths"
	@echo ""
	@echo "Dependencies:"
	@echo "  install-deps-ubuntu  - Install system dependencies on Ubuntu"
	@echo "  install-deps-macos   - Install system dependencies on macOS"
