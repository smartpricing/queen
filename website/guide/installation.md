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
  -e PG_PASSWORD=postgres \
  -e NUM_WORKERS=2 \
  -e DB_POOL_SIZE=5 \
  -e SIDECAR_POOL_SIZE=30 \
  -e SIDECAR_MICRO_BATCH_WAIT_MS=10 \
  -e POP_WAIT_INITIAL_INTERVAL_MS=500 \
  -e POP_WAIT_BACKOFF_THRESHOLD=1 \
  -e POP_WAIT_BACKOFF_MULTIPLIER=3.0 \
  -e POP_WAIT_MAX_INTERVAL_MS=5000 \
  -e DEFAULT_SUBSCRIPTION_MODE=new \
  -e LOG_LEVEL=info \
  smartnessai/queen-mq:{{VERSION}}
```

That's it! Queen is running at `http://localhost:6632`.

## Client Installation

### JavaScript/Node.js

```bash
npm install queen-mq
```

**Requirements:**
- Node.js 22+ (required for native fetch and modern JS features)
- npm

**Usage:**
```javascript
import { Queen } from 'queen-mq'
const queen = new Queen('http://localhost:6632')
```

### Python

```bash
pip install queen-mq
```

**Requirements:**
- Python 3.8+
- pip

**Usage:**
```python
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        # Your code here
        pass

asyncio.run(main())
```

### C++

See the [C++ Client Guide](/clients/cpp) for detailed installation instructions.

## Server Installation

### Option 1: Docker (Recommended)

**Pull the image:**
```bash
docker pull smartnessai/queen-mq:{{VERSION}}
```

**Run with custom configuration:**
```bash
docker run -p 6632:6632 \
  -e PG_HOST=your-postgres-host \
  -e PG_PORT=5432 \
  -e PG_USER=queen \
  -e PG_PASSWORD=your-password \
  -e PG_DB=queen \
  -e NUM_WORKERS=10 \
  -e DB_POOL_SIZE=50 \
  -e SIDECAR_POOL_SIZE=30 \
  -e SIDECAR_MICRO_BATCH_WAIT_MS=10 \
  -e POP_WAIT_INITIAL_INTERVAL_MS=500 \
  -e POP_WAIT_BACKOFF_THRESHOLD=1 \
  -e POP_WAIT_BACKOFF_MULTIPLIER=3.0 \
  -e POP_WAIT_MAX_INTERVAL_MS=5000 \
  -e DEFAULT_SUBSCRIPTION_MODE=new \
  -e LOG_LEVEL=info \
  -e PORT=6632 \
  smartnessai/queen-mq:{{VERSION}}
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

# Install
make

# Run
./bin/queen-server
```

See the [Server Installation Guide](/server/installation) for detailed build instructions.

## Web Dashboard

The dashboard is bundled with the Queen server. Access it at:

```
http://localhost:6632
```

No additional installation needed!

### Running Webapp Separately

If you want to run the webapp separately for development:

```bash
cd app
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