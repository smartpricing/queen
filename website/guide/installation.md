# Installation

Get Queen MQ up and running on your system. Choose the installation method that works best for you.

## Quick Start with Docker

The fastest way to get started:

```bash
# Create Docker network
docker network create queen

# Start PostgreSQL
docker run --name postgres --network queen \
  -e POSTGRES_PASSWORD=postgres \
  -p 5432:5432 \
  -d postgres

# Start Queen Server
docker run -p 6632:6632 --network queen \
  -e PG_HOST=postgres \
  -e PG_USER=postgres \
  -e PG_PASSWORD=postgres \
  -e PG_DB=postgres \
  -e DB_POOL_SIZE=20 \
  -e NUM_WORKERS=2 \
  smartnessai/queen-mq:0.5.0
```

That's it! Queen is running at `http://localhost:6632`.

## Client Installation

### JavaScript/Node.js

```bash
npm install queen-mq
```

**Requirements:**
- Node.js 22+ (required for native fetch and modern JS features)
- npm or yarn

**Usage:**
```javascript
import { Queen } from 'queen-mq'
const queen = new Queen('http://localhost:6632')
```

### C++

See the [C++ Client Guide](/clients/cpp) for detailed installation instructions.

## Server Installation

### Option 1: Docker (Recommended)

**Pull the image:**
```bash
docker pull smartnessai/queen-mq:0.5.0
```

**Run with custom configuration:**
```bash
docker run -p 6632:6632 \
  -e PG_HOST=your-postgres-host \
  -e PG_PORT=5432 \
  -e PG_USER=queen \
  -e PG_PASSWORD=your-password \
  -e PG_DB=queen \
  -e DB_POOL_SIZE=50 \
  -e NUM_WORKERS=10 \
  -e PORT=6632 \
  smartnessai/queen-mq:0.5.0
```

### Option 2: Build from Source

**Prerequisites:**
- C++17 compiler (GCC 9+, Clang 10+)
- PostgreSQL 13+ with libpq-dev
- CMake or Make
- Git

**Clone and build:**
```bash
# Clone the repository
git clone https://github.com/smartpricing/queen.git
cd queen/server

# Install dependencies
make deps

# Build
make build-only

# Run
./bin/queen-server
```

**With optimizations:**
```bash
# Build with release optimizations
make clean
make build-only OPTIMIZATION_FLAGS="-O3 -march=native"

# Run
DB_POOL_SIZE=50 NUM_WORKERS=10 ./bin/queen-server
```

See the [Server Installation Guide](/server/installation) for detailed build instructions.

### Option 3: Docker Compose

Create `docker-compose.yml`:

```yaml
version: '3.8'

services:
  postgres:
    image: postgres:15
    environment:
      POSTGRES_DB: queen
      POSTGRES_USER: queen
      POSTGRES_PASSWORD: queen_password
    ports:
      - "5432:5432"
    volumes:
      - postgres_data:/var/lib/postgresql/data
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U queen"]
      interval: 5s
      timeout: 5s
      retries: 5

  queen:
    image: smartnessai/queen-mq:0.5.0
    depends_on:
      postgres:
        condition: service_healthy
    ports:
      - "6632:6632"
    environment:
      PG_HOST: postgres
      PG_PORT: 5432
      PG_USER: queen
      PG_PASSWORD: queen_password
      PG_DB: queen
      DB_POOL_SIZE: 50
      NUM_WORKERS: 10
      PORT: 6632
    restart: unless-stopped

volumes:
  postgres_data:
```

**Start everything:**
```bash
docker-compose up -d
```

## PostgreSQL Setup

Queen requires PostgreSQL 13 or higher.

### Option 1: Use Existing PostgreSQL

Connect Queen to your existing PostgreSQL instance:

```bash
docker run -p 6632:6632 \
  -e PG_HOST=your-postgres-host \
  -e PG_PORT=5432 \
  -e PG_USER=queen \
  -e PG_PASSWORD=your-password \
  -e PG_DB=queen \
  smartnessai/queen-mq:0.5.0
```

Queen will automatically create the required tables on first run.

### Option 2: Docker PostgreSQL

```bash
docker run --name queen-postgres \
  -e POSTGRES_DB=queen \
  -e POSTGRES_USER=queen \
  -e POSTGRES_PASSWORD=queen_password \
  -p 5432:5432 \
  -v queen_data:/var/lib/postgresql/data \
  -d postgres:15
```

### Database Setup

Queen automatically creates all required tables and indexes on first startup. No manual SQL execution needed!

**Tables created:**
- `messages` - Message storage
- `partitions` - Partition metadata
- `consumer_groups` - Consumer group tracking
- `dead_letter_queue` - Failed messages
- `traces` - Message tracing data

## Web Dashboard

The dashboard is bundled with the Queen server. Access it at:

```
http://localhost:6632
```

No additional installation needed! 🎉

### Running Webapp Separately (Optional)

If you want to run the webapp separately for development:

```bash
cd webapp
npm install
npm run dev
```

Then configure it to point to your Queen server.

See [Webapp Setup](/webapp/setup) for more details.

## Proxy Server (Optional)

For production deployments with authentication and SSL:

```bash
cd proxy
npm install

# Configure environment
cp .env.example .env
# Edit .env with your settings

# Run
npm start
```

See [Proxy Setup](/proxy/setup) for complete guide.

## Kubernetes Deployment

Deploy Queen on Kubernetes with the included Helm charts:

```bash
cd helm

# Install
helm install queen . \
  --set postgres.host=your-postgres-host \
  --set postgres.password=your-password \
  --set replicaCount=3 \
  --set resources.poolSize=50

# Or use prod.yaml
helm install queen . -f prod.yaml
```

See [Deployment Guide](/server/deployment) for Kubernetes best practices.

## Environment Variables

### Essential Variables

```bash
# PostgreSQL Connection
PG_HOST=localhost
PG_PORT=5432
PG_USER=queen
PG_PASSWORD=your-password
PG_DB=queen

# Server Configuration
PORT=6632
NUM_WORKERS=10
DB_POOL_SIZE=50
```

### Optional Variables

```bash
# Performance Tuning
NUM_BACKGROUND_THREADS=8
NUM_POLL_WORKERS=4
ASYNC_DB_CONNECTIONS=142

# Failover
FILE_BUFFER_DIR=/var/lib/queen/buffers

# Security
ENABLE_CORS=true
LOG_LEVEL=info

# Features
ENABLE_METRICS=true
ENABLE_TRACING=true
```

See [Configuration Guide](/server/configuration) for complete variable reference.

## Verify Installation

### Check Server Health

```bash
curl http://localhost:6632/health
```

**Expected response:**
```json
{
  "status": "healthy",
  "database": "connected",
  "server": "C++ Queen Server (Acceptor/Worker)",
  "version": "1.0.0"
}
```

### Test with Client

```javascript
import { Queen } from 'queen-mq'

const queen = new Queen('http://localhost:6632')

// Create a test queue
await queen.queue('test').create()

// Push a message
await queen.queue('test').push([{ data: { test: true } }])

// Pop the message
const messages = await queen.queue('test').pop()

console.log('Success!', messages)
```

### Check Dashboard

Open `http://localhost:6632` in your browser. You should see the Queen dashboard.

## Troubleshooting

### Server Won't Start

**Check PostgreSQL connection:**
```bash
# Test connection
psql -h localhost -U queen -d queen

# Check Docker logs
docker logs <queen-container-id>
```

**Common issues:**
- PostgreSQL not running
- Wrong credentials
- Network connectivity
- Port already in use

### Client Can't Connect

**Check server is running:**
```bash
curl http://localhost:6632/health
```

**Check firewall:**
```bash
# Allow port 6632
sudo ufw allow 6632
```

**Check network (Docker):**
```bash
# Make sure containers are on same network
docker network inspect queen
```

### Build Errors (Source Installation)

**Missing dependencies:**
```bash
# Ubuntu/Debian
sudo apt-get install -y build-essential libpq-dev postgresql-client

# macOS
brew install postgresql
```

**Compiler too old:**
```bash
# Need GCC 9+ or Clang 10+
gcc --version
clang --version

# Update if needed
sudo apt-get install g++-10
```

## Next Steps

- [Quick Start Guide](/guide/quickstart) - Build your first application
- [Configuration](/server/configuration) - Tune for your workload
- [Performance Tuning](/server/tuning) - Optimize for production
- [Deployment](/server/deployment) - Production deployment patterns

## Support

Need help?
- 📚 [Documentation](/) - Complete guides
- 💬 [GitHub Issues](https://github.com/smartpricing/queen/issues) - Report bugs
- 🏢 [LinkedIn](https://www.linkedin.com/company/smartness-com/) - Connect with us

## Version Information

**Current Version:** 0.5.0 (Production Ready)

**Compatibility:**
- PostgreSQL 13+
- Node.js 22+
- C++17 or higher
- Docker 20.10+

**Changelog:** See [GitHub Releases](https://github.com/smartpricing/queen/releases)

