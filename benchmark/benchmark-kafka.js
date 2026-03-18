import { Kafka, Partitioners, logLevel } from 'kafkajs';
import { config } from './config.js';
import { BenchmarkMetrics } from './lib/metrics.js';
import { DockerMetrics } from './lib/docker-metrics.js';

const TOPIC_NAME = 'benchmark';

const kafka = new Kafka({
  clientId: 'benchmark-client',
  brokers: [config.endpoints.kafka],
  logLevel: logLevel.ERROR,
  retry: {
    initialRetryTime: 100,
    retries: 8
  }
});

function generatePayload() {
  return JSON.stringify({
    data: 'x'.repeat(config.messageSize - 50),
    ts: Date.now(),
  });
}

async function runPushBenchmark() {
  console.log('\n--- Kafka: Push Benchmark ---');
  console.log(`Pushing to ${config.partitionCount} partitions with ${config.producer.concurrency} concurrent producers`);
  console.log(`Duration: ${config.duration}s`);
  
  const metrics = new BenchmarkMetrics('Kafka - Push');
  const concurrency = config.producer.concurrency;
  const endTime = Date.now() + (config.duration * 1000);
  let messageIndex = 0;
  
  const producers = await Promise.all(
    Array(concurrency).fill(null).map(async () => {
      const producer = kafka.producer({
        createPartitioner: Partitioners.LegacyPartitioner,
        allowAutoTopicCreation: false,
        idempotent: false,
        maxInFlightRequests: 1,  // Only 1 request at a time - no pipelining
        metadataMaxAge: 10000,
        retry: {
          initialRetryTime: 100,
          retries: 5
        }
      });
      await producer.connect();
      return producer;
    })
  );
  
  metrics.start();
  
  const producerTasks = producers.map(async (producer, producerId) => {
    while (Date.now() < endTime) {
      const idx = messageIndex++;
      const partition = idx % config.partitionCount;
      const payload = generatePayload();
      const startNs = process.hrtime.bigint();
      
      try {
        await producer.send({
          topic: TOPIC_NAME,
          messages: [{
            key: `key-${partition}`,
            value: payload,
            partition: partition,
          }],
          acks: -1,
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
    await producer.disconnect();
  }
  
  return metrics.printReport();
}

async function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function runConsumeBenchmark() {
  console.log('\n--- Kafka: Consume Benchmark ---');
  console.log(`Consuming with ${config.consumer.concurrency} concurrent consumers (single group)`);
  
  // Wait for broker to recover after producer benchmark
  console.log('Waiting for broker to stabilize...');
  await sleep(5000);
  
  const metrics = new BenchmarkMetrics('Kafka - Consume');
  const concurrency = config.consumer.concurrency;
  const groupId = `benchmark-group-${Date.now()}`;
  
  // Create consumers sequentially with single group
  const consumers = [];
  for (let i = 0; i < concurrency; i++) {
    const consumer = kafka.consumer({ 
      groupId: groupId,
      sessionTimeout: 60000,
      heartbeatInterval: 5000,
      rebalanceTimeout: 120000,
      maxWaitTimeInMs: 5000,
      retry: {
        initialRetryTime: 1000,
        retries: 10,
        maxRetryTime: 30000,
      }
    });
    
    let connected = false;
    for (let attempt = 0; attempt < 5 && !connected; attempt++) {
      try {
        await consumer.connect();
        await consumer.subscribe({ topic: TOPIC_NAME, fromBeginning: true });
        connected = true;
        consumers.push(consumer);
        console.log(`  Consumer ${i + 1}/${concurrency} connected`);
      } catch (e) {
        console.log(`  Consumer ${i + 1} connect attempt ${attempt + 1} failed: ${e.message}`);
        await sleep(2000);
      }
    }
    
    // Small delay between consumer creations
    if (i < concurrency - 1) {
      await sleep(500);
    }
  }
  
  if (consumers.length === 0) {
    console.error('No consumers could connect');
    return { error: 'No consumers connected' };
  }
  
  console.log(`  ${consumers.length} consumers ready`);
  
  metrics.start();
  
  let totalConsumed = 0;
  let lastMessageTime = Date.now();
  const maxIdleTime = 15000;
  const benchmarkStartTime = Date.now();
  const maxWait = 120000;
  
  const consumerTasks = consumers.map(async (consumer) => {
    return new Promise((resolve) => {
      consumer.run({
        autoCommit: true,
        autoCommitInterval: 1000,
        eachBatch: async ({ batch, heartbeat }) => {
          const startNs = process.hrtime.bigint();
          const batchSize = batch.messages.length;
          
          if (batchSize > 0) {
            totalConsumed += batchSize;
            metrics.incrementReceived(batchSize);
            metrics.recordLatency(startNs);
            lastMessageTime = Date.now();
          }
          
          try {
            await heartbeat();
          } catch (e) {
            // Ignore heartbeat errors
          }
          
          if (Date.now() - lastMessageTime > maxIdleTime || Date.now() - benchmarkStartTime > maxWait) {
            resolve();
          }
        }
      }).catch(() => resolve());
      
      const checkInterval = setInterval(() => {
        if (Date.now() - lastMessageTime > maxIdleTime || Date.now() - benchmarkStartTime > maxWait) {
          clearInterval(checkInterval);
          resolve();
        }
      }, 1000);
    });
  });
  
  const progressInterval = setInterval(() => {
    process.stdout.write(`\r  Consumed: ${totalConsumed.toLocaleString()} messages`);
  }, 2000);
  
  await Promise.all(consumerTasks);
  clearInterval(progressInterval);
  
  metrics.stop();
  console.log('');
  
  for (const consumer of consumers) {
    try {
      await consumer.disconnect();
    } catch (e) {
      // Ignore disconnect errors
    }
  }
  
  return metrics.printReport();
}

async function main() {
  console.log('╔════════════════════════════════════════════════════════════╗');
  console.log('║           Apache Kafka Benchmark Suite                     ║');
  console.log('╚════════════════════════════════════════════════════════════╝');
  console.log();
  console.log('Configuration:');
  console.log(`  Partitions: ${config.partitionCount}`);
  console.log(`  Acks: -1 (all replicas)`);
  console.log(`  Message size: ${config.messageSize} bytes`);
  console.log(`  Duration: ${config.duration}s`);
  console.log(`  Producers: ${config.producer.concurrency}`);
  console.log(`  Consumers: ${config.consumer.concurrency}`);
  
  const dockerMetrics = new DockerMetrics(['kafka-broker']);
  dockerMetrics.start();
  
  const results = {
    system: 'Apache Kafka',
    timestamp: new Date().toISOString(),
    config: {
      partitions: config.partitionCount,
      acks: -1,
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
  const filename = `results-kafka-${Date.now()}.json`;
  fs.writeFileSync(filename, JSON.stringify(results, null, 2));
  console.log(`\nResults saved to ${filename}`);
}

main().catch(console.error);
