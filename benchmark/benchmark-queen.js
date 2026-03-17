// Queen MQ Benchmark
// Measures latency and throughput with no client-side batching

import { Queen } from 'queen-mq';
import { config, generatePayload, getPartitionName } from './config.js';
import { BenchmarkMetrics, sleep } from './lib/metrics.js';
import { DockerMetrics, CONTAINER_NAMES } from './lib/docker-metrics.js';
import { writeFileSync } from 'fs';

const queen = new Queen(config.endpoints.queen);
const QUEUE_NAME = 'benchmark';

async function runLatencyBenchmark() {
  console.log('\n--- Queen: Latency Benchmark (single messages) ---');
  
  const metrics = new BenchmarkMetrics('Queen - Latency');
  const scenario = config.scenarios[0];  // latency-focused
  const messagesPerSecond = scenario.messagesPerSecond;
  const totalMessages = messagesPerSecond * scenario.duration;
  const delayMs = 1000 / messagesPerSecond;
  
  console.log(`Target: ${messagesPerSecond} msg/s for ${scenario.duration}s = ${totalMessages} messages`);
  
  metrics.start();
  
  for (let i = 0; i < totalMessages; i++) {
    const partitionIndex = i % config.partitionCount;
    const payload = generatePayload();
    const startNs = process.hrtime.bigint();
    
    try {
      await queen
        .queue(QUEUE_NAME)
        .partition(getPartitionName(partitionIndex))
        .push([{
          transactionId: `lat-${Date.now()}-${i}`,
          data: payload,
        }]);
      
      metrics.recordLatency(startNs);
      metrics.incrementSent(JSON.stringify(payload).length);
    } catch (e) {
      metrics.incrementErrors();
      console.error('Push error:', e.message);
    }
    
    // Rate limiting
    if (delayMs > 0) {
      await sleep(delayMs);
    }
    
    if ((i + 1) % 1000 === 0) {
      process.stdout.write(`\r  Progress: ${i + 1}/${totalMessages} messages`);
    }
  }
  
  metrics.stop();
  console.log('');
  return metrics.printReport();
}

async function runThroughputBenchmark() {
  console.log('\n--- Queen: Throughput Benchmark (max speed, no batching) ---');
  
  const metrics = new BenchmarkMetrics('Queen - Throughput');
  const scenario = config.scenarios[1];  // throughput-focused
  const concurrency = config.producer.concurrency;
  
  console.log(`Running for ${scenario.duration}s with ${concurrency} concurrent producers`);
  
  const endTime = Date.now() + (scenario.duration * 1000);
  let messageIndex = 0;
  
  metrics.start();
  
  // Run concurrent producers
  const producers = Array(concurrency).fill(null).map(async (_, producerId) => {
    while (Date.now() < endTime) {
      const batchPromises = [];
      
      // Each producer sends to different partitions
      for (let j = 0; j < 10; j++) {
        const idx = messageIndex++;
        const partitionIndex = idx % config.partitionCount;
        const payload = generatePayload();
        const startNs = process.hrtime.bigint();
        
        batchPromises.push(
          queen
            .queue(QUEUE_NAME)
            .partition(getPartitionName(partitionIndex))
            .push([{
              transactionId: `thr-${producerId}-${idx}`,
              data: payload,
            }])
            .then(() => {
              metrics.recordLatency(startNs);
              metrics.incrementSent(JSON.stringify(payload).length);
            })
            .catch((e) => {
              metrics.incrementErrors();
            })
        );
      }
      
      await Promise.all(batchPromises);
    }
  });
  
  // Progress reporter
  const progressInterval = setInterval(() => {
    const elapsed = (Date.now() - (endTime - scenario.duration * 1000)) / 1000;
    const rate = metrics.messagesSent / elapsed;
    process.stdout.write(`\r  Sent: ${metrics.messagesSent.toLocaleString()} msgs, Rate: ${Math.round(rate).toLocaleString()} msg/s`);
  }, 1000);
  
  await Promise.all(producers);
  clearInterval(progressInterval);
  
  metrics.stop();
  console.log('');
  return metrics.printReport();
}

async function runPartitionFanoutBenchmark() {
  console.log(`\n--- Queen: Partition Fanout Benchmark (${config.partitionCount} partitions) ---`);
  
  const metrics = new BenchmarkMetrics('Queen - Partition Fanout');
  const scenario = config.scenarios[2];  // partition-fanout
  const totalMessages = scenario.messagesPerSecond * scenario.duration;
  const concurrency = config.producer.concurrency;
  
  console.log(`Publishing to ${config.partitionCount} different partitions`);
  console.log(`Target: ${scenario.messagesPerSecond} msg/s for ${scenario.duration}s`);
  
  metrics.start();
  
  // Round-robin across all partitions
  let messageIndex = 0;
  const endTime = Date.now() + (scenario.duration * 1000);
  
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
            transactionId: `fan-${producerId}-${idx}`,
            data: payload,
          }]);
        
        metrics.recordLatency(startNs);
        metrics.incrementSent(JSON.stringify(payload).length);
      } catch (e) {
        metrics.incrementErrors();
      }
      
      // Rate limiting per producer
      const targetDelay = 1000 / (scenario.messagesPerSecond / concurrency);
      if (targetDelay > 0) {
        await sleep(targetDelay);
      }
    }
  });
  
  // Progress reporter
  const progressInterval = setInterval(() => {
    const elapsed = (Date.now() - (endTime - scenario.duration * 1000)) / 1000;
    const rate = metrics.messagesSent / elapsed;
    process.stdout.write(`\r  Sent: ${metrics.messagesSent.toLocaleString()} msgs, Rate: ${Math.round(rate).toLocaleString()} msg/s`);
  }, 1000);
  
  await Promise.all(producers);
  clearInterval(progressInterval);
  
  metrics.stop();
  console.log('');
  return metrics.printReport();
}

async function runConsumerBenchmark() {
  console.log('\n--- Queen: Consumer Benchmark ---');
  
  const metrics = new BenchmarkMetrics('Queen - Consumer');
  const concurrency = config.consumer.concurrency;
  const batchSize = config.consumer.batchSize;
  
  console.log(`Consuming with ${concurrency} concurrent consumers, batch size ${batchSize}`);
  
  metrics.start();
  
  let totalConsumed = 0;
  let globalEmptyCount = 0;
  let running = true;
  
  // Run concurrent consumers
  const consumers = Array(concurrency).fill(null).map(async (_, consumerId) => {
    let localEmptyCount = 0;
    
    while (running && localEmptyCount < 5) {
      const startNs = process.hrtime.bigint();
      
      try {
        // pop() returns an array of messages directly
        // wait(false) = don't block waiting for messages
        const messages = await queen
          .queue(QUEUE_NAME)
          .batch(batchSize)
          .wait(false)
          .pop();
        
        if (messages && messages.length > 0) {
          // Manual ack each message (fair comparison with Kafka/Pulsar)
          for (const msg of messages) {
            await queen.ack(msg, true);  // true = success
            metrics.incrementReceived(JSON.stringify(msg.data).length);
          }
          
          metrics.recordLatency(startNs);
          totalConsumed += messages.length;
          localEmptyCount = 0;
          globalEmptyCount = 0;
          
          if (totalConsumed % 1000 === 0) {
            process.stdout.write(`\r  Consumed: ${totalConsumed.toLocaleString()} messages`);
          }
        } else {
          localEmptyCount++;
          globalEmptyCount++;
          if (globalEmptyCount >= concurrency * 3) {
            running = false;  // All consumers seeing empty queues
          }
          await sleep(50);
        }
      } catch (e) {
        metrics.incrementErrors();
        await sleep(50);
      }
    }
  });
  
  await Promise.all(consumers);
  
  metrics.stop();
  console.log('');
  return metrics.printReport();
}

async function main() {
  console.log('╔════════════════════════════════════════════════════════════╗');
  console.log('║           Queen MQ Benchmark Suite                         ║');
  console.log('╚════════════════════════════════════════════════════════════╝');
  console.log(`\nConfiguration:`);
  console.log(`  Partitions: ${config.partitionCount}`);
  console.log(`  Linger: ${config.producer.lingerMs}ms (no batching)`);
  console.log(`  Message size: ${config.messageSize} bytes`);
  
  // Start Docker metrics collection
  const dockerMetrics = new DockerMetrics(CONTAINER_NAMES.queen);
  dockerMetrics.start();
  
  const results = {
    system: 'Queen MQ',
    timestamp: new Date().toISOString(),
    config: {
      partitions: config.partitionCount,
      lingerMs: config.producer.lingerMs,
      messageSize: config.messageSize,
    },
    benchmarks: {},
    dockerMetrics: null,
  };
  
  try {
    results.benchmarks.latency = await runLatencyBenchmark();
    results.benchmarks.throughput = await runThroughputBenchmark();
    results.benchmarks.partitionFanout = await runPartitionFanoutBenchmark();
    results.benchmarks.consumer = await runConsumerBenchmark();
  } catch (e) {
    console.error('Benchmark failed:', e);
  }
  
  // Stop and report Docker metrics
  dockerMetrics.stop();
  results.dockerMetrics = dockerMetrics.printReport();
  
  // Save results
  const filename = `results-queen-${Date.now()}.json`;
  writeFileSync(filename, JSON.stringify(results, null, 2));
  console.log(`\nResults saved to ${filename}`);
  
  return results;
}

main().catch(console.error);
