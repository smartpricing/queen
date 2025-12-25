# Multi-stage Dockerfile for Queen Message Queue
#
# This Dockerfile builds the complete Queen Message Queue system:
# - C++ server with full C++17 support (using g++-12)
# - Vue.js frontend dashboard
# - Optimized runtime image (~250MB final size)
#
# Build: docker build -t queen-mq .
# Run:   docker run -p 6632:6632 -e PG_HOST=your-db queen-mq
#
# For full stack with PostgreSQL, use docker-compose.yml
#
# Dependency Caching:
# To avoid re-downloading dependencies, place zip files in deps-cache/:
#   - deps-cache/uws.zip
#   - deps-cache/usockets.zip
#   - deps-cache/spdlog.zip
#   - deps-cache/libuv.zip
#   - deps-cache/json.hpp
#   - deps-cache/httplib.h
#
# Stage 1: Build Frontend
FROM node:22-alpine AS frontend-builder

WORKDIR /app/webapp

# Copy frontend package files
COPY app/package*.json ./

# Install dependencies
RUN npm ci

# Copy frontend source
COPY app/ ./

# Build frontend
RUN npm run build

# Stage 2: Build C++ Server
FROM ubuntu:24.04 as cpp-builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y build-essential libpq-dev libssl-dev zlib1g-dev curl unzip ca-certificates cmake

COPY ./ /usr/build/

WORKDIR /usr/build/server

# Setup dependencies: use cached zips if available, otherwise run make deps
RUN if [ -f /usr/build/deps-cache/uws.zip ] && \
       [ -f /usr/build/deps-cache/usockets.zip ] && \
       [ -f /usr/build/deps-cache/spdlog.zip ] && \
       [ -f /usr/build/deps-cache/libuv.zip ] && \
       [ -f /usr/build/deps-cache/json.hpp ] && \
       [ -f /usr/build/deps-cache/httplib.h ]; then \
        echo "Using cached dependencies from deps-cache/"; \
        mkdir -p vendor; \
        cp /usr/build/deps-cache/json.hpp vendor/json.hpp; \
        cp /usr/build/deps-cache/httplib.h vendor/httplib.h; \
        cd vendor && \
        cp /usr/build/deps-cache/uws.zip . && unzip -qo uws.zip && rm -rf uWebSockets && mv uWebSockets-master uWebSockets && rm uws.zip && \
        cp /usr/build/deps-cache/usockets.zip . && unzip -qo usockets.zip && rm -rf uSockets && mv uSockets-master uSockets && rm usockets.zip && \
        cp /usr/build/deps-cache/spdlog.zip . && unzip -qo spdlog.zip && rm -rf spdlog && mv spdlog-1.14.1 spdlog && rm spdlog.zip && \
        cp /usr/build/deps-cache/libuv.zip . && unzip -qo libuv.zip && rm -rf libuv && mv libuv-1.48.0 libuv && rm libuv.zip && \
        echo "Building libuv..." && \
        cd libuv && mkdir -p build && cd build && \
        cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DLIBUV_BUILD_SHARED=OFF && \
        cmake --build . --target uv_a -j4 && \
        cd /usr/build/server && \
        touch vendor/.deps_ready && \
        echo "Dependencies ready (from cache)!"; \
    else \
        echo "No cache found, downloading dependencies..."; \
        make deps; \
    fi

# Now build (wildcards will work because vendor/uSockets exists)
RUN make build-only -j 8

# Verify
RUN test -f bin/queen-server && echo "✅ Build successful"

# Stage 3: Runtime Image
FROM ubuntu:24.04

# Install runtime dependencies only
RUN apt-get update && apt-get install -y \
    libpq5 \
    libssl3 \
    zlib1g \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy C++ server binary from builder
COPY --from=cpp-builder /usr/build/server/bin/queen-server ./bin/queen-server

# Copy schema and procedures for database initialization (from libqueen)
COPY --from=cpp-builder /usr/build/lib/schema ./schema

# Copy frontend build from builder
COPY --from=frontend-builder /app/webapp/dist ./webapp/dist

# Expose the server port
EXPOSE 6632

# Run the C++ server
CMD ["./bin/queen-server"]
