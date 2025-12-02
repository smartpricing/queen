import autocannon from 'autocannon';
import axios from 'axios';

const SERVER_URL = process.env.SERVER_URL || 'http://localhost:6632';
const QUEUE_NAME = 'test-queue';

async function resetQueue() {
  try {
    console.log('Deleting queue...');
    await axios.delete(`${SERVER_URL}/api/v1/resources/queues/${QUEUE_NAME}`);
    console.log('✅ Queue deleted');
    
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
    console.error('❌ Error:', error.response?.data || error.message);
  }
}

await resetQueue();

const maxPartition = 500;

// Pre-generate requests array with different partitions
const requests = [];
for (let i = 0; i <= maxPartition; i++) {
  requests.push({
    method: 'POST',
    path: '/api/v1/push',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      items: [{
        queue: "test-queue",
        partition: `${i}`,
        payload: { 
          message: "Hello World",
          partition_id: i
        }
      }]
    })
  });
}

// NOTE: autocannon v8 has a bug where pipelining > 1 with requests array
// doesn't track response stats. Use pipelining: 1 with more connections instead.
const instance = autocannon({
  url: SERVER_URL,
  connections: 500,   // More connections for throughput
  duration: 10,
  pipelining: 1,
  requests: requests,
});

instance.on('done', async (results) => {
  console.log('\n=== Benchmark Results ===');
  console.log(`Requests: ${results.requests.total}`);
  console.log(`Duration: ${results.duration}s`);
  console.log(`Throughput: ${results.requests.average} req/s`);
  console.log(`Latency p50: ${results.latency.p50}ms`);
  console.log(`Latency p99: ${results.latency.p99}ms`);
  console.log(`Errors: ${results.errors}`);
  console.log(`Non-2xx: ${results.non2xx}`);
  
  // Verify messages in queue
  const queue = await axios.get(`${SERVER_URL}/api/v1/resources/queues/${QUEUE_NAME}`);
  console.log(`\n✅ Queue total messages: ${queue.data.totals.total}`);
});

autocannon.track(instance, { renderProgressBar: true });
