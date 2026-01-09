#!/bin/bash
# Download all dependencies for Docker build caching
# Run from the deps-cache directory

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "Downloading dependencies to cache..."

echo "  -> json.hpp"
curl -sL -o json.hpp "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp"

echo "  -> httplib.h"
curl -sL -o httplib.h "https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h"

echo "  -> uws.zip"
curl -sL -o uws.zip "https://github.com/uNetworking/uWebSockets/archive/refs/heads/master.zip"

echo "  -> usockets.zip"
curl -sL -o usockets.zip "https://github.com/uNetworking/uSockets/archive/refs/heads/master.zip"

echo "  -> spdlog.zip"
curl -sL -o spdlog.zip "https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.zip"

echo "  -> libuv.zip"
curl -sL -o libuv.zip "https://github.com/libuv/libuv/archive/refs/tags/v1.48.0.zip"

echo "  -> jwt-cpp.zip"
curl -sL -o jwt-cpp.zip "https://github.com/Thalhammer/jwt-cpp/archive/refs/tags/v0.7.0.zip"

echo ""
echo "✅ All dependencies cached!"
echo ""
ls -lh *.zip *.hpp *.h 2>/dev/null || true

