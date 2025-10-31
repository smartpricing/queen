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
# Stage 1: Build Frontend
FROM node:22-alpine AS frontend-builder

WORKDIR /app/webapp

# Copy frontend package files
COPY webapp/package*.json ./

# Install dependencies
RUN npm ci

# Copy frontend source
COPY webapp/ ./

# Build frontend
RUN npm run build

# Stage 2: Build C++ Server
FROM ubuntu:24.04 as cpp-builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y build-essential libpq-dev libssl-dev zlib1g-dev curl unzip ca-certificates
COPY ./ /usr/build/ 

WORKDIR /usr/build/server

# Download dependencies FIRST (so wildcards can find the files)
RUN make deps

# Now build (wildcards will work because vendor/uSockets exists)
RUN make build-only

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

# Copy frontend build from builder
COPY --from=frontend-builder /app/webapp/dist ./webapp/dist

# Expose the server port
EXPOSE 6632

# Run the C++ server
CMD ["./bin/queen-server"]