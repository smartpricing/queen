import { Pool } from 'undici';
import axios from 'axios';

const SERVER_URL = process.env.SERVER_URL || 'http://localhost:6632';
const QUEUE_NAME = 'test-queue-pop';
const MESSAGE_COUNT = 500000;       // Messages to pre-populate
const MAX_PARTITION = 500;
const CONCURRENT_WORKERS = 500;   // Number of concurrent POP+ACK workers
const IDLE_TIMEOUT_MS = 3000;     // Exit after 3s of no messages

// Metrics tracking
let totalPops = 0;
let totalAcks = 0;
let totalErrors = 0;
let emptyPops = 0;
let lastPopTime = Date.now();
const popLatencies = [];
const ackLatencies = [];
const totalLatencies = [];

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
  
  // Execute in parallel with max 10 concurrent requests
  let completed = 0;
  for (let i = 0; i < batchPayloads.length; i += maxConcurrent) {
    const chunk = batchPayloads.slice(i, i + maxConcurrent);
    await Promise.all(chunk.map(items => axios.post(`${SERVER_URL}/api/v1/push`, { items })));
    completed += chunk.length;
    console.log(`  Pushed ${completed * batchSize} messages...`);
  }
  
  // Verify
  const queue = await axios.get(`${SERVER_URL}/api/v1/resources/queues/${QUEUE_NAME}`);
  console.log(`✅ Queue populated with ${queue.data.totals.total} messages\n`);
}

// Worker function: continuously POP + ACK until stopped
let totalMessagesPopped = 0
async function worker(pool, workerId, stopSignal) {
  const partition = workerId % MAX_PARTITION;
  
  while (!stopSignal.stopped) {
    const cycleStart = Date.now();
    
    try {
      // POP request
      const popStart = Date.now();
      const popResponse = await pool.request({
        method: 'GET',
        path: `/api/v1/pop/queue/${QUEUE_NAME}/partition/${partition}?batch=1&wait=true`,
        headers: { 'Content-Type': 'application/json' }
      });
      
      const popBody = await popResponse.body.json();
      const popTime = Date.now() - popStart;
      
      if (!popBody.messages || popBody.messages.length === 0) {
        emptyPops++;
        continue;
      }

      const messageCount = popBody.messages.length;
      totalMessagesPopped += messageCount;
      
      totalPops++;
      lastPopTime = Date.now();
      popLatencies.push(popTime);
      
      // Batch ACK all messages at once
      const acknowledgments = popBody.messages.map(msg => ({
        transactionId: msg.transactionId,
        partitionId: msg.partitionId
      }));
      
      const ackStart = Date.now();
      const ackResponse = await pool.request({
        method: 'POST',
        path: `/api/v1/ack/batch`,
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ acknowledgments })
      });
      
      await ackResponse.body.dump(); // Consume body
      const ackTime = Date.now() - ackStart;
      
      totalAcks += messageCount;  // Count actual messages ACKed
      ackLatencies.push(ackTime);
      totalLatencies.push(Date.now() - cycleStart);
      
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
  console.log(`Will exit ${IDLE_TIMEOUT_MS / 1000}s after last message received\n`);
  
  // Create undici pool with many connections
  const pool = new Pool(SERVER_URL, {
    connections: CONCURRENT_WORKERS,
    pipelining: 1,
    keepAliveTimeout: 30000,
    keepAliveMaxTimeout: 60000,
  });
  
  const stopSignal = { stopped: false };
  const startTime = Date.now();
  lastPopTime = Date.now();
  
  // Progress reporting
  const progressInterval = setInterval(() => {
    const elapsed = (Date.now() - startTime) / 1000;
    const opsPerSec = Math.round(totalAcks / elapsed);
    const idleTime = ((Date.now() - lastPopTime) / 1000).toFixed(1);
    process.stdout.write(`\r  Progress: ${totalAcks} ops | ${opsPerSec} ops/s | Errors: ${totalErrors} | Idle: ${idleTime}s    `);
  }, 500);
  
  // Start all workers
  const workers = [];
  for (let i = 0; i < CONCURRENT_WORKERS; i++) {
    workers.push(worker(pool, i, stopSignal));
  }
  
  // Wait until idle timeout (no messages for IDLE_TIMEOUT_MS)
  await new Promise(resolve => {
    const checkIdle = setInterval(() => {
      if (Date.now() - lastPopTime >= IDLE_TIMEOUT_MS) {
        clearInterval(checkIdle);
        resolve();
      }
    }, 100);
  });
  
  // Stop workers
  stopSignal.stopped = true;
  clearInterval(progressInterval);
  
  const duration = (Date.now() - startTime) / 1000;
  
  // Close pool to abort any stuck wait=true requests
  await pool.close();
  
  // Calculate results
  console.log('\n\n=== POP+ACK Benchmark Results ===');
  console.log(`Total POP+ACK cycles: ${totalAcks}`);
  console.log(`Total POP+ACK messages: ${totalMessagesPopped}`);
  console.log(`Duration: ${duration.toFixed(2)}s`);
  console.log(`Throughput: ${Math.round(totalAcks / duration)} ops/s`);
  console.log(`Errors: ${totalErrors}`);
  console.log(`Empty POPs: ${emptyPops}`);
  
  console.log('\n--- POP Latency ---');
  console.log(`  p50: ${calculatePercentile(popLatencies, 50)}ms`);
  console.log(`  p95: ${calculatePercentile(popLatencies, 95)}ms`);
  console.log(`  p99: ${calculatePercentile(popLatencies, 99)}ms`);
  console.log(`  avg: ${popLatencies.length ? Math.round(popLatencies.reduce((a, b) => a + b, 0) / popLatencies.length) : 0}ms`);
  
  console.log('\n--- ACK Latency ---');
  console.log(`  p50: ${calculatePercentile(ackLatencies, 50)}ms`);
  console.log(`  p95: ${calculatePercentile(ackLatencies, 95)}ms`);
  console.log(`  p99: ${calculatePercentile(ackLatencies, 99)}ms`);
  console.log(`  avg: ${ackLatencies.length ? Math.round(ackLatencies.reduce((a, b) => a + b, 0) / ackLatencies.length) : 0}ms`);
  
  console.log('\n--- Total (POP+ACK) Latency ---');
  console.log(`  p50: ${calculatePercentile(totalLatencies, 50)}ms`);
  console.log(`  p95: ${calculatePercentile(totalLatencies, 95)}ms`);
  console.log(`  p99: ${calculatePercentile(totalLatencies, 99)}ms`);
  console.log(`  avg: ${totalLatencies.length ? Math.round(totalLatencies.reduce((a, b) => a + b, 0) / totalLatencies.length) : 0}ms`);
  
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
}

// Main
await resetQueue();
await populateQueue();
await runBenchmark();
