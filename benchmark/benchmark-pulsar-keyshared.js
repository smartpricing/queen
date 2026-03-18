// Pulsar benchmark using Key_Shared subscription
// Single partition topic with virtual partitioning via message keys
// This is Pulsar's recommended approach for high partition count use cases

import Pulsar from 'pulsar-client';
import { config } from './config.js';
import { BenchmarkMetrics } from './lib/metrics.js';
import { DockerMetrics } from './lib/docker-metrics.js';

const TOPIC_NAME = 'persistent://public/benchmark/benchmark-keyshared';

function generatePayload() {
  return JSON.stringify({
    data: 'x'.repeat(config.messageSize - 50),
    ts: Date.now(),
  });
}

async function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function runPushBenchmark() {
  console.log('\n--- Pulsar Key_Shared: Push Benchmark ---');
  console.log(`Using ${config.partitionCount} virtual partition keys with ${config.producer.concurrency} concurrent producers`);
  console.log(`Duration: ${config.duration}s`);
  
  const metrics = new BenchmarkMetrics('Pulsar Key_Shared - Push');
  const concurrency = config.producer.concurrency;
  let messageIndex = 0;
  
  const client = new Pulsar.Client({
    serviceUrl: config.endpoints.pulsar,
    operationTimeoutSeconds: 60,
  });
  
  // Create producers - with single partition, this is much faster
  const producers = [];
  for (let i = 0; i < concurrency; i++) {
    for (let attempt = 0; attempt < 3; attempt++) {
      try {
        const producer = await client.createProducer({
          topic: TOPIC_NAME,
          batchingEnabled: false,
          sendTimeoutMs: 30000,
        });
        producers.push(producer);
        break;
      } catch (e) {
        console.log(`  Producer ${i + 1} attempt ${attempt + 1} failed: ${e.message}`);
        await sleep(1000);
      }
    }
  }
  
  if (producers.length === 0) {
    console.error('No producers could be created');
    await client.close();
    return { error: 'No producers created' };
  }
  
  console.log(`  ${producers.length} producers ready`);
  
  const endTime = Date.now() + (config.duration * 1000);
  
  metrics.start();
  
  const producerTasks = producers.map(async (producer, producerId) => {
    while (Date.now() < endTime) {
      const idx = messageIndex++;
      const virtualPartition = idx % config.partitionCount;
      const payload = generatePayload();
      const startNs = process.hrtime.bigint();
      
      try {
        await producer.send({
          data: Buffer.from(payload),
          // orderingKey is used by Key_Shared to route messages to consumers
          // Messages with the same orderingKey always go to the same consumer
          orderingKey: `vp-${virtualPartition}`,
        });
        
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
  
  await Promise.all(producerTasks);
  clearInterval(progressInterval);
  
  metrics.stop();
  console.log('');
  
  for (const producer of producers) {
    await producer.close();
  }
  await client.close();
  
  return metrics.printReport();
}

async function runConsumeBenchmark() {
  console.log('\n--- Pulsar Key_Shared: Consume Benchmark ---');
  console.log(`Consuming with ${config.consumer.concurrency} concurrent consumers (Key_Shared subscription)`);
  
  // Wait for broker to stabilize after producer benchmark
  console.log('  Waiting for broker to stabilize...');
  await sleep(5000);
  
  const metrics = new BenchmarkMetrics('Pulsar Key_Shared - Consume');
  const concurrency = config.consumer.concurrency;
  const subscriptionName = `benchmark-keyshared-sub-${Date.now()}`;
  
  const client = new Pulsar.Client({
    serviceUrl: config.endpoints.pulsar,
    operationTimeoutSeconds: 120,
  });
  
  // Create consumers with Key_Shared subscription
  const consumers = [];
  for (let i = 0; i < concurrency; i++) {
    for (let attempt = 0; attempt < 5; attempt++) {
      try {
        const consumer = await client.subscribe({
          topic: TOPIC_NAME,
          subscription: subscriptionName,
          subscriptionType: 'KeyShared',  // Key_Shared for per-key ordering
          subscriptionInitialPosition: 'Earliest',
          ackTimeoutMs: 60000,
          receiverQueueSize: 1000,
        });
        consumers.push(consumer);
        console.log(`  Consumer ${i + 1}/${concurrency} connected`);
        break;
      } catch (e) {
        console.log(`  Consumer ${i + 1} attempt ${attempt + 1} failed: ${e.message}`);
        await sleep(3000);
      }
    }
    // Small delay between consumer creations
    if (i < concurrency - 1) {
      await sleep(500);
    }
  }
  
  if (consumers.length === 0) {
    console.error('No consumers could connect');
    await client.close();
    return { error: 'No consumers connected' };
  }
  
  console.log(`  ${consumers.length} consumers ready (Key_Shared subscription)`);
  
  metrics.start();
  
  let totalConsumed = 0;
  let lastMessageTime = Date.now();
  const maxIdleTime = 10000;
  const benchmarkStartTime = Date.now();
  const maxWait = 120000;
  
  const consumerTasks = consumers.map(async (consumer) => {
    while (true) {
      if (Date.now() - lastMessageTime > maxIdleTime || Date.now() - benchmarkStartTime > maxWait) {
        break;
      }
      
      try {
        const startNs = process.hrtime.bigint();
        const messages = await consumer.batchReceive(config.consumer.batchSize, 5000);
        
        if (messages && messages.length > 0) {
          for (const msg of messages) {
            await consumer.acknowledge(msg);
          }
          
          metrics.recordLatency(startNs);
          metrics.incrementReceived(messages.length);
          totalConsumed += messages.length;
          lastMessageTime = Date.now();
        }
      } catch (e) {
        if (e.message && e.message.includes('TimeOut')) {
          if (Date.now() - lastMessageTime > maxIdleTime) {
            break;
          }
        } else {
          metrics.incrementErrors();
        }
      }
    }
  });
  
  const progressInterval = setInterval(() => {
    process.stdout.write(`\r  Consumed: ${totalConsumed.toLocaleString()} messages`);
  }, 2000);
  
  await Promise.all(consumerTasks);
  clearInterval(progressInterval);
  
  metrics.stop();
  console.log('');
  
  for (const consumer of consumers) {
    await consumer.close();
  }
  await client.close();
  
  return metrics.printReport();
}

async function main() {
  console.log('╔════════════════════════════════════════════════════════════╗');
  console.log('║     Apache Pulsar Key_Shared Benchmark Suite               ║');
  console.log('╚════════════════════════════════════════════════════════════╝');
  console.log();
  console.log('Configuration:');
  console.log(`  Physical partitions: 1`);
  console.log(`  Virtual partitions (keys): ${config.partitionCount}`);
  console.log(`  Subscription type: Key_Shared`);
  console.log(`  Batching: disabled`);
  console.log(`  Message size: ${config.messageSize} bytes`);
  console.log(`  Duration: ${config.duration}s`);
  console.log(`  Producers: ${config.producer.concurrency}`);
  console.log(`  Consumers: ${config.consumer.concurrency}`);
  
  const dockerMetrics = new DockerMetrics(['pulsar-broker']);
  dockerMetrics.start();
  
  const results = {
    system: 'Apache Pulsar (Key_Shared)',
    timestamp: new Date().toISOString(),
    config: {
      physicalPartitions: 1,
      virtualPartitions: config.partitionCount,
      subscriptionType: 'Key_Shared',
      batchingEnabled: false,
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
  const filename = `results-pulsar-keyshared-${Date.now()}.json`;
  fs.writeFileSync(filename, JSON.stringify(results, null, 2));
  console.log(`\nResults saved to ${filename}`);
}

main().catch(console.error);
