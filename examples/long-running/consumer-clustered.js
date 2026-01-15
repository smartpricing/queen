import autocannon from 'autocannon';
import axios from 'axios';
import cluster from 'cluster';
import os from 'os';

const SERVER_URL = process.env.SERVER_URL || 'http://localhost:6632';
const QUEUE_NAME = 'queen-long-running';
const NUM_WORKERS = 4;
const CONNECTIONS_PER_WORKER = 100;
const DURATION = 60 * 60 * 24;
const MAX_PARTITION = 1000;
const batchSize = 1000;

// Pre-generate requests array
function generateRequests() {
  const requests = [];
  for (let i = 0; i <= MAX_PARTITION; i++) {
    requests.push({
      method: 'GET',
      path: `/api/v1/pop/queue/${QUEUE_NAME}/partition/${i}?batch=${batchSize}&wait=false&autoAck=true`,
      headers: { 'Content-Type': 'application/json' }
    });
  }
  return requests;
}


if (cluster.isPrimary) {
  // ========== PRIMARY PROCESS ==========
  console.log(`\n🚀 Multi-process load test starting`);
  console.log(`   Workers: ${NUM_WORKERS}`);
  console.log(`   Connections per worker: ${CONNECTIONS_PER_WORKER}`);
  console.log(`   Total connections: ${NUM_WORKERS * CONNECTIONS_PER_WORKER}`);
  console.log(`   Duration: ${DURATION}s\n`);

  const workerResults = [];
  let workersReady = 0;
  let workersFinished = 0;

  // Fork workers
  for (let i = 0; i < NUM_WORKERS; i++) {
    const worker = cluster.fork();
    
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
        }
      } else if (msg.type === 'done') {
        workerResults.push(msg.results);
        workersFinished++;
        console.log(`Worker ${msg.workerId} finished (${workersFinished}/${NUM_WORKERS})`);
        
        // All workers done, aggregate results
        if (workersFinished === NUM_WORKERS) {
        }
      }
    });
  }
} else {
  // ========== WORKER PROCESS ==========
  const workerId = cluster.worker.id;
  const requests = generateRequests();

  // Signal ready
  process.send({ type: 'ready', workerId });

  // Wait for start signal
  process.on('message', (msg) => {
    if (msg.type === 'start') {
      runBenchmark();
    }
  });

  function runBenchmark() {
    const instance = autocannon({
      url: SERVER_URL,
      connections: CONNECTIONS_PER_WORKER,
      duration: DURATION,
      pipelining: 1,
      requests: requests,
      workers: NUM_WORKERS
    });

    instance.on('done', (results) => {
      process.send({ type: 'done', workerId, results });
    });
  }
}