# Queen Benchmark Tool - Makefile

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -pthread -march=native

# Include paths
INCLUDES = -I. \
           -I../client-cpp \
           -I../server/vendor \
           -I../server/include

# Check for cpp-httplib installation
HTTPLIB_PATHS = /opt/homebrew/include \
                /usr/local/include \
                /usr/include

HTTPLIB_FOUND = $(foreach path,$(HTTPLIB_PATHS),$(wildcard $(path)/httplib.h))

ifneq ($(HTTPLIB_FOUND),)
    INCLUDES += -I$(dir $(firstword $(HTTPLIB_FOUND)))
    $(info Found cpp-httplib at: $(dir $(firstword $(HTTPLIB_FOUND))))
else
    $(warning cpp-httplib not found in system paths)
endif

# Libraries
LIBS = -lpthread

# SSL support
USE_SSL ?= 1
ifeq ($(USE_SSL),1)
    CXXFLAGS += -DCPPHTTPLIB_OPENSSL_SUPPORT
    
    OPENSSL_PREFIX := $(shell brew --prefix openssl 2>/dev/null)
    ifneq ($(OPENSSL_PREFIX),)
        INCLUDES += -I$(OPENSSL_PREFIX)/include
        LIBS += -L$(OPENSSL_PREFIX)/lib -lssl -lcrypto
        $(info SSL support enabled with OpenSSL at: $(OPENSSL_PREFIX))
    else
        LIBS += -lssl -lcrypto
        $(info SSL support enabled (using system OpenSSL))
    endif
endif

# Targets
.PHONY: all clean help

all: bin/benchmark

bin/benchmark: queen_benchmark.cpp ../client-cpp/queen_client.hpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(INCLUDES) queen_benchmark.cpp -o bin/benchmark $(LIBS)
	@echo ""
	@echo "✓ Benchmark built successfully!"
	@echo ""
	@echo "Run with:"
	@echo "  ./bin/benchmark producer --threads 4 --count 100000 --batch 500 --partitions 4"
	@echo "  ./bin/benchmark consumer --threads 4 --batch 100 --partitions 4"

clean:
	rm -rf bin
	rm -f *.o
	@echo "Cleaned build artifacts"

help:
	@echo "Queen Benchmark Tool - Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  make          - Build benchmark tool (default)"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make help     - Show this help"
	@echo ""
	@echo "Usage after building:"
	@echo "  ./bin/benchmark <mode> [options]"
	@echo ""
	@echo "Examples:"
	@echo "  # Producer with single queue, 4 partitions"
	@echo "  ./bin/benchmark producer --threads 4 --count 100000 --batch 500 --partitions 4 --mode single-queue"
	@echo ""
	@echo "  # Consumer with multiple queues"
	@echo "  ./bin/benchmark consumer --threads 8 --batch 100 --partitions 8 --mode multi-queue"

