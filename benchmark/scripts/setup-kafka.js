// Setup script for Apache Kafka
// Uses Kafka CLI inside the container for reliable topic creation
// Creates topic incrementally if needed for large partition counts

import { execSync } from 'child_process';
import { config } from '../config.js';

const CONTAINER = 'kafka-broker';
const BOOTSTRAP = 'localhost:9092';
const TOPIC = 'benchmark';
const BATCH_SIZE = 2000;  // Create partitions in batches

function exec(cmd, options = {}) {
  if (!options.quiet) {
    console.log(`  $ ${cmd.substring(0, 120)}${cmd.length > 120 ? '...' : ''}`);
  }
  try {
    const result = execSync(cmd, { 
      encoding: 'utf-8',
      timeout: 600000,
      ...options 
    });
    return result.trim();
  } catch (e) {
    if (options.ignoreError) {
      return e.stdout?.trim() || e.stderr?.trim() || '';
    }
    throw e;
  }
}

function kafkaCmd(cmd) {
  return `docker exec ${CONTAINER} /opt/kafka/bin/${cmd}`;
}

async function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function waitForBroker(maxRetries = 30) {
  console.log('Waiting for Kafka broker to be ready...');
  
  for (let i = 0; i < maxRetries; i++) {
    try {
      const result = exec(
        kafkaCmd(`kafka-broker-api-versions.sh --bootstrap-server ${BOOTSTRAP}`),
        { ignoreError: true, quiet: true }
      );
      if (result.includes('ApiVersion')) {
        console.log('  Broker is ready ✓');
        return true;
      }
    } catch (e) {
      // ignore
    }
    process.stdout.write(`\r  Waiting... (attempt ${i + 1}/${maxRetries})`);
    await sleep(2000);
  }
  throw new Error('Broker not ready after ' + maxRetries * 2 + ' seconds');
}

async function getCurrentPartitionCount() {
  try {
    const result = exec(
      kafkaCmd(`kafka-topics.sh --bootstrap-server ${BOOTSTRAP} --describe --topic ${TOPIC}`),
      { ignoreError: true, quiet: true }
    );
    const lines = result.split('\n').filter(line => line.includes('Partition:'));
    return lines.length;
  } catch (e) {
    return 0;
  }
}

async function setup() {
  console.log('Setting up Apache Kafka...');
  console.log(`Target: ${config.partitionCount} partitions\n`);
  
  const startTime = Date.now();
  
  // Wait for broker
  await waitForBroker();
  
  // Check if topic exists and delete it
  console.log('\nChecking for existing topic...');
  const listResult = exec(
    kafkaCmd(`kafka-topics.sh --bootstrap-server ${BOOTSTRAP} --list`),
    { ignoreError: true }
  );
  
  if (listResult.includes(TOPIC)) {
    console.log('Deleting existing topic...');
    exec(kafkaCmd(`kafka-topics.sh --bootstrap-server ${BOOTSTRAP} --delete --topic ${TOPIC}`));
    console.log('Waiting for deletion to propagate...');
    await sleep(10000);
  }
  
  // Create topic with initial batch of partitions
  const targetPartitions = config.partitionCount;
  const initialPartitions = Math.min(BATCH_SIZE, targetPartitions);
  
  console.log(`\nCreating topic '${TOPIC}' with ${initialPartitions} initial partitions...`);
  
  try {
    exec(kafkaCmd(
      `kafka-topics.sh --bootstrap-server ${BOOTSTRAP} --create ` +
      `--topic ${TOPIC} ` +
      `--partitions ${initialPartitions} ` +
      `--replication-factor 1 ` +
      `--config retention.ms=3600000 ` +
      `--config min.insync.replicas=1`
    ));
    console.log('Initial topic created ✓');
  } catch (e) {
    console.error('Topic creation failed:', e.message);
    process.exit(1);
  }
  
  // Wait for initial partitions
  await sleep(3000);
  
  // Add more partitions in batches if needed
  let currentPartitions = await getCurrentPartitionCount();
  console.log(`Current partition count: ${currentPartitions}`);
  
  while (currentPartitions < targetPartitions) {
    const nextTarget = Math.min(currentPartitions + BATCH_SIZE, targetPartitions);
    console.log(`\nAdding partitions: ${currentPartitions} -> ${nextTarget}...`);
    
    try {
      exec(kafkaCmd(
        `kafka-topics.sh --bootstrap-server ${BOOTSTRAP} --alter ` +
        `--topic ${TOPIC} ` +
        `--partitions ${nextTarget}`
      ));
      
      // Wait for partitions to be ready
      await sleep(5000);
      currentPartitions = await getCurrentPartitionCount();
      console.log(`  Partitions now: ${currentPartitions}`);
      
      if (currentPartitions < nextTarget) {
        console.log('  Waiting for leader election...');
        await sleep(10000);
        currentPartitions = await getCurrentPartitionCount();
      }
    } catch (e) {
      console.error('Failed to add partitions:', e.message);
      break;
    }
  }
  
  // Final verification
  console.log('\nVerifying topic...');
  await sleep(3000);
  
  const describeResult = exec(
    kafkaCmd(`kafka-topics.sh --bootstrap-server ${BOOTSTRAP} --describe --topic ${TOPIC}`)
  );
  
  const partitionLines = describeResult.split('\n').filter(line => line.includes('Partition:'));
  const partitionCount = partitionLines.length;
  
  const elapsed = Date.now() - startTime;
  console.log(`\nKafka setup complete in ${(elapsed / 1000).toFixed(1)}s`);
  console.log(`Created topic '${TOPIC}' with ${partitionCount} partitions`);
  
  if (partitionCount < config.partitionCount) {
    console.warn(`\nWARNING: Only ${partitionCount} of ${config.partitionCount} partitions were created`);
    console.warn('This is a known limitation of single-broker Kafka.');
    console.warn('Consider using a multi-broker cluster for 10k+ partitions.');
  }
  
  console.log('\nTopic details:');
  console.log(describeResult.split('\n')[0]);
}

setup().catch(e => {
  console.error('Setup failed:', e.message);
  process.exit(1);
});
