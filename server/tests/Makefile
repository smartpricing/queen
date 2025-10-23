# Database Pool Health Test Makefile

CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -I../include -I../vendor -I../vendor/spdlog/include
LDFLAGS = -lpq -lpthread

# Detect OS
UNAME_S := $(shell uname -s)

# PostgreSQL library paths (adjust for your system)
ifeq ($(UNAME_S),Darwin)
    # macOS (Homebrew) - try to auto-detect or use common paths
    BREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
    PG_INCLUDE := $(shell pg_config --includedir 2>/dev/null || echo "$(BREW_PREFIX)/include/postgresql@14")
    PG_LIB := $(shell pg_config --libdir 2>/dev/null || echo "$(BREW_PREFIX)/lib")
    SSL_LIB := $(BREW_PREFIX)/opt/openssl@3/lib
    CXXFLAGS += -I$(PG_INCLUDE)
    LDFLAGS += -L$(PG_LIB) -L$(SSL_LIB)
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

# Async PostgreSQL + uWebSockets test
async-test: $(BUILD_DIR)/async_pg_test
	@echo "Running async PostgreSQL test..."
	@./$(BUILD_DIR)/async_pg_test

$(BUILD_DIR)/async_pg_test: async_pg_test.cpp | $(BUILD_DIR)
	@echo "Compiling async_pg_test..."
	$(CXX) $(CXXFLAGS) -DWITH_OPENSSL=1 -DLIBUS_USE_OPENSSL \
		-I../vendor/uWebSockets/src -I../vendor/uSockets/src \
		async_pg_test.cpp \
		../build/usockets/bsd.o \
		../build/usockets/context.o \
		../build/usockets/loop.o \
		../build/usockets/socket.o \
		../build/usockets/eventing/epoll_kqueue.o \
		../build/usockets/crypto/openssl.o \
		../build/usockets/crypto/sni_tree.o \
		$(LDFLAGS) -lssl -lcrypto -lz \
		-o $@

.PHONY: all test clean async-test

