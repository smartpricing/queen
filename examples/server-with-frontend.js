/**
 * Example: Starting Queen Server with Built-in Frontend Dashboard
 * 
 * This example shows how to run Queen with the frontend dashboard
 * served from the same server (single deployment unit).
 * 
 * Prerequisites:
 * - Build the frontend first: cd webapp && npm run build
 */

import { QueenServer } from '../src/server.js';

console.log('Starting Queen Server with Frontend Dashboard...\n');

const server = await QueenServer({
  port: 3000,
  host: '127.0.0.1',
  serveWebapp: true  // 👈 Enable frontend serving
});

console.log('\n✅ Server started successfully!');
console.log(`
📊 Access Points:
   - Frontend Dashboard: http://${server.host}:${server.port}/
   - API Endpoint:      http://${server.host}:${server.port}/api/v1/
   - Health Check:      http://${server.host}:${server.port}/health
   - Metrics:           http://${server.host}:${server.port}/metrics

🎯 Single Port Deployment:
   Everything (frontend + API) served from port ${server.port}!
   No CORS issues, no separate static file server needed.

📝 To test:
   1. Open http://${server.host}:${server.port}/ in your browser
   2. You should see the Queen Dashboard UI
   3. All API calls will work from the same origin

Press Ctrl+C to stop.
`);

// Graceful shutdown
process.on('SIGINT', async () => {
  console.log('\n\nShutting down...');
  await server.shutdown('SIGINT');
});

