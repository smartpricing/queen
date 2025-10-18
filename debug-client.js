#!/usr/bin/env node

// Quick debug script to test C++ server compatibility
import { Queen } from './src/client/index.js';

async function debugTest() {
    console.log('🔍 Debug Test - Testing C++ Server Compatibility');
    
    try {
        const client = new Queen({
            baseUrl: 'http://localhost:6632'
        });
        
        console.log('1. Configuring queue...');
        await client.queue('debug-test', {});
        console.log('✅ Queue configured');
        
        console.log('2. Pushing message...');
        await client.push('debug-test', { message: 'debug test', timestamp: Date.now() });
        console.log('✅ Message pushed');
        
        console.log('3. Taking message...');
        let messageReceived = false;
        for await (const msg of client.take('debug-test', { limit: 1 })) {
            console.log('📦 Received message:', JSON.stringify(msg, null, 2));
            
            if (msg.data && msg.data.message === 'debug test') {
                messageReceived = true;
                console.log('4. Acknowledging message...');
                await client.ack(msg);
                console.log('✅ Message acknowledged');
            }
            break;
        }
        
        if (!messageReceived) {
            console.log('❌ Failed to receive message');
        } else {
            console.log('🎉 All operations successful!');
        }
        
    } catch (error) {
        console.error('❌ Error:', error.message);
        console.error('Stack:', error.stack);
    }
}

debugTest();
