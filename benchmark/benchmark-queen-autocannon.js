import autocannon from 'autocannon';
import axios from 'axios';
import cluster from 'cluster';
import { config } from './config.js';

const SERVER_URL = config.endpoints.queen;
const QUEUE_NAME = 'benchmark';
const NUM_WORKERS = 5;
const CONNECTIONS_PER_WORKER = 50;
const DURATION = 60;
const MAX_PARTITION = config.partitionCount;
const NUMBER_OF_MESSAGES_PER_PUSH = 1;

function generateRequests() {
  const requests = [];
  for (let i = 0; i < MAX_PARTITION; i++) {
    let items = [];
    for (let j = 0; j < NUMBER_OF_MESSAGES_PER_PUSH; j++) {
      items.push({
        queue: QUEUE_NAME,
        partition: `${i}`,
        payload: { 
          data: 'x'.repeat(config.messageSize - 50),
          ts: Date.now()
        }
      });
    }    
    requests.push({
      method: 'POST',
      path: '/api/v1/push',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ items })
    });
  }
  return requests;
}

if (cluster.isPrimary) {
  console.log('╔════════════════════════════════════════════════════════════╗');
  console.log('║     Queen MQ Autocannon Benchmark (HTTP Direct)            ║');
  console.log('╚════════════════════════════════════════════════════════════╝');
  console.log();
  console.log('Configuration:');
  console.log(`  Partitions: ${config.partitionCount}`);
  console.log(`  Workers: ${NUM_WORKERS}`);
  console.log(`  Connections per worker: ${CONNECTIONS_PER_WORKER}`);
  console.log(`  Total connections: ${NUM_WORKERS * CONNECTIONS_PER_WORKER}`);
  console.log(`  Duration: ${DURATION}s`);
  console.log(`  Messages per request: ${NUMBER_OF_MESSAGES_PER_PUSH}`);
  console.log();

  const workerResults = [];
  let workersReady = 0;
  let workersFinished = 0;

  for (let i = 0; i < NUM_WORKERS; i++) {
    const worker = cluster.fork();
    
    worker.on('message', (msg) => {
      if (msg.type === 'ready') {
        workersReady++;
        console.log(`Worker ${msg.workerId} ready (${workersReady}/${NUM_WORKERS})`);
        
        if (workersReady === NUM_WORKERS) {
          console.log('\n--- All workers ready, starting benchmark ---\n');
          for (const id in cluster.workers) {
            cluster.workers[id].send({ type: 'start' });
          }
        }
      } else if (msg.type === 'done') {
        workerResults.push(msg.results);
        workersFinished++;
        console.log(`Worker ${msg.workerId} finished (${workersFinished}/${NUM_WORKERS})`);
        
        if (workersFinished === NUM_WORKERS) {
          aggregateResults(workerResults);
        }
      }
    });
  }

  async function aggregateResults(results) {
    const aggregated = {
      totalRequests: 0,
      totalBytes: 0,
      errors: 0,
      non2xx: 0,
      timeouts: 0,
      duration: DURATION,
      latencies: [],
      throughputs: []
    };

    for (const r of results) {
      aggregated.totalRequests += r.requests.total;
      aggregated.totalBytes += r.throughput.total;
      aggregated.errors += r.errors;
      aggregated.non2xx += r.non2xx;
      aggregated.timeouts += r.timeouts;
      aggregated.latencies.push(r.latency);
      aggregated.throughputs.push(r.requests.average);
    }

    const avgLatency = {
      p50: aggregated.latencies.reduce((sum, l) => sum + l.p50, 0) / results.length,
      p90: aggregated.latencies.reduce((sum, l) => sum + (l.p90 || l.p99), 0) / results.length,
      p99: aggregated.latencies.reduce((sum, l) => sum + l.p99, 0) / results.length,
      avg: aggregated.latencies.reduce((sum, l) => sum + l.average, 0) / results.length,
      max: Math.max(...aggregated.latencies.map(l => l.max))
    };

    const totalThroughput = aggregated.throughputs.reduce((sum, t) => sum + t, 0);
    const totalMessages = aggregated.totalRequests * NUMBER_OF_MESSAGES_PER_PUSH;
    const messagesPerSecond = Math.round(totalThroughput * NUMBER_OF_MESSAGES_PER_PUSH);

    console.log('\n');
    console.log('============================================================');
    console.log('  Queen - Autocannon Benchmark Results (Aggregated)');
    console.log('============================================================');
    console.log(`  Workers: ${NUM_WORKERS}`);
    console.log(`  Connections: ${NUM_WORKERS * CONNECTIONS_PER_WORKER}`);
    console.log(`  Duration: ${aggregated.duration}s`);
    console.log();
    console.log(`  HTTP Requests: ${aggregated.totalRequests.toLocaleString()}`);
    console.log(`  Messages sent: ${totalMessages.toLocaleString()}`);
    console.log(`  Throughput: ${Math.round(totalThroughput).toLocaleString()} req/s`);
    console.log(`  Messages/sec: ${messagesPerSecond.toLocaleString()} msg/s`);
    console.log();
    console.log('  Latency (ms):');
    console.log(`    Mean: ${avgLatency.avg.toFixed(2)}`);
    console.log(`    p50:  ${avgLatency.p50.toFixed(2)}`);
    console.log(`    p90:  ${avgLatency.p90.toFixed(2)}`);
    console.log(`    p99:  ${avgLatency.p99.toFixed(2)}`);
    console.log(`    Max:  ${avgLatency.max.toFixed(2)}`);
    console.log();
    console.log(`  Errors: ${aggregated.errors}`);
    console.log(`  Non-2xx: ${aggregated.non2xx}`);
    console.log(`  Timeouts: ${aggregated.timeouts}`);
    console.log('============================================================');

    // Save results
    const timestamp = Date.now();
    const resultData = {
      system: 'Queen MQ (Autocannon)',
      timestamp: new Date().toISOString(),
      config: {
        partitions: config.partitionCount,
        workers: NUM_WORKERS,
        connectionsPerWorker: CONNECTIONS_PER_WORKER,
        totalConnections: NUM_WORKERS * CONNECTIONS_PER_WORKER,
        messagesPerRequest: NUMBER_OF_MESSAGES_PER_PUSH,
        messageSize: config.messageSize,
        duration: DURATION
      },
      results: {
        duration: aggregated.duration,
        requests: aggregated.totalRequests,
        totalMessages,
        requestsPerSecond: Math.round(totalThroughput),
        messagesPerSecond,
        latency: {
          mean: avgLatency.avg,
          p50: avgLatency.p50,
          p90: avgLatency.p90,
          p99: avgLatency.p99,
          max: avgLatency.max
        },
        errors: aggregated.errors,
        non2xx: aggregated.non2xx,
        timeouts: aggregated.timeouts
      }
    };
    
    const fs = await import('fs');
    fs.writeFileSync(`results-queen-autocannon-${timestamp}.json`, JSON.stringify(resultData, null, 2));
    console.log(`\nResults saved to results-queen-autocannon-${timestamp}.json`);

    process.exit(0);
  }

} else {
  const workerId = cluster.worker.id;
  const requests = generateRequests();

  process.send({ type: 'ready', workerId });

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
