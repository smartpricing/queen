// Apache Pulsar Benchmark
// Measures latency and throughput with batching disabled

import Pulsar from 'pulsar-client';
import { config, generatePayload, getPartitionName } from './config.js';
import { BenchmarkMetrics, sleep } from './lib/metrics.js';
import { DockerMetrics, CONTAINER_NAMES } from './lib/docker-metrics.js';
import { writeFileSync } from 'fs';

const TOPIC_NAME = 'persistent://public/benchmark/benchmark';

async function createProducer(client, partitionIndex = null) {
  const topic = partitionIndex !== null 
    ? `${TOPIC_NAME}-partition-${partitionIndex}`
    : TOPIC_NAME;
    
  return client.createProducer({
    topic,
    sendTimeoutMs: 30000,
    // CRITICAL: Disable batching for fair comparison
    batchingEnabled: false,
  });
}

async function runLatencyBenchmark(client) {
  console.log('\n--- Pulsar: Latency Benchmark (single messages) ---');
  
  const producer = await createProducer(client);
  
  const metrics = new BenchmarkMetrics('Pulsar - Latency');
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
        data: Buffer.from(payloadStr),
        partitionKey: `lat-${i}`,
        // Route to specific partition
        eventTimestamp: Date.now(),
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
  await producer.close();
  console.log('');
  return metrics.printReport();
}

async function runThroughputBenchmark(client) {
  console.log('\n--- Pulsar: Throughput Benchmark (max speed, no batching) ---');
  
  const metrics = new BenchmarkMetrics('Pulsar - Throughput');
  const scenario = config.scenarios[1];  // throughput-focused
  const concurrency = config.producer.concurrency;
  
  console.log(`Running for ${scenario.duration}s with ${concurrency} concurrent producers`);
  
  // Create multiple producers for concurrency
  const producers = await Promise.all(
    Array(concurrency).fill(null).map(() => createProducer(client))
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
        const payload = generatePayload();
        const payloadStr = JSON.stringify(payload);
        const startNs = process.hrtime.bigint();
        
        batchPromises.push(
          producer.send({
            data: Buffer.from(payloadStr),
            partitionKey: `thr-${producerId}-${idx}`,
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
  
  // Close all producers
  await Promise.all(producers.map(p => p.close()));
  
  console.log('');
  return metrics.printReport();
}

async function runPartitionFanoutBenchmark(client) {
  console.log(`\n--- Pulsar: Partition Fanout Benchmark (${config.partitionCount} partitions) ---`);
  
  const metrics = new BenchmarkMetrics('Pulsar - Partition Fanout');
  const scenario = config.scenarios[2];  // partition-fanout
  const concurrency = config.producer.concurrency;
  
  console.log(`Publishing to ${config.partitionCount} different partitions`);
  console.log(`Target: ${scenario.messagesPerSecond} msg/s for ${scenario.duration}s`);
  console.log(`Using ${concurrency} concurrent producers`);
  
  // Create multiple producers for concurrency
  const producers = await Promise.all(
    Array(concurrency).fill(null).map(() => createProducer(client))
  );
  
  metrics.start();
  
  let messageIndex = 0;
  const endTime = Date.now() + (scenario.duration * 1000);
  
  // Run concurrent producers for better fanout performance
  const producerTasks = producers.map(async (producer, producerId) => {
    while (Date.now() < endTime) {
      const idx = messageIndex++;
      const partitionIndex = idx % config.partitionCount;
      const payload = generatePayload();
      const payloadStr = JSON.stringify(payload);
      const startNs = process.hrtime.bigint();
      
      try {
        // Use unique ordering key to ensure distribution across partitions
        await producer.send({
          data: Buffer.from(payloadStr),
          partitionKey: `p${partitionIndex}`,  // Short key for partition routing
          orderingKey: `p${partitionIndex}`,   // Ordering key also helps routing
        });
        
        metrics.recordLatency(startNs);
        metrics.incrementSent(payloadStr.length);
      } catch (e) {
        metrics.incrementErrors();
      }
      
      // Rate limiting per producer
      const targetDelay = 1000 / (scenario.messagesPerSecond / concurrency);
      if (targetDelay > 1) {
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
  
  await Promise.all(producerTasks);
  clearInterval(progressInterval);
  
  metrics.stop();
  
  // Close all producers
  await Promise.all(producers.map(p => p.close()));
  
  console.log('');
  return metrics.printReport();
}

async function runConsumerBenchmark(client) {
  console.log('\n--- Pulsar: Consumer Benchmark ---');
  
  const batchSize = config.consumer.batchSize;
  const concurrency = config.consumer.concurrency;
  
  // Create multiple consumers for concurrency (shared subscription)
  const consumers = await Promise.all(
    Array(concurrency).fill(null).map(() => 
      client.subscribe({
        topic: TOPIC_NAME,
        subscription: `benchmark-consumer-shared`,  // Same subscription for load balancing
        subscriptionType: 'Shared',
        subscriptionInitialPosition: 'Earliest',
        receiverQueueSize: batchSize * 2,
      })
    )
  );
  
  const metrics = new BenchmarkMetrics('Pulsar - Consumer');
  
  console.log(`Consuming with ${concurrency} concurrent consumers, batch size ${batchSize}`);
  
  metrics.start();
  
  let totalConsumed = 0;
  let lastMessageTime = Date.now();
  let running = true;
  
  // Run concurrent consumers
  const consumerTasks = consumers.map(async (consumer, consumerId) => {
    let localEmptyCount = 0;
    
    while (running && localEmptyCount < 20) {  // More tolerance per consumer
      try {
        const startNs = process.hrtime.bigint();
        
        // Batch receive for fair comparison with Queen/Kafka
        const messages = await consumer.batchReceive(1000);  // 1 second timeout
        
        if (messages && messages.length > 0) {
          // Ack each message in batch
          for (const msg of messages) {
            metrics.incrementReceived(msg.getData().length);
            await consumer.acknowledge(msg);
          }
          
          metrics.recordLatency(startNs);
          totalConsumed += messages.length;
          localEmptyCount = 0;
          lastMessageTime = Date.now();
          
          if (totalConsumed % 1000 === 0) {
            process.stdout.write(`\r  Consumed: ${totalConsumed.toLocaleString()} messages`);
          }
        } else {
          localEmptyCount++;
          // Stop if no messages received globally for 10 seconds
          if (Date.now() - lastMessageTime > 10000) {
            running = false;
          }
        }
      } catch (e) {
        if (e.message && e.message.includes('timeout')) {
          localEmptyCount++;
          // Stop if no messages received globally for 10 seconds
          if (Date.now() - lastMessageTime > 10000) {
            running = false;
          }
        } else {
          metrics.incrementErrors();
        }
      }
    }
  });
  
  await Promise.all(consumerTasks);
  
  metrics.stop();
  
  // Close all consumers
  await Promise.all(consumers.map(c => c.close()));
  
  console.log('');
  return metrics.printReport();
}

async function main() {
  console.log('╔════════════════════════════════════════════════════════════╗');
  console.log('║           Apache Pulsar Benchmark Suite                    ║');
  console.log('╚════════════════════════════════════════════════════════════╝');
  console.log(`\nConfiguration:`);
  console.log(`  Partitions: ${config.partitionCount}`);
  console.log(`  Batching: disabled`);
  console.log(`  Sync writes: ${config.durability.pulsarSyncWrites}`);
  console.log(`  Message size: ${config.messageSize} bytes`);
  
  // Start Docker metrics collection
  const dockerMetrics = new DockerMetrics(CONTAINER_NAMES.pulsar);
  dockerMetrics.start();
  
  const client = new Pulsar.Client({
    serviceUrl: config.endpoints.pulsar,
    operationTimeoutSeconds: 30,
  });
  
  const results = {
    system: 'Apache Pulsar',
    timestamp: new Date().toISOString(),
    config: {
      partitions: config.partitionCount,
      batchingEnabled: false,
      syncWrites: config.durability.pulsarSyncWrites,
      messageSize: config.messageSize,
    },
    benchmarks: {},
    dockerMetrics: null,
  };
  
  try {
    results.benchmarks.latency = await runLatencyBenchmark(client);
    results.benchmarks.throughput = await runThroughputBenchmark(client);
    results.benchmarks.partitionFanout = await runPartitionFanoutBenchmark(client);
    results.benchmarks.consumer = await runConsumerBenchmark(client);
  } catch (e) {
    console.error('Benchmark failed:', e);
  }
  
  await client.close();
  
  // Stop and report Docker metrics
  dockerMetrics.stop();
  results.dockerMetrics = dockerMetrics.printReport();
  
  // Save results
  const filename = `results-pulsar-${Date.now()}.json`;
  writeFileSync(filename, JSON.stringify(results, null, 2));
  console.log(`\nResults saved to ${filename}`);
  
  return results;
}

main().catch(console.error);
