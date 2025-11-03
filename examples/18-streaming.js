/**
 * Simple Streaming Test - Wait for actual window processing
 */

import { Queen } from '../client-js/client-v2/Queen.js';

const queen = new Queen({ url: 'http://localhost:6632' });

async function main() {
  console.log('\n========== Simple Streaming Test ==========\n');
  
  await queen.queue('test-stream-queue-alice').delete()
  await queen.queue('test-stream-queue-alice').namespace('test').task('stream').create();
  

  await queen.stream('test-stream-alice', 'test')
    .sources(['test-stream-queue-alice'])
    .tumblingTime(5)
    .gracePeriod(1)
    .define();
  
  const producer = async () => {
    while (true) {
      const message = await queen.queue('test-stream-queue-alice').partition('test-partition').push({
        data: {
          value: Math.random() * 100,
          timestamp: new Date().toISOString()
        }
      });
      console.log('Produced message');
      await new Promise(resolve => setTimeout(resolve, 1000));
    }
  }
  producer();
  
  const consumer = queen.consumer('test-stream-alice', 'test-consumer-alice');
  let windowsProcessed = 0;
  
  
  await consumer.process(async (window) => {
    console.log(new Date().toISOString(), window.id, window.start,window.end,window.allMessages.length);
  });
  
  
  await queen.close();
  process.exit(0);
}

main()

