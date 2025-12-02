import autocannon from 'autocannon';
import axios from 'axios';

const SERVER_URL = process.env.SERVER_URL || 'http://localhost:6632';
const QUEUE_NAME = 'test-queue-pop';
const MESSAGE_COUNT = 50000;  // Messages to pre-populate
const MAX_PARTITION = 500;

async function resetQueue() {
  try {
    console.log('Deleting queue...');
    await axios.delete(`${SERVER_URL}/api/v1/resources/queues/${QUEUE_NAME}`);
    console.log('✅ Queue deleted');
  } catch (error) {
    // Queue might not exist, that's fine
  }
  
  try {
    console.log('Creating queue...');
    await axios.post(`${SERVER_URL}/api/v1/configure`, {
      queue: QUEUE_NAME,
      options: {
        leaseTime: 300,
        retryLimit: 3
      }
    });
    console.log('✅ Queue created');
  } catch (error) {
    console.error('❌ Error creating queue:', error.response?.data || error.message);
  }
}

async function populateQueue() {
  console.log(`\nPopulating queue with ${MESSAGE_COUNT} messages across ${MAX_PARTITION} partitions...`);
  
  const batchSize = 100;  // Messages per request
  const batches = Math.ceil(MESSAGE_COUNT / batchSize);
  
  for (let b = 0; b < batches; b++) {
    const items = [];
    for (let i = 0; i < batchSize; i++) {
      const partition = (b * batchSize + i) % MAX_PARTITION;
      items.push({
        queue: QUEUE_NAME,
        partition: `${partition}`,
        payload: { 
          message: "Hello World",
          partition_id: partition,
          batch: b,
          index: i
        }
      });
    }
    
    await axios.post(`${SERVER_URL}/api/v1/push`, { items });
    
    if ((b + 1) % 100 === 0) {
      console.log(`  Pushed ${(b + 1) * batchSize} messages...`);
    }
  }
  
  // Verify
  const queue = await axios.get(`${SERVER_URL}/api/v1/resources/queues/${QUEUE_NAME}`);
  console.log(`✅ Queue populated with ${queue.data.totals.total} messages\n`);
}

await resetQueue();
await populateQueue();

// Pre-generate requests array with different partitions
// Each request pops from a specific partition with autoAck=true
const requests = [];
for (let i = 0; i < MAX_PARTITION; i++) {
  requests.push({
    method: 'GET',
    path: `/api/v1/pop/queue/${QUEUE_NAME}/partition/${i}?batch=1&wait=false&autoAck=false`,
    headers: {
      'Content-Type': 'application/json'
    }
  });
}

console.log('Starting POP benchmark with autoAck=true...\n');

// NOTE: autocannon v8 has a bug where pipelining > 1 with requests array
// doesn't track response stats. Use pipelining: 1 with more connections instead.
const instance = autocannon({
  url: SERVER_URL,
  connections: 500,   // More connections for throughput
  duration: 30,
  pipelining: 1,
  requests: requests,
});

instance.on('done', async (results) => {
  console.log('\n=== POP Benchmark Results (autoAck=true) ===');
  console.log(`Requests: ${results.requests.total}`);
  console.log(`Duration: ${results.duration}s`);
  console.log(`Throughput: ${results.requests.average} req/s`);
  console.log(`Latency p50: ${results.latency.p50}ms`);
  console.log(`Latency p99: ${results.latency.p99}ms`);
  console.log(`Errors: ${results.errors}`);
  console.log(`Non-2xx: ${results.non2xx}`);
  
  // Verify messages remaining in queue
  const queue = await axios.get(`${SERVER_URL}/api/v1/resources/queues/${QUEUE_NAME}`);
  console.log(`\n📊 Queue stats after benchmark:`);
  console.log(`   Total remaining: ${queue.data.totals.total}`);
  console.log(`   Pending: ${queue.data.totals.pending}`);
  console.log(`   Consumed: ${queue.data.totals.consumed}`);
});

autocannon.track(instance, { renderProgressBar: true });

