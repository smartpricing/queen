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

# Test executable
TEST_TARGET = $(BUILD_DIR)/test_queen

.PHONY: all test clean help

all: test

# Build and run test
test: $(TEST_TARGET)
	@echo "Running test..."
	@./$(TEST_TARGET)

# Compile test.cpp
$(TEST_TARGET): test.cpp queen.hpp
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling test.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) test.cpp -o $@ $(LIBS)
	@echo "✅ Build complete!"

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
	@echo "  all    - Build test executable (default)"
	@echo "  test   - Build and run test"
	@echo "  clean  - Remove build artifacts"
	@echo "  debug  - Show build configuration paths"
	@echo ""
	@echo "Dependencies (from ../server/vendor/):"
	@echo "  - libuv (static library)"
	@echo "  - json.hpp (nlohmann json)"
	@echo "  - spdlog (header-only)"
	@echo "  - libpq (PostgreSQL client, system install)"

