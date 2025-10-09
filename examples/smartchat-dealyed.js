// Create client
import fs from 'fs';
import { createQueenClient } from '../src/client/index.js';

const client = createQueenClient({
    baseUrl: 'http://localhost:6632'
});

// Configure a queue with delayed processing
await client.configure({
    queue: 'htmls-delayed-02',
    // partition parameter removed - all config is queue-level now
    options: {
      windowBuffer: 10,
      //delayedProcessing: 10,  // Don't process messages until 60 seconds old
      leaseTime: 300,
      retryLimit: 3
    }
  })

for (let i = 0; i < 10; i++) {
await client.push({
    items: [{
        queue: 'htmls-delayed-02',
        partition: 'html-02',  // Use 'Default' instead of 0
        payload: { to: 'user-' + i + '@example.com' },
    }]
})
}

const stop = await client.consume({
    queue: 'htmls-delayed-02',
    partition: 'html-02',  // Use 'Default' instead of 0
    handler: async (message) => {
        console.log(message)
    },
    options: {
        wait: true,
        timeout: 30000,
        batch: 5
    }
})