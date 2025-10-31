#!/bin/bash

# Test script for Phase 1 Streaming API
# This script starts the server and runs the streaming tests

set -e

echo "========================================"
echo "Phase 1 Streaming API - End-to-End Test"
echo "========================================"
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if PostgreSQL is running
echo -n "Checking PostgreSQL... "
if ! pg_isready -h localhost -p 5432 > /dev/null 2>&1; then
    echo -e "${RED}FAILED${NC}"
    echo "PostgreSQL is not running on localhost:5432"
    echo "Please start PostgreSQL first"
    exit 1
fi
echo -e "${GREEN}OK${NC}"

# Check if server binary exists
echo -n "Checking server binary... "
if [ ! -f "server/bin/queen-server" ]; then
    echo -e "${RED}FAILED${NC}"
    echo "Server binary not found. Building..."
    cd server
    make build-only
    cd ..
fi
echo -e "${GREEN}OK${NC}"

# Check if node_modules exists
echo -n "Checking client dependencies... "
if [ ! -d "client-js/node_modules" ]; then
    echo -e "${YELLOW}MISSING${NC}"
    echo "Installing client dependencies..."
    cd client-js
    npm install
    cd ..
fi
echo -e "${GREEN}OK${NC}"

# Start server in background
echo ""
echo "Starting Queen server..."
cd server
DB_POOL_SIZE=50 LOG_LEVEL=info ./bin/queen-server &
SERVER_PID=$!
cd ..

echo "Server PID: $SERVER_PID"
echo "Waiting for server to be ready..."

# Wait for server to be ready
sleep 3

# Check if server is running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}Server failed to start${NC}"
    exit 1
fi

echo -e "${GREEN}Server is running${NC}"
echo ""

# Run the example
echo "========================================"
echo "Running Example: 16-streaming-phase1.js"
echo "========================================"
echo ""

cd client-js
nvm use 22 && node ../examples/16-streaming-phase1.js
EXAMPLE_EXIT=$?
cd ..

echo ""
echo "========================================"

if [ $EXAMPLE_EXIT -eq 0 ]; then
    echo -e "${GREEN}✅ Example completed successfully!${NC}"
else
    echo -e "${RED}❌ Example failed with exit code $EXAMPLE_EXIT${NC}"
fi

echo "========================================"
echo ""

# Run the tests
echo "========================================"
echo "Running Tests: phase1_basic.js"
echo "========================================"
echo ""

cd client-js
nvm use 22 && node test-v2/streaming/phase1_basic.js
TEST_EXIT=$?
cd ..

echo ""
echo "========================================"

if [ $TEST_EXIT -eq 0 ]; then
    echo -e "${GREEN}✅ All tests passed!${NC}"
else
    echo -e "${RED}❌ Tests failed with exit code $TEST_EXIT${NC}"
fi

echo "========================================"
echo ""

# Cleanup
echo "Stopping server (PID: $SERVER_PID)..."
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo -e "${GREEN}Server stopped${NC}"
echo ""

# Final status
if [ $EXAMPLE_EXIT -eq 0 ] && [ $TEST_EXIT -eq 0 ]; then
    echo "========================================"
    echo -e "${GREEN}🎉 Phase 1 Streaming API: WORKING!${NC}"
    echo "========================================"
    exit 0
else
    echo "========================================"
    echo -e "${RED}❌ Some tests failed${NC}"
    echo "========================================"
    exit 1
fi

