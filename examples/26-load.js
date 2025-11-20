import autocannon from 'autocannon';
import axios from 'axios';

const SERVER_URL = process.env.SERVER_URL || 'http://localhost:6632';
const QUEUE_NAME = 'test-queue';

async function resetQueue() {
  try {
    // Delete
    console.log('Deleting queue...');
    await axios.delete(`${SERVER_URL}/api/v1/resources/queues/${QUEUE_NAME}`);
    console.log('✅ Queue deleted');
    
    // Create
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

const maxPartition = 100;
let partitionCounter = 0;

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

const instance = autocannon({
  url: SERVER_URL,
  connections: 64,
  duration: 5,
  pipelining: 10,
  requests: requests, // Autocannon will round-robin through these
}, (err, result) => {
  if (err) {
    console.error(err);
  } else {
    console.log('\n=== Benchmark Results ===');
    console.log(`Requests: ${result.requests.total}`);
    console.log(`Duration: ${result.duration}s`);
    console.log(`Throughput: ${result.requests.average} req/s`);
    console.log(`Latency p50: ${result.latency.p50}ms`);
    console.log(`Latency p99: ${result.latency.p99}ms`);
    console.log(`Errors: ${result.errors}`);
  }
});

autocannon.track(instance, { renderProgressBar: true });