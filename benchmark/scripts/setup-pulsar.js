// Setup script for Apache Pulsar
// Creates partitioned topic with configurable partition count

import Pulsar from 'pulsar-client';
import { config } from '../config.js';

async function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function setup() {
  console.log('Setting up Apache Pulsar...');
  console.log(`Target: ${config.partitionCount} partitions`);
  
  const startTime = Date.now();
  const adminUrl = config.endpoints.pulsarAdmin;
  
  // Create namespace if not exists
  try {
    const nsResponse = await fetch(`${adminUrl}/admin/v2/namespaces/public/benchmark`, {
      method: 'PUT',
    });
    if (nsResponse.ok) {
      console.log('Created namespace public/benchmark');
    }
  } catch (e) {
    // Namespace might already exist
  }
  
  // Check if topic exists and get current partition count
  let currentPartitions = 0;
  try {
    const checkResponse = await fetch(
      `${adminUrl}/admin/v2/persistent/public/benchmark/benchmark/partitions`
    );
    if (checkResponse.ok) {
      const data = await checkResponse.json();
      currentPartitions = typeof data === 'number' ? data : (data.partitions || 0);
      console.log(`Existing topic found with ${currentPartitions} partitions`);
    }
  } catch (e) {
    // Topic doesn't exist
  }
  
  if (currentPartitions > 0 && currentPartitions < config.partitionCount) {
    // Update partition count (Pulsar supports increasing partitions)
    console.log(`Updating partition count from ${currentPartitions} to ${config.partitionCount}...`);
    const updateResponse = await fetch(
      `${adminUrl}/admin/v2/persistent/public/benchmark/benchmark/partitions`,
      {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(config.partitionCount),
      }
    );
    
    if (!updateResponse.ok) {
      const text = await updateResponse.text();
      console.log(`Update failed: ${updateResponse.status} ${text}`);
      console.log('Attempting to delete and recreate...');
      
      // Force delete
      await fetch(`${adminUrl}/admin/v2/persistent/public/benchmark/benchmark/partitions?force=true&deleteSchema=true`, {
        method: 'DELETE',
      });
      console.log('Deleted topic, waiting...');
      await sleep(10000);
      currentPartitions = 0;
    } else {
      console.log('Partition count updated successfully');
    }
  } else if (currentPartitions === 0) {
    // Create new topic
    console.log(`Creating partitioned topic with ${config.partitionCount} partitions...`);
    const createResponse = await fetch(
      `${adminUrl}/admin/v2/persistent/public/benchmark/benchmark/partitions`,
      {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(config.partitionCount),
      }
    );
    
    if (!createResponse.ok && createResponse.status !== 409) {
      const text = await createResponse.text();
      throw new Error(`Failed to create topic: ${createResponse.status} ${text}`);
    }
  } else {
    console.log(`Topic already has ${currentPartitions} partitions (>= ${config.partitionCount})`);
  }
  
  // Wait for topic to be ready
  console.log('Waiting for topic to be ready...');
  await sleep(3000);
  
  // Verify final partition count
  const verifyResponse = await fetch(
    `${adminUrl}/admin/v2/persistent/public/benchmark/benchmark/partitions`
  );
  const partitionData = await verifyResponse.json();
  const partitionCount = typeof partitionData === 'number' 
    ? partitionData 
    : (partitionData.partitions || 0);
  
  const elapsed = Date.now() - startTime;
  console.log(`\nPulsar setup complete in ${(elapsed / 1000).toFixed(1)}s`);
  console.log(`Topic 'benchmark' has ${partitionCount} partitions`);
  
  if (partitionCount < config.partitionCount) {
    console.warn(`\n⚠️  WARNING: Only ${partitionCount} partitions available (wanted ${config.partitionCount})`);
    console.warn('You may need to restart Pulsar and run setup again.');
  }
}

setup().catch(console.error);
