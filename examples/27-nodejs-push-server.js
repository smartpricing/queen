/**
 * Minimal performant Node.js HTTP server to benchmark PUSH performance
 * Same interface as Queen server - calls the same SQL stored procedure
 * 
 * Environment variables (same as C++ server):
 *   NODE_PORT  - HTTP port (default: 6632)
 *   PG_USER    - PostgreSQL user (default: postgres)
 *   PG_HOST    - PostgreSQL host (default: localhost)
 *   PG_DB      - PostgreSQL database (default: postgres)
 *   PG_PASSWORD- PostgreSQL password (default: postgres)
 *   PG_PORT    - PostgreSQL port (default: 5432)
 * 
 * Usage:
 *   cd examples && npm install
 *   node 27-nodejs-push-server.js
 * 
 * Then run benchmark against it:
 *   SERVER_URL=http://localhost:6632 node 26-load.js
 */

import { createServer } from 'node:http';
import pg from 'pg';

const { Pool } = pg;

// Configuration - same defaults as C++ server (config.hpp)
const PORT = parseInt(process.env.NODE_PORT || '6632');

// Database config - same env vars and defaults as C++ server
const DB_CONFIG = {
  user: process.env.PG_USER || 'postgres',
  host: process.env.PG_HOST || 'localhost',
  database: process.env.PG_DB || 'postgres',
  password: process.env.PG_PASSWORD || 'postgres',
  port: parseInt(process.env.PG_PORT || '5432'),
};

// Connection pool - optimized for high concurrency
// Note: C++ server uses 150 pool size by default
const pool = new Pool({
  ...DB_CONFIG,
  max: 150,                    // Match C++ server pool size
  idleTimeoutMillis: 30000,
  connectionTimeoutMillis: 30000,  // Longer timeout under load
  allowExitOnIdle: false,
});

// Pre-prepare the push statement for better performance
const PUSH_SQL = 'SELECT queen.push_messages_v2($1::jsonb, $2::boolean, $3::boolean)';

// Simple JSON body parser for known content-type
function parseBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on('data', chunk => chunks.push(chunk));
    req.on('end', () => {
      try {
        const body = JSON.parse(Buffer.concat(chunks).toString());
        resolve(body);
      } catch (e) {
        reject(new Error('Invalid JSON'));
      }
    });
    req.on('error', reject);
  });
}

// Request handler
async function handleRequest(req, res) {
  // Only handle POST /api/v1/push
  if (req.method !== 'POST' || req.url !== '/api/v1/push') {
    res.writeHead(404, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ error: 'Not found' }));
    return;
  }


  try {
    // Parse request body
    const body = await parseBody(req);
    
    if (!body.items || !Array.isArray(body.items)) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ error: 'items array is required' }));
      return;
    }

    // Transform items to match stored procedure format
    const itemsJson = body.items.map((item, idx) => ({
      queue: item.queue,
      partition: item.partition || 'Default',
      payload: item.payload || {},
      transactionId: item.transactionId,
      traceId: item.traceId,
    }));

    // Call stored procedure
    const result = await pool.query(PUSH_SQL, [
      JSON.stringify(itemsJson),
      true,   // check_duplicates
      false   // check_capacity (ignored)
    ]);

    console.log(JSON.stringify(result.rows[0].push_messages_v2))

    // Return results
    const results = result.rows[0].push_messages_v2;
    res.writeHead(201, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(results));

  } catch (err) {
    // Only log non-timeout errors to avoid spam
    if (!err.message.includes('timeout')) {
      console.error('Error:', err.message);
    }
    res.writeHead(503, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ error: err.message }));
  }
}

// Create HTTP server
const server = createServer(handleRequest);

// Handle graceful shutdown
process.on('SIGTERM', async () => {
  console.log('\nShutting down...');
  server.close();
  await pool.end();
  process.exit(0);
});

process.on('SIGINT', async () => {
  console.log('\nShutting down...');
  server.close();
  await pool.end();
  process.exit(0);
});

// Start server
server.listen(PORT, () => {
  console.log(`Node.js PUSH server listening on port ${PORT}`);
  console.log(`Database: ${DB_CONFIG.user}@${DB_CONFIG.host}:${DB_CONFIG.port}/${DB_CONFIG.database}`);
  console.log(`Pool size: ${pool.options.max} connections`);
  console.log('\nBenchmark with:');
  console.log(`  SERVER_URL=http://localhost:${PORT} node examples/26-load.js`);
});

