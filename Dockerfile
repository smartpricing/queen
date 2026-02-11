# Multi-stage Dockerfile for Queen Message Queue
#
# This Dockerfile builds the complete Queen Message Queue system:
# - C++ server with full C++17 support
# - Vue.js frontend dashboard
# - Optimized runtime image (~250MB final size)
#
# Build: docker build -t queen-mq .
# Run:   docker run -p 6632:6632 -e PG_HOST=your-db queen-mq
#
# For full stack with PostgreSQL, use docker-compose.yml
#
# Build optimizations:
# - Layered COPY: Makefile+deps cached separately from source code
# - Precompiled headers: spdlog + json.hpp parsed once, not 30+ times
# - ccache: compiler cache persisted across builds via BuildKit mount
# - Auto parallelism: uses $(nproc) instead of hardcoded -j value
#
# Requires BuildKit: DOCKER_BUILDKIT=1 docker build -t queen-mq .
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
FROM ubuntu:24.04 AS cpp-builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential libpq-dev libssl-dev zlib1g-dev \
    curl unzip ca-certificates cmake ccache \
    && rm -rf /var/lib/apt/lists/*

# Enable ccache - wraps g++/gcc transparently for compilation caching
ENV PATH="/usr/lib/ccache:${PATH}"

WORKDIR /usr/build/server

# Layer 1: Copy only the Makefile (changes rarely - only when dep URLs change)
# This layer caches the dependency download so source code changes don't re-download
COPY server/Makefile ./Makefile

# Download and build dependencies (cached unless Makefile changes)
RUN make deps

# Layer 2: Copy source code (changes frequently, only triggers recompile)
COPY server/src/ ./src/
COPY server/include/ ./include/
COPY lib/ /usr/build/lib/

# Build with ccache persistent cache and auto-detected parallelism
RUN --mount=type=cache,target=/root/.ccache \
    make -j$(nproc) build-only

# Verify
RUN test -f bin/queen-server && echo "Build successful"

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
