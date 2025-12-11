import autocannon from 'autocannon';
import axios from 'axios';

const SERVER_URL = process.env.SERVER_URL || 'http://localhost:6632';
const QUEUE_NAME = 'queen-long-running';
const connections = 500;
const batchSize = 1000;
const maxPartition = 500;
const workers = 2;
const duration = 10 * 60 ; // 10 minutes

// Pre-generate requests array with different partitions
const requests = [];
for (let i = 0; i <= maxPartition; i++) {
  requests.push({
    method: 'GET',
    path: `/api/v1/pop/queue/${QUEUE_NAME}/partition/${i}?batch=${batchSize}&wait=true&autoAck=true`,
    headers: {
      'Content-Type': 'application/json'
    }
  });
}

const instance = autocannon({
  url: SERVER_URL,
  connections: connections,   // More connections for throughput
  duration: duration,
  pipelining: 1,
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
