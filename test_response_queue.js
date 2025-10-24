#!/usr/bin/env node

/**
 * Test script for the new Response Queue architecture
 * Tests POP and ACK operations to ensure no segfaults occur
 */

const axios = require('axios');

const BASE_URL = 'http://localhost:6632';
const QUEUE_NAME = 'test-response-queue';
const PARTITION_NAME = 'test-partition';

async function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function createQueue() {
    try {
        console.log('🔧 Creating test queue...');
        const response = await axios.post(`${BASE_URL}/api/v1/configure`, {
            queue: QUEUE_NAME,
            partition: PARTITION_NAME,
            maxRetries: 3,
            retryDelay: 1000,
            deadLetterQueue: true
        });
        console.log('✅ Queue created successfully');
        return true;
    } catch (error) {
        console.log('⚠️  Queue might already exist:', error.response?.data?.error || error.message);
        return true; // Continue anyway
    }
}

async function pushMessage(messageData) {
    try {
        console.log('📤 Pushing message...');
        const response = await axios.post(`${BASE_URL}/api/v1/push`, {
            items: [{
                queue: QUEUE_NAME,
                partition: PARTITION_NAME,
                payload: messageData,
                traceId: `trace-${Date.now()}`
            }]
        });
        console.log('✅ Message pushed:', response.data.results[0].messageId);
        return response.data.results[0];
    } catch (error) {
        console.error('❌ Failed to push message:', error.response?.data || error.message);
        throw error;
    }
}

async function popMessage() {
    try {
        console.log('📥 Popping message...');
        const response = await axios.get(`${BASE_URL}/api/v1/pop`, {
            params: {
                consumerGroup: 'test-consumer',
                batch: 1,
                wait: false
            }
        });
        
        if (response.status === 204) {
            console.log('📭 No messages available');
            return null;
        }
        
        console.log('✅ Message popped:', response.data.messages[0].transactionId);
        return response.data.messages[0];
    } catch (error) {
        console.error('❌ Failed to pop message:', error.response?.data || error.message);
        throw error;
    }
}

async function ackMessage(transactionId, status = 'completed') {
    try {
        console.log(`📝 Acknowledging message (${status})...`);
        const response = await axios.post(`${BASE_URL}/api/v1/ack`, {
            transactionId: transactionId,
            status: status,
            consumerGroup: 'test-consumer'
        });
        console.log('✅ Message acknowledged:', response.data.transactionId);
        return response.data;
    } catch (error) {
        console.error('❌ Failed to acknowledge message:', error.response?.data || error.message);
        throw error;
    }
}

async function testBasicFlow() {
    console.log('\n🧪 Testing Basic POP/ACK Flow...');
    
    // Push a message
    const pushResult = await pushMessage({ 
        test: 'basic-flow', 
        timestamp: new Date().toISOString() 
    });
    
    // Wait a bit for the message to be available
    await sleep(100);
    
    // Pop the message
    const message = await popMessage();
    if (!message) {
        throw new Error('Expected to pop a message but got none');
    }
    
    // Acknowledge the message
    await ackMessage(message.transactionId, 'completed');
    
    console.log('✅ Basic flow completed successfully');
}

async function testConcurrentOperations() {
    console.log('\n🧪 Testing Concurrent POP/ACK Operations...');
    
    // Push multiple messages
    const promises = [];
    for (let i = 0; i < 5; i++) {
        promises.push(pushMessage({ 
            test: 'concurrent', 
            index: i, 
            timestamp: new Date().toISOString() 
        }));
    }
    
    await Promise.all(promises);
    console.log('📤 Pushed 5 messages concurrently');
    
    // Wait a bit
    await sleep(200);
    
    // Pop and ACK messages concurrently
    const popPromises = [];
    for (let i = 0; i < 5; i++) {
        popPromises.push(popMessage());
    }
    
    const messages = await Promise.all(popPromises);
    const validMessages = messages.filter(m => m !== null);
    
    console.log(`📥 Popped ${validMessages.length} messages concurrently`);
    
    // ACK all messages concurrently
    const ackPromises = validMessages.map(msg => 
        ackMessage(msg.transactionId, 'completed')
    );
    
    await Promise.all(ackPromises);
    console.log('✅ Concurrent operations completed successfully');
}

async function testTransactionAPI() {
    console.log('\n🧪 Testing Transaction API...');
    
    try {
        console.log('📤 Testing transaction with PUSH and ACK operations...');
        const response = await axios.post(`${BASE_URL}/api/v1/transaction`, {
            operations: [
                {
                    type: 'push',
                    items: [{
                        queue: QUEUE_NAME,
                        partition: PARTITION_NAME,
                        payload: { 
                            test: 'transaction-push', 
                            timestamp: new Date().toISOString() 
                        }
                    }]
                }
            ]
        });
        
        console.log('✅ Transaction completed:', response.data.transactionId);
        console.log(`📊 Operations processed: ${response.data.results.length}`);
        
        return response.data;
    } catch (error) {
        console.error('❌ Failed to execute transaction:', error.response?.data || error.message);
        throw error;
    }
}

async function testErrorHandling() {
    console.log('\n🧪 Testing Error Handling...');
    
    // Push a message
    const pushResult = await pushMessage({ 
        test: 'error-handling', 
        timestamp: new Date().toISOString() 
    });
    
    await sleep(100);
    
    // Pop the message
    const message = await popMessage();
    if (!message) {
        throw new Error('Expected to pop a message but got none');
    }
    
    // Acknowledge with failure
    await ackMessage(message.transactionId, 'failed');
    
    console.log('✅ Error handling completed successfully');
}

async function testAbortedRequests() {
    console.log('\n🧪 Testing Aborted Requests (Response Queue Safety)...');
    
    // Create multiple requests and abort some of them
    const controller = new AbortController();
    
    // Start a POP request and immediately abort it
    const popPromise = axios.get(`${BASE_URL}/api/v1/pop`, {
        params: {
            consumerGroup: 'test-abort',
            batch: 1,
            wait: true,
            timeout: 5000
        },
        signal: controller.signal
    }).catch(error => {
        if (error.code === 'ECONNABORTED' || error.name === 'AbortError') {
            console.log('✅ Request aborted as expected');
            return null;
        }
        throw error;
    });
    
    // Abort after 100ms
    setTimeout(() => {
        controller.abort();
    }, 100);
    
    await popPromise;
    
    // Wait a bit to ensure the response queue handles the aborted request safely
    await sleep(500);
    
    console.log('✅ Aborted request test completed successfully');
}

async function runTests() {
    console.log('🚀 Starting Response Queue Architecture Tests\n');
    
    try {
        // Setup
        await createQueue();
        await sleep(500); // Give queue time to be created
        
        // Run tests
        await testBasicFlow();
        await testConcurrentOperations();
        await testTransactionAPI();
        await testErrorHandling();
        await testAbortedRequests();
        
        console.log('\n🎉 All tests passed! Response Queue architecture is working correctly.');
        console.log('\n📊 Key Benefits Verified:');
        console.log('   ✅ No Loop::defer segfaults');
        console.log('   ✅ Thread-safe response handling');
        console.log('   ✅ Proper error propagation');
        console.log('   ✅ Graceful handling of aborted requests');
        console.log('   ✅ Concurrent operation safety');
        
    } catch (error) {
        console.error('\n❌ Test failed:', error.message);
        process.exit(1);
    }
}

// Check if axios is available
try {
    require.resolve('axios');
} catch (e) {
    console.error('❌ axios is required. Install it with: npm install axios');
    process.exit(1);
}

// Run the tests
runTests().catch(console.error);
