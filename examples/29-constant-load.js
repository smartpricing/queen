import { Queen } from '../client-js/client-v2/index.js'

const QUEUE_NAME = 'test-c-load';
const PARTITIONS = 10;

let lastPartition = 0;

const queen = new Queen({
  url: 'http://localhost:6632',
});

await queen.queue(QUEUE_NAME).config({
  leaseTime: 10,
  retryLimit: 3,
  retentionEnabled: true,
  retentionSeconds: 360,
  completedRetentionSeconds: 120
}).create();

async function producer () {
    await queen.queue(QUEUE_NAME)
    .partition(lastPartition.toString())
    .push([
        {
            data: {
                message: 'Hello, world!',
            },
        },
    ]);

    lastPartition = (lastPartition + 1) % PARTITIONS;
}

async function consumer1 () {
    let consumerCount = 0;
    await queen.queue(QUEUE_NAME)
    .group('c1')
    .batch(1000)
    .autoAck(true)
    .consume(async (messages) => {
        consumerCount += messages.length; 
        if (consumerCount % 1000 === 0) {
            console.log(`C1 Consumed ${consumerCount} messages`);
        }
    });
}

async function consumer2 () {
    let consumerCount = 0;
    await queen.queue(QUEUE_NAME)
    .group('c2')
    .batch(1000)
    .autoAck(true)
    .consume(async (messages) => {
        consumerCount += messages.length; 
        if (consumerCount % 1000 === 0) {
            console.log(`C2 Consumed ${consumerCount} messages`);
        }
    });
}

setInterval(producer, 1);
consumer1();
consumer2();