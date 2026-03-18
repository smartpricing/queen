// Setup script for Apache Pulsar Key_Shared benchmark
// Creates a single non-partitioned topic for virtual partitioning via keys

import { config } from '../config.js';

async function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function setup() {
  console.log('Setting up Apache Pulsar (Key_Shared mode)...');
  console.log(`Virtual partitions: ${config.partitionCount} (via message keys)`);
  console.log('Physical partitions: 1');
  
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
  
  // Delete existing partitioned topic if exists
  try {
    console.log('Cleaning up any existing topic...');
    await fetch(`${adminUrl}/admin/v2/persistent/public/benchmark/benchmark-keyshared?force=true&deleteSchema=true`, {
      method: 'DELETE',
    });
    await fetch(`${adminUrl}/admin/v2/persistent/public/benchmark/benchmark-keyshared/partitions?force=true&deleteSchema=true`, {
      method: 'DELETE',
    });
    await sleep(2000);
  } catch (e) {
    // Topic might not exist
  }
  
  // Create non-partitioned topic (single partition)
  // For non-partitioned topics, we just need to ensure it exists by producing to it
  // or we can create it explicitly
  console.log('Creating non-partitioned topic...');
  
  try {
    // Create topic by setting retention policy (this creates the topic)
    const createResponse = await fetch(
      `${adminUrl}/admin/v2/persistent/public/benchmark/benchmark-keyshared`,
      {
        method: 'PUT',
      }
    );
    
    if (createResponse.ok || createResponse.status === 409) {
      console.log('Topic created (or already exists)');
    } else {
      const text = await createResponse.text();
      console.log(`Topic creation response: ${createResponse.status} ${text}`);
    }
  } catch (e) {
    console.log(`Note: ${e.message}`);
  }
  
  // Wait for topic to be ready
  console.log('Waiting for topic to be ready...');
  await sleep(2000);
  
  const elapsed = Date.now() - startTime;
  console.log(`\nPulsar Key_Shared setup complete in ${(elapsed / 1000).toFixed(1)}s`);
  console.log(`Topic 'benchmark-keyshared' ready (non-partitioned)`);
  console.log(`Will use ${config.partitionCount} virtual partition keys`);
}

setup().catch(console.error);
