import { Queen } from 'queen-mq';
import { config } from './config.js';
import { BenchmarkMetrics } from './lib/metrics.js';
import { DockerMetrics } from './lib/docker-metrics.js';

const QUEUE_NAME = 'benchmark';

const queen = new Queen(config.endpoints.queen);

function generatePayload() {
  return JSON.stringify({
    data: 'x'.repeat(config.messageSize - 50),
    ts: Date.now(),
  });
}

function getPartitionName(index) {
  return `partition-${index}`;
}

async function runPushBenchmark() {
  console.log('\n--- Queen: Push Benchmark ---');
  console.log(`Pushing to ${config.partitionCount} partitions with ${config.producer.concurrency} concurrent producers`);
  console.log(`Duration: ${config.duration}s`);
  
  const metrics = new BenchmarkMetrics('Queen - Push');
  const concurrency = config.producer.concurrency;
  const endTime = Date.now() + (config.duration * 1000);
  let messageIndex = 0;
  
  metrics.start();
  
  const producers = Array(concurrency).fill(null).map(async (_, producerId) => {
    while (Date.now() < endTime) {
      const idx = messageIndex++;
      const partitionIndex = idx % config.partitionCount;
      const payload = generatePayload();
      const startNs = process.hrtime.bigint();
      
      try {
        await queen
          .queue(QUEUE_NAME)
          .partition(getPartitionName(partitionIndex))
          .push([{
            transactionId: `push-${producerId}-${idx}`,
            data: payload,
          }]);
        
        metrics.recordLatency(startNs);
        metrics.incrementSent(1, payload.length);
      } catch (e) {
        metrics.incrementErrors();
      }
    }
  });
  
  const progressInterval = setInterval(() => {
    const elapsed = (Date.now() - (endTime - config.duration * 1000)) / 1000;
    const rate = metrics.messagesSent / elapsed;
    process.stdout.write(`\r  Sent: ${metrics.messagesSent.toLocaleString()} msgs, Rate: ${Math.round(rate).toLocaleString()} msg/s`);
  }, 1000);
  
  await Promise.all(producers);
  clearInterval(progressInterval);
  
  metrics.stop();
  console.log('');
  return metrics.printReport();
}

async function runConsumeBenchmark() {
  console.log('\n--- Queen: Consume Benchmark ---');
  console.log(`Consuming with ${config.consumer.concurrency} concurrent consumers, batch size ${config.consumer.batchSize}`);
  
  const metrics = new BenchmarkMetrics('Queen - Consume');
  const concurrency = config.consumer.concurrency;
  
  metrics.start();
  
  let totalConsumed = 0;
  let lastMessageTime = Date.now();
  const maxIdleTime = 10000;
  const benchmarkStartTime = Date.now();
  const maxWait = 120000;
  
  const consumers = Array(concurrency).fill(null).map(async (_, consumerId) => {
    let emptyCount = 0;
    
    while (emptyCount < 50) {
      if (Date.now() - lastMessageTime > maxIdleTime) {
        break;
      }
      if (Date.now() - benchmarkStartTime > maxWait) {
        break;
      }
      
      try {
        const startNs = process.hrtime.bigint();
        const messages = await queen
          .queue(QUEUE_NAME)
          .batch(config.consumer.batchSize)
          .wait(false)
          .pop();
        
        if (messages && messages.length > 0) {
          await queen.ack(messages, true);
          
          metrics.recordLatency(startNs);
          metrics.incrementReceived(messages.length);
          totalConsumed += messages.length;
          lastMessageTime = Date.now();
          emptyCount = 0;
        } else {
          emptyCount++;
          await new Promise(r => setTimeout(r, 100));
        }
      } catch (e) {
        metrics.incrementErrors();
      }
    }
  });
  
  const progressInterval = setInterval(() => {
    process.stdout.write(`\r  Consumed: ${totalConsumed.toLocaleString()} messages`);
  }, 2000);
  
  await Promise.all(consumers);
  clearInterval(progressInterval);
  
  metrics.stop();
  console.log('');
  return metrics.printReport();
}

async function main() {
  console.log('╔════════════════════════════════════════════════════════════╗');
  console.log('║           Queen MQ Benchmark Suite                         ║');
  console.log('╚════════════════════════════════════════════════════════════╝');
  console.log();
  console.log('Configuration:');
  console.log(`  Partitions: ${config.partitionCount}`);
  console.log(`  Message size: ${config.messageSize} bytes`);
  console.log(`  Duration: ${config.duration}s`);
  console.log(`  Producers: ${config.producer.concurrency}`);
  console.log(`  Consumers: ${config.consumer.concurrency}`);
  
  const dockerMetrics = new DockerMetrics(['queen-postgres', 'queen-server']);
  dockerMetrics.start();
  
  const results = {
    system: 'Queen MQ',
    timestamp: new Date().toISOString(),
    config: {
      partitions: config.partitionCount,
      messageSize: config.messageSize,
      duration: config.duration,
    },
    benchmarks: {},
  };
  
  results.benchmarks.push = await runPushBenchmark();
  results.benchmarks.consume = await runConsumeBenchmark();
  
  dockerMetrics.stop();
  results.dockerMetrics = dockerMetrics.getStats();
  
  console.log('\n────────────────────────────────────────────────────────────');
  console.log('  Docker Resource Usage');
  console.log('────────────────────────────────────────────────────────────');
  for (const [container, stats] of Object.entries(results.dockerMetrics.containers)) {
    console.log(`\n  Container: ${container}`);
    console.log(`    CPU:    avg=${stats.cpu.avg.toFixed(2)}%  max=${stats.cpu.max.toFixed(2)}%  p95=${stats.cpu.p95.toFixed(2)}%`);
    console.log(`    Memory: avg=${stats.memoryMB.avg.toFixed(2)}MB  max=${stats.memoryMB.max.toFixed(2)}MB  p95=${stats.memoryMB.p95.toFixed(2)}MB`);
  }
  console.log('────────────────────────────────────────────────────────────');
  
  const fs = await import('fs');
  const filename = `results-queen-${Date.now()}.json`;
  fs.writeFileSync(filename, JSON.stringify(results, null, 2));
  console.log(`\nResults saved to ${filename}`);
}

main().catch(console.error);
