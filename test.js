#!/usr/bin/env node
import fs from 'fs/promises';
import path from 'path';

const API_BASE = 'http://localhost:6632/api/v1';
const TEST_NS = 'test';
const TEST_TASK = 'benchmark';
const TEST_QUEUE = 'items';
const OUTPUT_FILE = 'consumed_items.json';

// Helper to make HTTP requests
const request = async (url, options = {}) => {
  const response = await fetch(url, {
    ...options,
    headers: {
      'Content-Type': 'application/json',
      ...options.headers
    }
  });
  
  if (!response.ok && response.status !== 204) {
    throw new Error(`HTTP ${response.status}: ${await response.text()}`);
  }
  
  return response.status === 204 ? null : response.json();
};

// Produce 1000 items
const produce = async () => {
  console.log('Starting producer...');
  console.log(`Pushing 1000 items to queue: ${TEST_NS}/${TEST_TASK}/${TEST_QUEUE}`);
  
  const batchSize = 100; // Push in batches of 100
  const totalItems = 1000;
  let pushed = 0;
  
  const startTime = Date.now();
  
  for (let batch = 0; batch < totalItems / batchSize; batch++) {
    const items = [];
    
    for (let i = 0; i < batchSize; i++) {
      const itemNumber = batch * batchSize + i + 1;
      items.push({
        ns: TEST_NS,
        task: TEST_TASK,
        queue: TEST_QUEUE,
        payload: {
          id: itemNumber,
          data: `Item ${itemNumber}`,
          timestamp: new Date().toISOString(),
          batch: batch + 1
        }
      });
    }
    
    const result = await request(`${API_BASE}/push`, {
      method: 'POST',
      body: JSON.stringify({ items })
    });
    
    pushed += result.messages.length;
    console.log(`Pushed batch ${batch + 1}/10 (${pushed} total items)`);
  }
  
  const elapsed = Date.now() - startTime;
  console.log(`\n✅ Successfully pushed ${pushed} items in ${elapsed}ms`);
  console.log(`Throughput: ${Math.round(pushed / (elapsed / 1000))} items/second`);
};

// Consume items from queue
const consume = async () => {
  console.log('Starting consumer...');
  console.log(`Consuming from queue: ${TEST_NS}/${TEST_TASK}/${TEST_QUEUE}`);
  
  const consumed = [];
  const startTime = Date.now();
  let totalConsumed = 0;
  let emptyPolls = 0;
  const maxEmptyPolls = 10; // Stop after 10 empty polls
  
  while (emptyPolls < maxEmptyPolls) {
    try {
      // Pop in batches for efficiency
      const result = await request(
        `${API_BASE}/pop/ns/${TEST_NS}/task/${TEST_TASK}/queue/${TEST_QUEUE}?batch=10`
      );
      
      if (result && result.messages && result.messages.length > 0) {
        emptyPolls = 0; // Reset empty poll counter
        
        for (const message of result.messages) {
          consumed.push({
            transactionId: message.transactionId,
            payload: message.payload,
            queue: message.queue,
            createdAt: message.createdAt
          });
          
          // Acknowledge the message
          await request(`${API_BASE}/ack`, {
            method: 'POST',
            body: JSON.stringify({
              transactionId: message.transactionId,
              status: 'completed'
            })
          });
        }
        
        totalConsumed += result.messages.length;
        process.stdout.write(`\rConsumed: ${totalConsumed} items`);
      } else {
        emptyPolls++;
        if (emptyPolls < maxEmptyPolls) {
          await new Promise(resolve => setTimeout(resolve, 100));
        }
      }
    } catch (error) {
      console.error('\nError consuming:', error.message);
      emptyPolls++;
      await new Promise(resolve => setTimeout(resolve, 1000));
    }
  }
  
  const elapsed = Date.now() - startTime;
  console.log(`\n\n✅ Successfully consumed ${totalConsumed} items in ${elapsed}ms`);
  console.log(`Throughput: ${Math.round(totalConsumed / (elapsed / 1000))} items/second`);
  
  // Sort by ID to verify order
  consumed.sort((a, b) => a.payload.id - b.payload.id);
  
  // Write to file
  await fs.writeFile(OUTPUT_FILE, JSON.stringify(consumed, null, 2));
  console.log(`\n📁 Consumed items saved to ${OUTPUT_FILE}`);
  
  // Verify order
  let inOrder = true;
  for (let i = 1; i < consumed.length; i++) {
    if (consumed[i].payload.id !== consumed[i - 1].payload.id + 1) {
      inOrder = false;
      console.log(`⚠️  Order issue: Item ${consumed[i - 1].payload.id} followed by ${consumed[i].payload.id}`);
    }
  }
  
  if (inOrder && consumed.length > 0) {
    console.log('✅ All items were processed in order');
  }
  
  // Check for missing items
  if (consumed.length > 0) {
    const ids = new Set(consumed.map(item => item.payload.id));
    const missing = [];
    for (let i = 1; i <= Math.max(...ids); i++) {
      if (!ids.has(i)) {
        missing.push(i);
      }
    }
    
    if (missing.length > 0) {
      console.log(`⚠️  Missing items: ${missing.join(', ')}`);
    } else {
      console.log('✅ No missing items in sequence');
    }
  }
};

// Main
const main = async () => {
  const command = process.argv[2];
  
  if (command === 'produce') {
    await produce();
  } else if (command === 'consume') {
    await consume();
  } else {
    console.log('Usage: node test.js [produce|consume]');
    console.log('  produce - Push 1000 items to the test queue');
    console.log('  consume - Consume items from the test queue and save to file');
    process.exit(1);
  }
};

main().catch(error => {
  console.error('Error:', error);
  process.exit(1);
});
