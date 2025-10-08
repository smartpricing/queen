import { createQueenClient } from '../src/client/index.js';

const client = createQueenClient({
    baseUrl: process.env.QUEEN_URL || 'http://localhost:6632',
    timeout: 10000  // 10 second timeout for operations
});

// V2: Using queue and partition structure
const queue = 'smartchat-agent';
const partition = 'id-1';  // Can use partition to separate different agents

async function consumer () {
  // Start consuming messages from the specific partition
  const stop = client.consume({
    queue: queue,
    partition: partition,  // Optional: remove to consume from any partition
    handler: async (message) => {
      console.log(`Consumed message from ${message.partition}: ${message.data.data}`);
    },
    options: {
      wait: true,
      timeout: 10000
    }
  });
  
  // Return stop function for cleanup if needed
  return stop;
}

let count = 0;
async function producer () {
  const messages = [{
    queue: queue,
    partition: partition,  // Optional: defaults to "Default"
    payload: {
      id: count,
      data: `Hello, world! ${count++}`,
      timestamp: new Date().toISOString()
    }
  }];

  const result = await client.push({
    items: messages
  });
  console.log(`Pushed ${result.messages.length} messages to ${queue}/${partition}`);
}

// Start consumer
consumer().then(stop => {
  // Handle graceful shutdown
  process.on('SIGINT', () => {
    console.log('\nStopping consumer...');
    stop();
    process.exit(0);
  });
});

// Start producer loop
(async () => {
  while (true) {
    await producer();
    await new Promise(resolve => setTimeout(resolve, 10000));
  }
})();