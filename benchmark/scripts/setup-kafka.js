// Setup script for Apache Kafka
// Creates a topic with 5000 partitions

import { Kafka, logLevel } from 'kafkajs';
import { config } from '../config.js';

const kafka = new Kafka({
  clientId: 'benchmark-setup',
  brokers: [config.endpoints.kafka],
  logLevel: logLevel.WARN,
});

const admin = kafka.admin();

async function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function waitForTopicReady(topicName, expectedPartitions, maxRetries = 30) {
  console.log('Waiting for topic to be ready...');
  
  for (let i = 0; i < maxRetries; i++) {
    try {
      const metadata = await admin.fetchTopicMetadata({ topics: [topicName] });
      const topic = metadata.topics[0];
      
      if (topic && topic.partitions) {
        const readyPartitions = topic.partitions.filter(p => p.leader >= 0).length;
        process.stdout.write(`\r  Partitions ready: ${readyPartitions}/${expectedPartitions}`);
        
        if (readyPartitions >= expectedPartitions) {
          console.log(' ✓');
          return topic.partitions.length;
        }
      }
    } catch (e) {
      // Metadata not ready yet, retry
      process.stdout.write(`\r  Waiting for metadata... (attempt ${i + 1}/${maxRetries})`);
    }
    
    await sleep(2000);
  }
  
  throw new Error(`Topic not ready after ${maxRetries * 2} seconds`);
}

async function setup() {
  console.log('Setting up Apache Kafka...');
  console.log(`Creating topic with ${config.partitionCount} partitions...`);
  console.log('(This may take a minute for 5000 partitions)\n');
  
  const startTime = Date.now();
  
  await admin.connect();
  
  // Delete existing topic if present
  const existingTopics = await admin.listTopics();
  if (existingTopics.includes('benchmark')) {
    console.log('Deleting existing benchmark topic...');
    await admin.deleteTopics({ topics: ['benchmark'] });
    // Wait for deletion to complete
    console.log('Waiting for deletion to complete...');
    await sleep(10000);
  }
  
  // Create topic with 5000 partitions
  console.log('Creating topic...');
  try {
    await admin.createTopics({
      topics: [{
        topic: 'benchmark',
        numPartitions: config.partitionCount,
        replicationFactor: 1,
        configEntries: [
          { name: 'retention.ms', value: '3600000' },  // 1 hour
          { name: 'min.insync.replicas', value: '1' },
        ],
      }],
      waitForLeaders: true,
      timeout: 120000,  // 2 minute timeout for large partition count
    });
  } catch (e) {
    // Topic might already exist or creation in progress
    console.log('Topic creation returned:', e.message);
    console.log('Checking if topic exists anyway...');
  }
  
  // Wait for all partitions to have leaders
  const partitionCount = await waitForTopicReady('benchmark', config.partitionCount);
  
  await admin.disconnect();
  
  const elapsed = Date.now() - startTime;
  console.log(`\nKafka setup complete in ${(elapsed / 1000).toFixed(1)}s`);
  console.log(`Created topic 'benchmark' with ${partitionCount} partitions`);
  
  if (partitionCount !== config.partitionCount) {
    console.warn(`WARNING: Expected ${config.partitionCount} partitions, got ${partitionCount}`);
  }
}

setup().catch(console.error);
