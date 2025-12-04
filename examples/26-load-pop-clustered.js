import autocannon from 'autocannon';
import axios from 'axios';

const SERVER_URL = process.env.SERVER_URL || 'http://localhost:6632';
const QUEUE_NAME = 'test-queue-pop-autocannon';
const MESSAGE_COUNT = parseInt(process.env.MESSAGE_COUNT) || 500000;
const MAX_PARTITION = parseInt(process.env.MAX_PARTITION) || 500;
const CONNECTIONS = parseInt(process.env.CONNECTIONS) || 500;
const DURATION = parseInt(process.env.DURATION) || 30;
const BATCH_SIZE = parseInt(process.env.BATCH_SIZE) || 1;

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
  console.log(`\nPopulating queue with ${MESSAGE_COUNT.toLocaleString()} messages across ${MAX_PARTITION} partitions...`);

  const batchSize = 1000;  // Messages per request
  const batches = Math.ceil(MESSAGE_COUNT / batchSize);
  const maxConcurrent = 10;

  // Prepare all batch payloads
  const batchPayloads = [];
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
    batchPayloads.push(items);
  }

  // Execute in parallel with max concurrent requests
  let completed = 0;
  for (let i = 0; i < batchPayloads.length; i += maxConcurrent) {
    const chunk = batchPayloads.slice(i, i + maxConcurrent);
    await Promise.all(chunk.map(items => axios.post(`${SERVER_URL}/api/v1/push`, { items })));
    completed += chunk.length;
    process.stdout.write(`\r  Pushed ${(completed * batchSize).toLocaleString()} messages...`);
  }

  // Verify
  const queue = await axios.get(`${SERVER_URL}/api/v1/resources/queues/${QUEUE_NAME}`);
  console.log(`\n✅ Queue populated with ${queue.data.totals.total.toLocaleString()} messages\n`);
}

async function runPopBenchmark() {
  console.log(`\n🚀 Starting POP benchmark with autocannon`);
  console.log(`   Connections: ${CONNECTIONS}`);
  console.log(`   Duration: ${DURATION}s`);
  console.log(`   Partitions: ${MAX_PARTITION}`);
  console.log(`   Batch size: ${BATCH_SIZE}`);
  console.log(`   autoAck: true\n`);

  // Pre-generate POP requests for each partition
  const requests = [];
  for (let i = 0; i < MAX_PARTITION; i++) {
    requests.push({
      method: 'GET',
      path: `/api/v1/pop/queue/${QUEUE_NAME}/partition/${i}?batch=${BATCH_SIZE}&wait=true&autoAck=true`,
      headers: {
        'Content-Type': 'application/json'
      }
    });
  }

  return new Promise((resolve) => {
    const instance = autocannon({
      url: SERVER_URL,
      connections: CONNECTIONS,
      duration: DURATION,
      pipelining: 1,
      requests: requests,
      workers: 10,
    });

    instance.on('done', async (results) => {
      console.log('\n' + '='.repeat(50));
      console.log('     AUTOCANNON POP BENCHMARK RESULTS');
      console.log('='.repeat(50));
      console.log(`Connections:           ${CONNECTIONS}`);
      console.log(`Duration:              ${results.duration}s`);
      console.log('─'.repeat(50));
      console.log(`Total Requests:        ${results.requests.total.toLocaleString()}`);
      console.log(`Throughput:            ${results.requests.average.toLocaleString()} req/s`);
      console.log(`Errors:                ${results.errors}`);
      console.log(`Non-2xx:               ${results.non2xx}`);
      console.log('─'.repeat(50));
      console.log('Latency:');
      console.log(`  p50: ${results.latency.p50}ms`);
      console.log(`  p75: ${results.latency.p75}ms`);
      console.log(`  p90: ${results.latency.p90}ms`);
      console.log(`  p99: ${results.latency.p99}ms`);
      console.log(`  avg: ${results.latency.average}ms`);
      console.log(`  max: ${results.latency.max}ms`);
      console.log('='.repeat(50));

      // Verify messages remaining in queue
      try {
        const queue = await axios.get(`${SERVER_URL}/api/v1/resources/queues/${QUEUE_NAME}`);
        console.log(`\n📊 Queue stats after benchmark:`);
        console.log(`   Total remaining: ${queue.data.totals.total.toLocaleString()}`);
        console.log(`   Pending: ${queue.data.totals.pending.toLocaleString()}`);
        console.log(`   Consumed: ${queue.data.totals.consumed.toLocaleString()}`);
      } catch (e) {
        console.log('Could not fetch queue stats');
      }

      resolve();
    });

    autocannon.track(instance, { renderProgressBar: true });
  });
}

// Main
console.log('\n🦆 Queen POP Benchmark (autocannon)\n');
await resetQueue();
await populateQueue();
await runPopBenchmark();

