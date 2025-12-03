import { Pool } from 'undici';
import axios from 'axios';
import cluster from 'cluster';
import os from 'os';

const SERVER_URL = process.env.SERVER_URL || 'http://localhost:6632';
const QUEUE_NAME = 'test-queue-pop';
const MESSAGE_COUNT = parseInt(process.env.MESSAGE_COUNT) || 500000;
const MAX_PARTITION = parseInt(process.env.MAX_PARTITION) || 1000;
const NUM_WORKERS = parseInt(process.env.WORKERS) || os.cpus().length;
const CONNECTIONS_PER_WORKER = parseInt(process.env.CONNECTIONS) || 100;
const IDLE_TIMEOUT_MS = parseInt(process.env.IDLE_TIMEOUT) || 3000;
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
      process.stdout.write(`\r  Pushed ${((b + 1) * batchSize).toLocaleString()} messages...`);
    }
  }

  // Verify
  const queue = await axios.get(`${SERVER_URL}/api/v1/resources/queues/${QUEUE_NAME}`);
  console.log(`\n✅ Queue populated with ${queue.data.totals.total.toLocaleString()} messages\n`);
}

function calculatePercentile(arr, p) {
  if (arr.length === 0) return 0;
  const sorted = [...arr].sort((a, b) => a - b);
  const idx = Math.ceil((p / 100) * sorted.length) - 1;
  return sorted[Math.max(0, idx)];
}

if (cluster.isPrimary) {
  // ========== PRIMARY PROCESS ==========
  console.log(`\n🚀 Multi-process POP+ACK benchmark`);
  console.log(`   Workers: ${NUM_WORKERS}`);
  console.log(`   Connections per worker: ${CONNECTIONS_PER_WORKER}`);
  console.log(`   Total connections: ${NUM_WORKERS * CONNECTIONS_PER_WORKER}`);
  console.log(`   Messages to populate: ${MESSAGE_COUNT.toLocaleString()}`);
  console.log(`   Partitions: ${MAX_PARTITION}`);
  console.log(`   Batch size: ${BATCH_SIZE}`);
  console.log(`   Idle timeout: ${IDLE_TIMEOUT_MS}ms\n`);

  // Reset and populate queue before starting
  await resetQueue();
  await populateQueue();

  const workerResults = [];
  let workersReady = 0;
  let workersFinished = 0;
  let lastActivityTime = Date.now();
  let totalOpsAcrossWorkers = 0;
  const startTime = Date.now();

  // Fork workers
  for (let i = 0; i < NUM_WORKERS; i++) {
    const worker = cluster.fork({ WORKER_ID: i });

    worker.on('message', (msg) => {
      if (msg.type === 'ready') {
        workersReady++;
        console.log(`Worker ${msg.workerId} ready (${workersReady}/${NUM_WORKERS})`);

        // All workers ready, signal them to start simultaneously
        if (workersReady === NUM_WORKERS) {
          console.log('\n⏱️  All workers ready, starting benchmark...\n');
          for (const id in cluster.workers) {
            cluster.workers[id].send({ type: 'start' });
          }

          // Start progress reporting
          const progressInterval = setInterval(() => {
            const elapsed = (Date.now() - startTime) / 1000;
            const opsPerSec = Math.round(totalOpsAcrossWorkers / elapsed);
            const idleTime = ((Date.now() - lastActivityTime) / 1000).toFixed(1);
            process.stdout.write(`\r  Progress: ${totalOpsAcrossWorkers.toLocaleString()} ops | ${opsPerSec.toLocaleString()} ops/s | Idle: ${idleTime}s    `);

            // Check idle timeout
            if (Date.now() - lastActivityTime >= IDLE_TIMEOUT_MS) {
              clearInterval(progressInterval);
              console.log('\n\n⏹️  Idle timeout reached, stopping workers...');
              for (const id in cluster.workers) {
                cluster.workers[id].send({ type: 'stop' });
              }
            }
          }, 500);
        }
      } else if (msg.type === 'activity') {
        // Worker reports activity (got messages)
        lastActivityTime = Date.now();
        totalOpsAcrossWorkers = msg.totalOps;
      } else if (msg.type === 'done') {
        workerResults.push(msg.results);
        workersFinished++;
        
        // All workers done, aggregate results
        if (workersFinished === NUM_WORKERS) {
          aggregateResults(workerResults, startTime);
        }
      }
    });
  }

  async function aggregateResults(results, benchStartTime) {
    const duration = (Date.now() - benchStartTime) / 1000;

    // Aggregate metrics
    const aggregated = {
      totalPops: 0,
      totalAcks: 0,
      totalMessagesPopped: 0,
      totalErrors: 0,
      emptyPops: 0,
      popLatencies: [],
      ackLatencies: [],
      totalLatencies: []
    };

    for (const r of results) {
      aggregated.totalPops += r.totalPops;
      aggregated.totalAcks += r.totalAcks;
      aggregated.totalMessagesPopped += r.totalMessagesPopped;
      aggregated.totalErrors += r.totalErrors;
      aggregated.emptyPops += r.emptyPops;
      // Combine latency samples (take a sample from each worker to avoid memory issues)
      aggregated.popLatencies.push(...r.popLatencySample);
      aggregated.ackLatencies.push(...r.ackLatencySample);
      aggregated.totalLatencies.push(...r.totalLatencySample);
    }

    console.log('\n' + '='.repeat(50));
    console.log('     AGGREGATED POP+ACK BENCHMARK RESULTS');
    console.log('='.repeat(50));
    console.log(`Workers:               ${NUM_WORKERS}`);
    console.log(`Connections:           ${NUM_WORKERS * CONNECTIONS_PER_WORKER}`);
    console.log(`Duration:              ${duration.toFixed(2)}s`);
    console.log('─'.repeat(50));
    console.log(`Total POP+ACK cycles:  ${aggregated.totalPops.toLocaleString()}`);
    console.log(`Total messages ACKed:  ${aggregated.totalAcks.toLocaleString()}`);
    console.log(`Throughput:            ${Math.round(aggregated.totalAcks / duration).toLocaleString()} msgs/s`);
    console.log(`Errors:                ${aggregated.totalErrors}`);
    console.log(`Empty POPs:            ${aggregated.emptyPops.toLocaleString()}`);

    console.log('─'.repeat(50));
    console.log('POP Latency:');
    console.log(`  p50: ${calculatePercentile(aggregated.popLatencies, 50)}ms`);
    console.log(`  p95: ${calculatePercentile(aggregated.popLatencies, 95)}ms`);
    console.log(`  p99: ${calculatePercentile(aggregated.popLatencies, 99)}ms`);
    console.log(`  avg: ${aggregated.popLatencies.length ? Math.round(aggregated.popLatencies.reduce((a, b) => a + b, 0) / aggregated.popLatencies.length) : 0}ms`);

    console.log('─'.repeat(50));
    console.log('ACK Latency:');
    console.log(`  p50: ${calculatePercentile(aggregated.ackLatencies, 50)}ms`);
    console.log(`  p95: ${calculatePercentile(aggregated.ackLatencies, 95)}ms`);
    console.log(`  p99: ${calculatePercentile(aggregated.ackLatencies, 99)}ms`);
    console.log(`  avg: ${aggregated.ackLatencies.length ? Math.round(aggregated.ackLatencies.reduce((a, b) => a + b, 0) / aggregated.ackLatencies.length) : 0}ms`);

    console.log('─'.repeat(50));
    console.log('Total (POP+ACK) Latency:');
    console.log(`  p50: ${calculatePercentile(aggregated.totalLatencies, 50)}ms`);
    console.log(`  p95: ${calculatePercentile(aggregated.totalLatencies, 95)}ms`);
    console.log(`  p99: ${calculatePercentile(aggregated.totalLatencies, 99)}ms`);
    console.log(`  avg: ${aggregated.totalLatencies.length ? Math.round(aggregated.totalLatencies.reduce((a, b) => a + b, 0) / aggregated.totalLatencies.length) : 0}ms`);
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

    process.exit(0);
  }

} else {
  // ========== WORKER PROCESS ==========
  const workerId = parseInt(process.env.WORKER_ID);

  // Metrics tracking
  let totalPops = 0;
  let totalAcks = 0;
  let totalMessagesPopped = 0;
  let totalErrors = 0;
  let emptyPops = 0;
  const popLatencies = [];
  const ackLatencies = [];
  const totalLatencies = [];
  let stopped = false;

  // Calculate which partitions this worker handles
  const partitionsPerWorker = Math.ceil(MAX_PARTITION / NUM_WORKERS);
  const startPartition = workerId * partitionsPerWorker;
  const endPartition = Math.min(startPartition + partitionsPerWorker, MAX_PARTITION);

  // Worker function: continuously POP + ACK until stopped
  async function workerLoop(pool, connectionId) {
    // Round-robin through this worker's partitions
    let partitionIndex = 0;
    const myPartitions = [];
    for (let p = startPartition; p < endPartition; p++) {
      myPartitions.push(p);
    }
    // Also add some overlap to help drain all partitions
    if (myPartitions.length === 0) {
      for (let p = 0; p < MAX_PARTITION; p++) {
        myPartitions.push(p);
      }
    }

    while (!stopped) {
      const partition = myPartitions[partitionIndex % myPartitions.length];
      partitionIndex++;
      const cycleStart = Date.now();

      try {
        // POP request
        const popStart = Date.now();
        const popResponse = await pool.request({
          method: 'GET',
          path: `/api/v1/pop/queue/${QUEUE_NAME}/partition/${partition}?batch=${BATCH_SIZE}&wait=false`,
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
        popLatencies.push(popTime);

        // Report activity to master
        process.send({ 
          type: 'activity', 
          totalOps: totalAcks + messageCount 
        });

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

        totalAcks += messageCount;
        ackLatencies.push(ackTime);
        totalLatencies.push(Date.now() - cycleStart);

      } catch (error) {
        totalErrors++;
      }
    }
  }

  async function runWorker() {
    // Create undici pool with many connections
    const pool = new Pool(SERVER_URL, {
      connections: CONNECTIONS_PER_WORKER,
      pipelining: 1,
      keepAliveTimeout: 30000,
      keepAliveMaxTimeout: 60000,
    });

    // Start all connection workers
    const workers = [];
    for (let i = 0; i < CONNECTIONS_PER_WORKER; i++) {
      workers.push(workerLoop(pool, i));
    }

    // Wait for stop signal
    await new Promise(resolve => {
      process.on('message', (msg) => {
        if (msg.type === 'stop') {
          stopped = true;
          resolve();
        }
      });
    });

    // Give workers a moment to finish current operations
    await new Promise(r => setTimeout(r, 100));

    // Close pool
    await pool.close();

    // Sample latencies to avoid sending huge arrays
    const sampleSize = 1000;
    const sampleArray = (arr) => {
      if (arr.length <= sampleSize) return arr;
      const step = Math.floor(arr.length / sampleSize);
      return arr.filter((_, i) => i % step === 0).slice(0, sampleSize);
    };

    // Send results to master
    process.send({
      type: 'done',
      workerId,
      results: {
        totalPops,
        totalAcks,
        totalMessagesPopped,
        totalErrors,
        emptyPops,
        popLatencySample: sampleArray(popLatencies),
        ackLatencySample: sampleArray(ackLatencies),
        totalLatencySample: sampleArray(totalLatencies)
      }
    });
  }

  // Signal ready
  process.send({ type: 'ready', workerId });

  // Wait for start signal
  process.on('message', (msg) => {
    if (msg.type === 'start') {
      runWorker();
    }
  });
}

