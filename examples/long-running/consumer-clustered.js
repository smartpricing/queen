import autocannon from 'autocannon';
import axios from 'axios';
import cluster from 'cluster';
import os from 'os';

const SERVER_URL = process.env.SERVER_URL || 'http://localhost:6632';
const QUEUE_NAME = 'queen-long-running';
const NUM_WORKERS = Number(process.env.NUM_WORKERS || 1);
const CONNECTIONS_PER_WORKER = Number(process.env.CONNECTIONS_PER_WORKER || 50);
const DURATION = Number(process.env.DURATION || 120);
const MAX_PARTITION = Number(process.env.MAX_PARTITION || 1000);
const batchSize = Number(process.env.BATCH_SIZE || 1000);

// Pre-generate requests array
function generateRequests() {
  const requests = [];
  requests.push({
    method: 'GET',
    path: `/api/v1/pop/queue/${QUEUE_NAME}?batch=${batchSize}&wait=true&autoAck=true`,
    headers: { 'Content-Type': 'application/json' }
  });
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

        if (workersFinished === NUM_WORKERS) {
          let totalReq = 0, total2xx = 0, totalNon2xx = 0, totalErr = 0, totalTO = 0;
          let latSum = { p50: 0, p90: 0, p99: 0, avg: 0, max: 0 };
          for (const r of workerResults) {
            totalReq += r.requests.total;
            total2xx += (r['2xx'] || 0);
            totalNon2xx += (r.non2xx || 0);
            totalErr += (r.errors || 0);
            totalTO += (r.timeouts || 0);
            latSum.p50 += r.latency.p50;
            latSum.p90 += (r.latency.p90 || r.latency.p99);
            latSum.p99 += r.latency.p99;
            latSum.avg += r.latency.average;
            latSum.max = Math.max(latSum.max, r.latency.max);
          }
          const n = workerResults.length;
          console.log('\n' + '='.repeat(50));
          console.log('      CONSUMER AGGREGATED RESULTS');
          console.log('='.repeat(50));
          console.log(`Total requests:  ${totalReq}`);
          console.log(`2xx:             ${total2xx}`);
          console.log(`non-2xx:         ${totalNon2xx}`);
          console.log(`errors:          ${totalErr}`);
          console.log(`timeouts:        ${totalTO}`);
          console.log(`Throughput:      ${(totalReq / DURATION).toFixed(1)} req/s`);
          console.log(`Latency avg:     ${(latSum.avg / n).toFixed(2)} ms`);
          console.log(`Latency p50:     ${(latSum.p50 / n).toFixed(2)} ms`);
          console.log(`Latency p99:     ${(latSum.p99 / n).toFixed(2)} ms`);
          console.log(`Latency max:     ${latSum.max.toFixed(2)} ms`);
          console.log('='.repeat(50));
          process.exit(0);
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
    });

    instance.on('done', (results) => {
      process.send({ type: 'done', workerId, results });
    });
  }
}
