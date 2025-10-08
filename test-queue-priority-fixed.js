#!/usr/bin/env node

/**
 * Test for queue priority using direct database setup
 */

import { createQueenClient } from './src/client/queenClient.js';
import pg from 'pg';

const client = createQueenClient({
  baseUrl: 'http://localhost:6632'
});

// Database connection for direct setup
const pool = new pg.Pool({
  host: process.env.PG_HOST || 'localhost',
  port: process.env.PG_PORT || 5432,
  database: process.env.PG_DB || 'postgres',
  user: process.env.PG_USER || 'postgres',
  password: process.env.PG_PASSWORD || 'postgres'
});

async function setupQueuesWithNamespace() {
  console.log('🔧 Setting up queues with namespace and priorities...');
  
  try {
    // Create queues with namespace directly in database
    await pool.query(`
      INSERT INTO queen.queues (name, namespace, priority) 
      VALUES 
        ('test-high-priority', 'priority-test', 10),
        ('test-medium-priority', 'priority-test', 5),
        ('test-low-priority', 'priority-test', 1)
      ON CONFLICT (name) DO UPDATE SET 
        namespace = EXCLUDED.namespace,
        priority = EXCLUDED.priority
    `);
    
    // Create default partitions for these queues
    await pool.query(`
      INSERT INTO queen.partitions (queue_id, name, priority)
      SELECT q.id, 'Default', 0
      FROM queen.queues q
      WHERE q.name IN ('test-high-priority', 'test-medium-priority', 'test-low-priority')
      ON CONFLICT (queue_id, name) DO NOTHING
    `);
    
    // Verify setup
    const result = await pool.query(`
      SELECT q.name, q.namespace, q.priority as queue_priority, p.name as partition_name, p.priority as partition_priority
      FROM queen.queues q
      JOIN queen.partitions p ON p.queue_id = q.id
      WHERE q.namespace = 'priority-test'
      ORDER BY q.priority DESC
    `);
    
    console.log('✅ Queues setup completed:');
    result.rows.forEach(row => {
      console.log(`  ${row.name} (namespace: ${row.namespace}, queue priority: ${row.queue_priority}, partition: ${row.partition_name})`);
    });
    
  } catch (error) {
    console.error('❌ Error setting up queues:', error.message);
    throw error;
  }
}

async function testQueuePriority() {
  console.log('\n🧪 Testing Queue Priority Implementation...\n');

  try {
    // Setup queues first
    await setupQueuesWithNamespace();
    
    // Create messages in different queues
    console.log('\n📋 Creating messages in different priority queues...');
    
    const baseTime = Date.now();
    
    // Low priority queue (created first, should be processed last)
    await client.push({
      items: [{
        queue: 'test-low-priority',
        partition: 'Default',
        payload: { 
          message: 'Low priority queue message', 
          queuePriority: 1,
          timestamp: baseTime 
        }
      }]
    });

    // High priority queue (created second, should be processed first)  
    await client.push({
      items: [{
        queue: 'test-high-priority',
        partition: 'Default',
        payload: { 
          message: 'High priority queue message', 
          queuePriority: 10,
          timestamp: baseTime + 1000 
        }
      }]
    });

    // Medium priority queue (created third, should be processed second)
    await client.push({
      items: [{
        queue: 'test-medium-priority',
        partition: 'Default',
        payload: { 
          message: 'Medium priority queue message', 
          queuePriority: 5,
          timestamp: baseTime + 2000 
        }
      }]
    });

    console.log('✅ Test messages pushed to different queues\n');

    // Wait a moment for messages to be stored
    await new Promise(resolve => setTimeout(resolve, 200));

    // Pop messages using namespace filter (should get them in queue priority order)
    console.log('🔍 Popping messages with namespace filter (testing queue priority)...');
    
    const results = [];
    for (let i = 0; i < 3; i++) {
      const result = await client.pop({ 
        namespace: 'priority-test',
        batch: 1
      });
      
      if (result.messages && result.messages.length > 0) {
        const message = result.messages[0];
        results.push({
          queue: message.queue,
          queuePriority: message.queuePriority,
          partitionPriority: message.priority,
          data: message.data
        });
        
        console.log(`📨 Popped: ${message.data.message} from queue "${message.queue}" (queue priority: ${message.queuePriority || 'N/A'})`);
        
        // Acknowledge the message
        await client.ack(message.transactionId, 'completed');
      } else {
        console.log('❌ No more messages available');
        break;
      }
    }
    
    if (results.length > 0) {
      console.log('\n📊 Queue Priority Results:');
      console.log('Expected order: High (10) → Medium (5) → Low (1)');
      console.log('Actual order:');
      
      results.forEach((result, index) => {
        console.log(`  ${index + 1}. Queue: ${result.queue} (Queue Priority: ${result.queuePriority || 'N/A'})`);
      });

      // Check priority ordering
      let priorityCorrect = true;
      for (let i = 1; i < results.length; i++) {
        const prevPriority = results[i-1].queuePriority || 0;
        const currPriority = results[i].queuePriority || 0;
        
        if (currPriority > prevPriority) {
          priorityCorrect = false;
          console.log(`❌ Priority violation: message ${i+1} has higher queue priority (${currPriority}) than message ${i} (${prevPriority})`);
        }
      }
      
      if (priorityCorrect) {
        console.log('\n✅ Queue priority test PASSED! Messages processed in correct priority order.');
      } else {
        console.log('\n❌ Queue priority test FAILED! Messages not processed in priority order.');
      }
    } else {
      console.log('⚠️  No messages retrieved for queue priority test.');
    }

  } catch (error) {
    console.error('❌ Test failed with error:', error.message);
    console.error('Stack:', error.stack);
  }
}

// Run test
async function runTest() {
  console.log('🚀 Starting Queue Priority Test\n');
  console.log('Make sure the Queen server is running on localhost:6632\n');
  
  await testQueuePriority();
  
  console.log('\n🏁 Test completed!');
  await pool.end();
}

runTest().catch(console.error);
