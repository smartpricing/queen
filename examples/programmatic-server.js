/**
 * Example: Starting Queen Server Programmatically
 * 
 * This example shows how to import and run the Queen server
 * directly from your code, rather than running it as a separate process.
 */

import { QueenServer, Queen } from 'queen-mq';

// Start the server programmatically
console.log('Starting Queen Server...');

const server = await QueenServer({
  port: 3000,
  host: '127.0.0.1'
});

console.log(`✅ Server started at http://${server.host}:${server.port}`);

// Now you can use the client in the same process
const client = new Queen({
  servers: [`http://${server.host}:${server.port}`]
});

// Create a queue
await client.push({
  queue: 'example-queue',
  items: [
    { data: { message: 'Hello from programmatic server!' } }
  ]
});

console.log('✅ Message pushed to queue');

// Pop a message
const result = await client.pop({
  queue: 'example-queue'
});

console.log('✅ Message received:', result.messages[0]?.data);

// Acknowledge the message
if (result.messages.length > 0) {
  await client.ack({
    transactionId: result.messages[0].transactionId,
    status: 'completed'
  });
  console.log('✅ Message acknowledged');
}

// Graceful shutdown on SIGINT
process.on('SIGINT', async () => {
  console.log('\nShutting down...');
  await server.shutdown('SIGINT');
});

console.log('\n📝 Server is running. Press Ctrl+C to stop.');

