#!/bin/bash

# Queen C++ Build Script
# Builds the C++ implementation with all dependencies

set -e

echo "🚀 Building Queen C++ Message Queue Server..."

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if we're in the right directory
if [ ! -f "Makefile" ]; then
    print_error "Makefile not found. Please run this script from the src-cpp directory."
    exit 1
fi

# Check system dependencies
print_status "Checking system dependencies..."

check_command() {
    if ! command -v $1 &> /dev/null; then
        print_error "$1 is not installed"
        return 1
    else
        print_success "$1 found"
        return 0
    fi
}

# Check required commands
DEPS_OK=true
check_command "g++" || DEPS_OK=false
check_command "make" || DEPS_OK=false
check_command "curl" || DEPS_OK=false
check_command "unzip" || DEPS_OK=false

# Check PostgreSQL development headers
PG_FOUND=false
if pkg-config --exists libpq 2>/dev/null; then
    PG_FOUND=true
    print_success "PostgreSQL headers found (via pkg-config)"
else
    # Check common paths
    for path in "/usr/include" "/usr/local/include" "/opt/homebrew/include" "/usr/local/opt/postgresql/include"; do
        if [ -f "$path/libpq-fe.h" ]; then
            PG_FOUND=true
            print_success "PostgreSQL headers found at $path"
            break
        fi
    done
    
    # Check versioned PostgreSQL installations (e.g., postgresql@14)
    if [ "$PG_FOUND" = false ]; then
        for pg_dir in /opt/homebrew/include/postgresql@* /usr/local/opt/postgresql@*/include; do
            if [ -d "$pg_dir" ] && [ -f "$pg_dir/libpq-fe.h" ]; then
                PG_FOUND=true
                print_success "PostgreSQL headers found at $pg_dir"
                break
            fi
        done
    fi
fi

if [ "$PG_FOUND" = false ]; then
    print_error "PostgreSQL development headers not found"
    print_status "Install with: sudo apt-get install libpq-dev (Ubuntu) or brew install postgresql (macOS)"
    DEPS_OK=false
fi

# Check OpenSSL
SSL_FOUND=false
if pkg-config --exists openssl 2>/dev/null; then
    SSL_FOUND=true
    print_success "OpenSSL headers found (via pkg-config)"
else
    # Check common paths
    for path in "/usr/include" "/usr/local/include" "/opt/homebrew/include" "/usr/local/opt/openssl/include" "/usr/local/opt/openssl@*/include"; do
        if [ -f "$path/openssl/ssl.h" ]; then
            SSL_FOUND=true
            print_success "OpenSSL headers found at $path"
            break
        fi
    done
fi

if [ "$SSL_FOUND" = false ]; then
    print_error "OpenSSL development headers not found"
    print_status "Install with: sudo apt-get install libssl-dev (Ubuntu) or brew install openssl (macOS)"
    DEPS_OK=false
fi

if [ "$DEPS_OK" = false ]; then
    print_error "Missing dependencies. Please install them and try again."
    exit 1
fi

# Parse command line arguments
CLEAN=false
DEPS_ONLY=false
VERBOSE=false
DEBUG=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN=true
            shift
            ;;
        --deps-only)
            DEPS_ONLY=true
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        --debug)
            DEBUG=true
            shift
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  --clean      Clean before building"
            echo "  --deps-only  Only download dependencies"
            echo "  --verbose    Verbose output"
            echo "  --debug      Build with debug symbols"
            echo "  --help       Show this help"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Clean if requested
if [ "$CLEAN" = true ]; then
    print_status "Cleaning previous build..."
    make clean
    print_success "Clean complete"
fi

# Set build flags
export MAKEFLAGS=""
if [ "$VERBOSE" = true ]; then
    export MAKEFLAGS="$MAKEFLAGS V=1"
fi

if [ "$DEBUG" = true ]; then
    export CXXFLAGS="-g -O0 -DDEBUG"
    print_status "Building in debug mode"
else
    export CXXFLAGS="-O3 -DNDEBUG"
    print_status "Building in release mode"
fi

# Download dependencies
print_status "Setting up dependencies..."
make deps
print_success "Dependencies ready"

if [ "$DEPS_ONLY" = true ]; then
    print_success "Dependencies downloaded successfully"
    exit 0
fi

# Build the project
print_status "Compiling Queen C++ server..."
if make; then
    print_success "Build completed successfully!"
else
    print_error "Build failed"
    exit 1
fi

# Check if binary was created
if [ -f "bin/queen-server" ]; then
    print_success "Binary created: bin/queen-server"
    
    # Show binary info
    SIZE=$(du -h bin/queen-server | cut -f1)
    print_status "Binary size: $SIZE"
    
    # Test basic functionality
    print_status "Testing binary..."
    if ./bin/queen-server --help > /dev/null 2>&1; then
        print_success "Binary is functional"
    else
        print_warning "Binary may have issues (help command failed)"
    fi
else
    print_error "Binary not found after build"
    exit 1
fi

echo ""
print_success "🎉 Queen C++ server built successfully!"
echo ""
print_status "Next steps:"
echo "  1. Set up your PostgreSQL database"
echo "  2. Configure environment variables (see README.md)"
echo "  3. Run: ./bin/queen-server"
echo "  4. Test: make test"
echo ""
print_status "For help: ./bin/queen-server --help"
