# libqueen - Header-only Queen core library
# Requires C++17 or later

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread

# Vendor directory (relative to lib/)
VENDOR_DIR = ../server/vendor

# libuv (from vendor - built as static library)
UV_INCLUDES = $(VENDOR_DIR)/libuv/include
UV_STATIC_LIB = $(VENDOR_DIR)/libuv/build/libuv.a

# spdlog (header-only from vendor)
SPDLOG_INCLUDES = $(VENDOR_DIR)/spdlog/include

# json.hpp (single header in vendor root)
JSON_INCLUDES = $(VENDOR_DIR)

# Platform-specific configuration
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS - detect Homebrew paths for PostgreSQL
    BREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo /usr/local)
    PG_CONFIG := $(shell which pg_config 2>/dev/null || echo "")
    ifneq ($(PG_CONFIG),)
        PG_INCLUDES := $(shell $(PG_CONFIG) --includedir)
        PG_LIBS := $(shell $(PG_CONFIG) --libdir)
    else
        PG_INCLUDES := $(BREW_PREFIX)/include/postgresql $(BREW_PREFIX)/opt/postgresql/include
        PG_LIBS := $(BREW_PREFIX)/lib $(BREW_PREFIX)/opt/postgresql/lib
    endif
else
    # Linux
    PG_INCLUDES := /usr/include/postgresql /usr/local/include/postgresql
    PG_LIBS := /usr/lib /usr/local/lib
endif

# Combine all includes
INCLUDES = -I. \
           -I$(UV_INCLUDES) \
           -I$(SPDLOG_INCLUDES) \
           -I$(JSON_INCLUDES) \
           $(addprefix -I,$(PG_INCLUDES))

# Libraries to link
LIBS = $(UV_STATIC_LIB) -lpq $(addprefix -L,$(PG_LIBS))

# Build directory
BUILD_DIR = build

# Test executables
TEST_TARGET = $(BUILD_DIR)/test_queen
TEST_SUITE_TARGET = $(BUILD_DIR)/test_suite
TEST_CONTENTION_TARGET = $(BUILD_DIR)/test_contention

.PHONY: all test test-suite test-contention clean help

all: test

# Build and run basic test
test: $(TEST_TARGET)
	@echo "Running basic test..."
	@./$(TEST_TARGET)

# Build and run comprehensive test suite
test-suite: $(TEST_SUITE_TARGET)
	@echo "Running comprehensive test suite..."
	@./$(TEST_SUITE_TARGET)

# Compile test.cpp (basic test)
$(TEST_TARGET): test.cpp queen.hpp
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling test.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) test.cpp -o $@ $(LIBS)
	@echo "✅ Build complete!"

# Compile test_suite.cpp (comprehensive test suite)
$(TEST_SUITE_TARGET): test_suite.cpp queen.hpp
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling test_suite.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_suite.cpp -o $@ $(LIBS)
	@echo "✅ Test suite build complete!"

# Build and run contention benchmark
test-contention: $(TEST_CONTENTION_TARGET)
	@echo "Running PUSH/POP contention benchmark..."
	@./$(TEST_CONTENTION_TARGET)

# Compile test_contention.cpp (contention benchmark)
$(TEST_CONTENTION_TARGET): test_contention.cpp queen.hpp
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling test_contention.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_contention.cpp -o $@ $(LIBS)
	@echo "✅ Contention benchmark build complete!"

# Clean build artifacts
clean:
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete!"

# Debug: show paths
debug:
	@echo "=== libqueen Build Configuration ==="
	@echo "OS: $(UNAME_S)"
	@echo "Vendor dir: $(VENDOR_DIR)"
	@echo "libuv includes: $(UV_INCLUDES)"
	@echo "libuv static lib: $(UV_STATIC_LIB)"
	@echo "spdlog includes: $(SPDLOG_INCLUDES)"
	@echo "json includes: $(JSON_INCLUDES)"
	@echo "PostgreSQL includes: $(PG_INCLUDES)"
	@echo "PostgreSQL libs: $(PG_LIBS)"
	@echo "Full INCLUDES: $(INCLUDES)"
	@echo "Full LIBS: $(LIBS)"
	@echo "===================================="

help:
	@echo "libqueen - Header-only Queen core library"
	@echo ""
	@echo "Targets:"
	@echo "  all            - Build basic test executable (default)"
	@echo "  test           - Build and run basic test"
	@echo "  test-suite     - Build and run comprehensive test suite"
	@echo "  test-contention - Build and run PUSH/POP contention benchmark"
	@echo "  clean          - Remove build artifacts"
	@echo "  debug          - Show build configuration paths"
	@echo ""
	@echo "Test Suite:"
	@echo "  The comprehensive test suite (test_suite.cpp) tests:"
	@echo "  - PUSH: single, batch, duplicate detection, partitions, large payloads"
	@echo "  - POP: empty queue, non-empty, batch, partitions, consumer groups"
	@echo "  - ACK: single, batch, success/failed status"
	@echo "  - TRANSACTIONS: push+ack atomic, multiple ops, partitions"
	@echo "  - RENEW_LEASE: manual, batch, expired lease, multiple renewals"
	@echo "  - MESSAGE ORDERING: FIFO ordering verification"
	@echo ""
	@echo "Contention Benchmark:"
	@echo "  The contention benchmark (test_contention.cpp) measures:"
	@echo "  - PUSH-only throughput (baseline)"
	@echo "  - POP-only throughput (baseline)"
	@echo "  - Mixed PUSH+POP throughput (shows contention)"
	@echo "  - Wildcard POP + PUSH (worst case partition_lookup contention)"
	@echo ""
	@echo "Dependencies (from ../server/vendor/):"
	@echo "  - libuv (static library)"
	@echo "  - json.hpp (nlohmann json)"
	@echo "  - spdlog (header-only)"
	@echo "  - libpq (PostgreSQL client, system install)"

