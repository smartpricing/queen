#!/usr/bin/env node
/**
 * Test to verify partition type validation
 * This test should return a 400 error when partition is a number
 */

const http = require('http');

async function testPartitionValidation() {
    console.log('Testing partition type validation...\n');
    
    // Test 1: Partition as number (should return 400 error)
    console.log('Test 1: Sending partition as number (should return 400)');
    const result1 = await sendPushRequest({
        items: [
            {
                queue: 'test-queue',
                partition: 123,  // Number instead of string
                payload: { test: 'data' }
            }
        ]
    });
    
    if (result1.statusCode === 400) {
        console.log('✅ Test 1 PASSED: Got 400 error as expected');
        console.log('   Error message:', result1.body.error);
    } else {
        console.log('❌ Test 1 FAILED: Expected 400, got', result1.statusCode);
        console.log('   Response:', result1.body);
    }
    
    console.log();
    
    // Test 2: Partition as string (should succeed)
    console.log('Test 2: Sending partition as string (should succeed)');
    const result2 = await sendPushRequest({
        items: [
            {
                queue: 'test-queue',
                partition: '123',  // Correct: string
                payload: { test: 'data' }
            }
        ]
    });
    
    if (result2.statusCode === 201) {
        console.log('✅ Test 2 PASSED: Got 201 success as expected');
    } else {
        console.log('❌ Test 2 FAILED: Expected 201, got', result2.statusCode);
        console.log('   Response:', result2.body);
    }
    
    console.log();
    
    // Test 3: TransactionId as number (should return 400 error)
    console.log('Test 3: Sending transactionId as number (should return 400)');
    const result3 = await sendPushRequest({
        items: [
            {
                queue: 'test-queue',
                partition: 'default',
                transactionId: 456,  // Number instead of string
                payload: { test: 'data' }
            }
        ]
    });
    
    if (result3.statusCode === 400) {
        console.log('✅ Test 3 PASSED: Got 400 error as expected');
        console.log('   Error message:', result3.body.error);
    } else {
        console.log('❌ Test 3 FAILED: Expected 400, got', result3.statusCode);
        console.log('   Response:', result3.body);
    }
    
    console.log();
    
    // Test 4: Queue as number (should return 400 error)
    console.log('Test 4: Sending queue as number (should return 400)');
    const result4 = await sendPushRequest({
        items: [
            {
                queue: 789,  // Number instead of string
                partition: 'default',
                payload: { test: 'data' }
            }
        ]
    });
    
    if (result4.statusCode === 400) {
        console.log('✅ Test 4 PASSED: Got 400 error as expected');
        console.log('   Error message:', result4.body.error);
    } else {
        console.log('❌ Test 4 FAILED: Expected 400, got', result4.statusCode);
        console.log('   Response:', result4.body);
    }
    
    console.log('\nAll tests completed!');
}

function sendPushRequest(body) {
    return new Promise((resolve, reject) => {
        const bodyStr = JSON.stringify(body);
        
        const options = {
            hostname: 'localhost',
            port: 9876,
            path: '/api/v1/push',
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Content-Length': Buffer.byteLength(bodyStr)
            }
        };
        
        const req = http.request(options, (res) => {
            let data = '';
            
            res.on('data', (chunk) => {
                data += chunk;
            });
            
            res.on('end', () => {
                try {
                    const body = JSON.parse(data);
                    resolve({ statusCode: res.statusCode, body });
                } catch (e) {
                    resolve({ statusCode: res.statusCode, body: data });
                }
            });
        });
        
        req.on('error', (e) => {
            reject(e);
        });
        
        req.write(bodyStr);
        req.end();
    });
}

// Run tests
testPartitionValidation().catch(console.error);

