# Database Pool Health Test Makefile

CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -I../include -I../vendor -I../vendor/spdlog/include
LDFLAGS = -lpq -lpthread

# Detect OS
UNAME_S := $(shell uname -s)

# PostgreSQL library paths (adjust for your system)
ifeq ($(UNAME_S),Darwin)
    # macOS (Homebrew) - try to auto-detect or use common paths
    PG_INCLUDE := $(shell pg_config --includedir 2>/dev/null || echo "/opt/homebrew/include/postgresql@14")
    PG_LIB := $(shell pg_config --libdir 2>/dev/null || echo "/opt/homebrew/lib")
    CXXFLAGS += -I$(PG_INCLUDE)
    LDFLAGS += -L$(PG_LIB)
else ifeq ($(UNAME_S),Linux)
    # Linux
    PG_INCLUDE := $(shell pg_config --includedir 2>/dev/null || echo "/usr/include/postgresql")
    PG_LIB := $(shell pg_config --libdir 2>/dev/null || echo "/usr/lib")
    CXXFLAGS += -I$(PG_INCLUDE)
    LDFLAGS += -L$(PG_LIB)
endif

# Build objects
BUILD_DIR = ./build
OBJECTS = $(BUILD_DIR)/pool_health_test.o $(BUILD_DIR)/database.o

all: $(BUILD_DIR)/pool_health_test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/database.o: ../src/database/database.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/pool_health_test.o: pool_health_test.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/pool_health_test: $(OBJECTS)
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $@

test: $(BUILD_DIR)/pool_health_test
	@echo "Running pool health tests..."
	@./$(BUILD_DIR)/pool_health_test

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all test clean

