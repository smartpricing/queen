import { Pool } from 'undici';
import axios from 'axios';

const SERVER_URL = process.env.SERVER_URL || 'http://localhost:6632';
const QUEUE_NAME = 'test-queue-pop';
const MESSAGE_COUNT = 50000;  // Messages to pre-populate
const MAX_PARTITION = 500;
const CONCURRENT_WORKERS = 500;  // Number of concurrent POP+ACK workers
const TEST_DURATION_MS = 30000;  // 30 seconds

// Metrics tracking
let totalPops = 0;
let totalAcks = 0;
let totalErrors = 0;
let emptyPops = 0;
const latencies = [];

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

// Worker function: continuously POP + ACK until stopped
async function worker(pool, workerId, stopSignal) {
  const partition = workerId % MAX_PARTITION;
  
  while (!stopSignal.stopped) {
    const start = Date.now();
    
    try {
      // POP request
      const popResponse = await pool.request({
        method: 'GET',
        path: `/api/v1/pop/queue/${QUEUE_NAME}/partition/${partition}?batch=1&wait=false`,
        headers: { 'Content-Type': 'application/json' }
      });
      
      const popBody = await popResponse.body.json();
      
      if (!popBody.items || popBody.items.length === 0) {
        emptyPops++;
        continue;
      }
      
      totalPops++;
      
      // ACK request with the message ID
      const messageId = popBody.items[0].id;
      const ackResponse = await pool.request({
        method: 'POST',
        path: `/api/v1/ack`,
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ items: [{ id: messageId }] })
      });
      
      await ackResponse.body.dump(); // Consume body
      
      totalAcks++;
      latencies.push(Date.now() - start);
      
    } catch (error) {
      totalErrors++;
    }
  }
}

function calculatePercentile(arr, p) {
  if (arr.length === 0) return 0;
  const sorted = [...arr].sort((a, b) => a - b);
  const idx = Math.ceil((p / 100) * sorted.length) - 1;
  return sorted[Math.max(0, idx)];
}

async function runBenchmark() {
  console.log(`Starting POP+ACK benchmark with ${CONCURRENT_WORKERS} concurrent workers...\n`);
  console.log(`Duration: ${TEST_DURATION_MS / 1000}s\n`);
  
  // Create undici pool with many connections
  const pool = new Pool(SERVER_URL, {
    connections: CONCURRENT_WORKERS,
    pipelining: 1,
    keepAliveTimeout: 30000,
    keepAliveMaxTimeout: 60000,
  });
  
  const stopSignal = { stopped: false };
  const startTime = Date.now();
  
  // Progress reporting
  const progressInterval = setInterval(() => {
    const elapsed = (Date.now() - startTime) / 1000;
    const opsPerSec = Math.round(totalAcks / elapsed);
    process.stdout.write(`\r  Progress: ${totalAcks} ops | ${opsPerSec} ops/s | Errors: ${totalErrors} | Empty: ${emptyPops}    `);
  }, 500);
  
  // Start all workers
  const workers = [];
  for (let i = 0; i < CONCURRENT_WORKERS; i++) {
    workers.push(worker(pool, i, stopSignal));
  }
  
  // Wait for duration
  await new Promise(resolve => setTimeout(resolve, TEST_DURATION_MS));
  
  // Stop workers
  stopSignal.stopped = true;
  clearInterval(progressInterval);
  
  // Wait for workers to finish current operations
  await Promise.allSettled(workers);
  
  const duration = (Date.now() - startTime) / 1000;
  
  // Calculate results
  console.log('\n\n=== POP+ACK Benchmark Results ===');
  console.log(`Total POP+ACK cycles: ${totalAcks}`);
  console.log(`Duration: ${duration.toFixed(2)}s`);
  console.log(`Throughput: ${Math.round(totalAcks / duration)} ops/s`);
  console.log(`Latency p50: ${calculatePercentile(latencies, 50)}ms`);
  console.log(`Latency p95: ${calculatePercentile(latencies, 95)}ms`);
  console.log(`Latency p99: ${calculatePercentile(latencies, 99)}ms`);
  console.log(`Errors: ${totalErrors}`);
  console.log(`Empty POPs: ${emptyPops}`);
  
  // Verify messages remaining in queue
  try {
    const queue = await axios.get(`${SERVER_URL}/api/v1/resources/queues/${QUEUE_NAME}`);
    console.log(`\n📊 Queue stats after benchmark:`);
    console.log(`   Total remaining: ${queue.data.totals.total}`);
    console.log(`   Pending: ${queue.data.totals.pending}`);
    console.log(`   Consumed: ${queue.data.totals.consumed}`);
  } catch (e) {
    console.log('Could not fetch queue stats');
  }
  
  await pool.close();
}

// Main
await resetQueue();
await populateQueue();
await runBenchmark();
