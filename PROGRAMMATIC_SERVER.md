# Queen MQ - Programmatic Server API

As of version 0.1.2, Queen MQ can be run programmatically from your Node.js code, in addition to running it as a standalone process.

## Installation

```bash
npm install queen-mq
```

## Basic Usage

### Starting the Server Programmatically

```javascript
import { QueenServer } from 'queen-mq';

// Start server with default configuration
const server = await QueenServer();

// Or with custom options
const server = await QueenServer({
  port: 3000,
  host: '127.0.0.1',
  workerId: 'worker-1'
});

console.log(`Server running at http://${server.host}:${server.port}`);
```

### Using Client and Server Together

```javascript
import { QueenServer, Queen } from 'queen-mq';

// Start the server
const server = await QueenServer({ port: 3000 });

// Create a client
const client = new Queen({
  servers: [`http://localhost:${server.port}`]
});

// Use the queue
await client.push({
  queue: 'my-queue',
  items: [{ data: { message: 'Hello World' } }]
});

const result = await client.pop({ queue: 'my-queue' });
console.log(result.messages);
```

### Graceful Shutdown

```javascript
const server = await QueenServer();

// Shutdown programmatically
await server.shutdown('SIGTERM');

// Or handle process signals
process.on('SIGINT', async () => {
  await server.shutdown('SIGINT');
});
```

## Server Options

The `QueenServer()` function accepts an options object:

```javascript
{
  port: number,        // Server port (default: from config, typically 3000)
  host: string,        // Server host (default: from config, typically '127.0.0.1')
  workerId: string     // Worker ID for clustering (default: from config)
}
```

## Return Value

The `QueenServer()` function returns a promise that resolves to a server instance with:

```javascript
{
  port: number,           // The port the server is listening on
  host: string,           // The host the server is bound to
  shutdown: Function,     // Async function to gracefully shutdown the server
  app: Object            // The underlying uWebSockets.js app instance
}
```

## Use Cases

### 1. Embedded Message Queue

Run Queen MQ embedded in your application instead of as a separate service:

```javascript
import { QueenServer, Queen } from 'queen-mq';
import express from 'express';

// Start Queen server
await QueenServer({ port: 3001 });

// Start your application server
const app = express();
const client = new Queen({ servers: ['http://localhost:3001'] });

app.post('/api/task', async (req, res) => {
  await client.push({
    queue: 'tasks',
    items: [{ data: req.body }]
  });
  res.json({ status: 'queued' });
});

app.listen(3000);
```

### 2. Testing

Easily spin up and tear down Queen servers in tests:

```javascript
import { QueenServer, Queen } from 'queen-mq';
import { describe, it, beforeAll, afterAll } from 'jest';

describe('Queue Tests', () => {
  let server, client;
  
  beforeAll(async () => {
    server = await QueenServer({ port: 4000 });
    client = new Queen({ servers: ['http://localhost:4000'] });
  });
  
  afterAll(async () => {
    await server.shutdown('TEST');
  });
  
  it('should process messages', async () => {
    await client.push({
      queue: 'test-queue',
      items: [{ data: { test: true } }]
    });
    
    const result = await client.pop({ queue: 'test-queue' });
    expect(result.messages).toHaveLength(1);
  });
});
```

### 3. Multiple Servers

Run multiple Queen instances in the same process:

```javascript
import { QueenServer } from 'queen-mq';

// Start multiple servers for different purposes
const server1 = await QueenServer({ port: 3001, workerId: 'worker-1' });
const server2 = await QueenServer({ port: 3002, workerId: 'worker-2' });

console.log('Multi-server setup running');
```

## Traditional Usage

You can still run Queen as a standalone server:

```bash
npm start
# or
node src/server.js
```

When `src/server.js` is run directly (not imported), it automatically starts the server with configuration from `src/config.js`.

## Requirements

- Node.js ≥ 22.0.0
- PostgreSQL database (configured via environment variables or config.js)
- Database must be initialized first (run `npm run init`)

## See Also

- [Main README](./README.md) - General Queen MQ documentation
- [API Documentation](./API.md) - HTTP API reference
- [Examples](./examples/) - More usage examples

