// Create client
import fs from 'fs';
import { createQueenClient } from '../src/client/index.js';

const client = createQueenClient({
    baseUrl: 'http://localhost:6632'
});

const numberOfConsumers = 10

async function producer () {
    let count = 0;
    while (true) {
        const partition = count % 2;
        console.log('Producing to partition:', partition);
        const items = [];
        for (let i = 0; i < 100; i++) {
            items.push({ queue: 'htmls', partition: partition, payload: { to: 'user@example.com-' + count++ } });
        }
        await client.push({
            items: items
        })
        await new Promise(resolve => setTimeout(resolve, 10));
    }
}

async function consumer () {
    const stop = client.consume({
        queue: 'htmls',
        handler: async (message) => {
          console.log('Processing:', message.data);
          //fs.appendFileSync('consumed_items ' + message.partition + '.json', JSON.stringify(message.data) + '\n');
        },
        options: {
            batch: 100,  // Process up to 10 messages at a time
            wait: true,  // Wait for messages if queue is empty
            timeout: 30000  // Timeout for long polling
        }
    });

    return stop;
}


if (process.argv[2] === 'producer') {
    producer(); 
} else {
    for (let i = 0; i < numberOfConsumers; i++) {
        consumer();
    }
}