#!/usr/bin/env node

import { Queen } from './src/client/index.js';

async function testBatchPush() {
    console.log('🔍 Testing batch push to see what client sends...');
    
    try {
        const client = new Queen({ baseUrl: 'http://localhost:6632' });
        
        console.log('1. Configuring queue...');
        await client.queue('batch-debug', {});
        
        console.log('2. Pushing batch to specific partition...');
        const messages = Array.from({ length: 3 }, (_, i) => ({ 
            message: `Batch ${i + 1}`, 
            index: i + 1 
        }));
        
        await client.push('batch-debug/test-partition', messages);
        console.log('✅ Batch push completed');
        
    } catch (error) {
        console.error('❌ Error:', error.message);
    }
}

testBatchPush();
