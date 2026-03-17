// Apache Kafka Benchmark
// Measures latency and throughput with linger.ms=0 (no batching)

import { Kafka, Partitioners, logLevel } from 'kafkajs';
import { config, generatePayload, getPartitionName } from './config.js';
import { BenchmarkMetrics, sleep } from './lib/metrics.js';
import { DockerMetrics, CONTAINER_NAMES } from './lib/docker-metrics.js';
import { writeFileSync } from 'fs';

const kafka = new Kafka({
  clientId: 'benchmark',
  brokers: [config.endpoints.kafka],
  logLevel: logLevel.ERROR,  // Suppress WARN spam for leader changes
  retry: {
    initialRetryTime: 100,
    retries: 10,
    maxRetryTime: 30000,
    factor: 2,
  },
});

const TOPIC_NAME = 'benchmark';

// Check if topic exists and has expected partitions
async function verifyTopicExists() {
  const admin = kafka.admin();
  await admin.connect();
  
  try {
    const topics = await admin.listTopics();
    if (!topics.includes(TOPIC_NAME)) {
      throw new Error(`Topic '${TOPIC_NAME}' not found. Run 'npm run setup:kafka' first.`);
    }
    
    const metadata = await admin.fetchTopicMetadata({ topics: [TOPIC_NAME] });
    const partitionCount = metadata.topics[0].partitions.length;
    console.log(`Topic '${TOPIC_NAME}' found with ${partitionCount} partitions`);
    
    if (partitionCount < config.partitionCount) {
      console.warn(`WARNING: Topic has ${partitionCount} partitions, expected ${config.partitionCount}`);
    }
  } finally {
    await admin.disconnect();
  }
}

async function runLatencyBenchmark() {
  console.log('\n--- Kafka: Latency Benchmark (single messages) ---');
  
  const producer = kafka.producer({
    allowAutoTopicCreation: false,
    createPartitioner: Partitioners.LegacyPartitioner,
    metadataMaxAge: 10000,  // Refresh metadata every 10s for partition leader changes
    // CRITICAL: linger.ms = 0 for no batching (fair comparison)
    // KafkaJS doesn't have linger.ms, but we send one message at a time
    // and await each send, which achieves the same effect
  });
  
  await producer.connect();
  
  const metrics = new BenchmarkMetrics('Kafka - Latency');
  const scenario = config.scenarios[0];  // latency-focused
  const messagesPerSecond = scenario.messagesPerSecond;
  const totalMessages = messagesPerSecond * scenario.duration;
  const delayMs = 1000 / messagesPerSecond;
  
  console.log(`Target: ${messagesPerSecond} msg/s for ${scenario.duration}s = ${totalMessages} messages`);
  
  metrics.start();
  
  for (let i = 0; i < totalMessages; i++) {
    const partitionIndex = i % config.partitionCount;
    const payload = generatePayload();
    const payloadStr = JSON.stringify(payload);
    const startNs = process.hrtime.bigint();
    
    try {
      await producer.send({
        topic: TOPIC_NAME,
        acks: config.durability.kafkaAcks,  // -1 = all
        messages: [{
          key: `lat-${i}`,
          value: payloadStr,
          partition: partitionIndex,
        }],
      });
      
      metrics.recordLatency(startNs);
      metrics.incrementSent(payloadStr.length);
    } catch (e) {
      metrics.incrementErrors();
      console.error('Send error:', e.message);
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
  await producer.disconnect();
  console.log('');
  return metrics.printReport();
}

async function runThroughputBenchmark() {
  console.log('\n--- Kafka: Throughput Benchmark (max speed, no batching) ---');
  
  const metrics = new BenchmarkMetrics('Kafka - Throughput');
  const scenario = config.scenarios[1];  // throughput-focused
  const concurrency = config.producer.concurrency;
  
  console.log(`Running for ${scenario.duration}s with ${concurrency} concurrent producers`);
  
  // Create multiple producers for concurrency
  const producers = await Promise.all(
    Array(concurrency).fill(null).map(async (_, i) => {
      const p = kafka.producer({ allowAutoTopicCreation: false, createPartitioner: Partitioners.LegacyPartitioner, metadataMaxAge: 10000 });
      await p.connect();
      return p;
    })
  );
  
  const endTime = Date.now() + (scenario.duration * 1000);
  let messageIndex = 0;
  
  metrics.start();
  
  // Run concurrent producers
  const producerTasks = producers.map(async (producer, producerId) => {
    while (Date.now() < endTime) {
      const batchPromises = [];
      
      // Each producer sends multiple messages concurrently
      for (let j = 0; j < 10; j++) {
        const idx = messageIndex++;
        const partitionIndex = idx % config.partitionCount;
        const payload = generatePayload();
        const payloadStr = JSON.stringify(payload);
        const startNs = process.hrtime.bigint();
        
        batchPromises.push(
          producer.send({
            topic: TOPIC_NAME,
            acks: config.durability.kafkaAcks,
            messages: [{
              key: `thr-${producerId}-${idx}`,
              value: payloadStr,
              partition: partitionIndex,
            }],
          })
            .then(() => {
              metrics.recordLatency(startNs);
              metrics.incrementSent(payloadStr.length);
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
  
  await Promise.all(producerTasks);
  clearInterval(progressInterval);
  
  metrics.stop();
  
  // Disconnect all producers
  await Promise.all(producers.map(p => p.disconnect()));
  
  console.log('');
  return metrics.printReport();
}

async function runPartitionFanoutBenchmark() {
  console.log(`\n--- Kafka: Partition Fanout Benchmark (${config.partitionCount} partitions) ---`);
  
  const producer = kafka.producer({ allowAutoTopicCreation: false, createPartitioner: Partitioners.LegacyPartitioner, metadataMaxAge: 10000 });
  await producer.connect();
  
  const metrics = new BenchmarkMetrics('Kafka - Partition Fanout');
  const scenario = config.scenarios[2];  // partition-fanout
  const concurrency = config.producer.concurrency;
  
  console.log(`Publishing to ${config.partitionCount} different partitions`);
  console.log(`Target: ${scenario.messagesPerSecond} msg/s for ${scenario.duration}s`);
  
  metrics.start();
  
  let messageIndex = 0;
  const endTime = Date.now() + (scenario.duration * 1000);
  const targetDelay = 1000 / scenario.messagesPerSecond;
  
  while (Date.now() < endTime) {
    const idx = messageIndex++;
    const partitionIndex = idx % config.partitionCount;
    const payload = generatePayload();
    const payloadStr = JSON.stringify(payload);
    const startNs = process.hrtime.bigint();
    
    try {
      await producer.send({
        topic: TOPIC_NAME,
        acks: config.durability.kafkaAcks,
        messages: [{
          key: `fan-${idx}`,
          value: payloadStr,
          partition: partitionIndex,
        }],
      });
      
      metrics.recordLatency(startNs);
      metrics.incrementSent(payloadStr.length);
    } catch (e) {
      metrics.incrementErrors();
    }
    
    // Rate limiting
    if (targetDelay > 0) {
      await sleep(targetDelay);
    }
    
    if (messageIndex % 1000 === 0) {
      const elapsed = (Date.now() - (endTime - scenario.duration * 1000)) / 1000;
      const rate = metrics.messagesSent / elapsed;
      process.stdout.write(`\r  Sent: ${metrics.messagesSent.toLocaleString()} msgs, Rate: ${Math.round(rate).toLocaleString()} msg/s`);
    }
  }
  
  metrics.stop();
  await producer.disconnect();
  console.log('');
  return metrics.printReport();
}

async function runConsumerBenchmark() {
  console.log('\n--- Kafka: Consumer Benchmark ---');
  
  const batchSize = config.consumer.batchSize;
  
  const consumer = kafka.consumer({
    groupId: `benchmark-consumer-${Date.now()}`,
    sessionTimeout: 30000,
    heartbeatInterval: 3000,
    // Disable auto-commit for manual ack (fair comparison)
    autoCommit: false,
    // Fetch settings for batching
    maxBytesPerPartition: 1024 * 1024,  // 1MB per partition
    minBytes: 1,
    maxWaitTimeInMs: 100,  // Don't wait too long for batches
  });
  
  await consumer.connect();
  await consumer.subscribe({ topic: TOPIC_NAME, fromBeginning: true });
  
  const metrics = new BenchmarkMetrics('Kafka - Consumer');
  
  console.log(`Consuming with batch processing (partitions processed in parallel internally)...`);
  
  const benchmarkStartTime = Date.now();
  metrics.start();
  
  let totalConsumed = 0;
  let lastMessageTime = Date.now();
  
  // Use eachBatch for fair comparison with Queen's batch pop
  await consumer.run({
    eachBatch: async ({ batch, resolveOffset, commitOffsetsIfNecessary }) => {
      const startNs = process.hrtime.bigint();
      const messages = batch.messages;
      
      if (messages.length > 0) {
        // Process and ack each message in batch
        for (const message of messages) {
          metrics.incrementReceived(message.value.length);
          resolveOffset(message.offset);
        }
        
        // Commit the batch
        await commitOffsetsIfNecessary();
        
        metrics.recordLatency(startNs);
        totalConsumed += messages.length;
        lastMessageTime = Date.now();
        
        if (totalConsumed % 1000 === 0) {
          process.stdout.write(`\r  Consumed: ${totalConsumed.toLocaleString()} messages`);
        }
      }
    },
  });
  
  // Wait for messages or timeout
  const maxWait = 30000;  // 30 seconds
  while (Date.now() - lastMessageTime < 5000) {
    await sleep(100);
    if (Date.now() - benchmarkStartTime > maxWait) break;
  }
  
  metrics.stop();
  await consumer.disconnect();
  console.log('');
  return metrics.printReport();
}

async function main() {
  console.log('╔════════════════════════════════════════════════════════════╗');
  console.log('║           Apache Kafka Benchmark Suite                     ║');
  console.log('╚════════════════════════════════════════════════════════════╝');
  console.log(`\nConfiguration:`);
  console.log(`  Partitions: ${config.partitionCount}`);
  console.log(`  Linger: 0ms (no batching - each send awaited)`);
  console.log(`  Acks: ${config.durability.kafkaAcks} (all)`);
  console.log(`  Message size: ${config.messageSize} bytes`);
  
  // Verify topic exists before starting
  try {
    await verifyTopicExists();
  } catch (e) {
    console.error(`\n❌ ${e.message}`);
    process.exit(1);
  }
  
  // Start Docker metrics collection
  const dockerMetrics = new DockerMetrics(CONTAINER_NAMES.kafka);
  dockerMetrics.start();
  
  const results = {
    system: 'Apache Kafka',
    timestamp: new Date().toISOString(),
    config: {
      partitions: config.partitionCount,
      lingerMs: 0,
      acks: config.durability.kafkaAcks,
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
  const filename = `results-kafka-${Date.now()}.json`;
  writeFileSync(filename, JSON.stringify(results, null, 2));
  console.log(`\nResults saved to ${filename}`);
  
  return results;
}

main().catch(console.error);
