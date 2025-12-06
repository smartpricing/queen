import autocannon from 'autocannon';
import axios from 'axios';

const SERVER_URL = process.env.SERVER_URL || 'http://localhost:6632';
const QUEUE_NAME = 'queen-long-running';
const workers = 2;
const connections = 100;
const maxPartition = 500;
const duration = 60 * 1; // 10 minutes

async function createQueue() {
  await axios.post(`${SERVER_URL}/api/v1/configure`, {
    queue: QUEUE_NAME,
    options: {
      leaseTime: 60,
      retryLimit: 3,
      retentionEnabled: true,
      retentionSeconds: 1800,
      completedRetentionSeconds: 1800
    }
  });
}

await createQueue();

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
        queue: QUEUE_NAME,
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
  connections: connections,   // More connections for throughput
  duration: duration,
  requests: requests,
  workers: workers
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
