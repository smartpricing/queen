/**
 * Test written by an human
 */

import { startTest, passTest, failTest, sleep, log } from './utils.js';

/**
 * Test: Pipeline API - Basic Processing
 */
export async function testCreateQueue(client) {
    startTest('Create Queue', 'human');

    try {
        const queue = 'test-create-queue-hm';
        await client.queue(queue, { leaseTime: 30 });
        passTest('Queue created successfully');
    } catch (error) {
        failTest('Create Queue', error  );
    }
}

export async function workerQueueTest(client) {
    startTest('Worker Queue', 'human');

    try {
        const queue = 'test-worker-queue-hm';
        const processedQueue = queue + '-processed';
        await client.queueDelete(queue);
        await client.queueDelete(processedQueue); 
        await client.queue(queue, { leaseTime: 30 });
        await client.queue(processedQueue, { leaseTime: 30 });
        const pushMessages = async (count) => {
            let messages = [];
            for (let i = 0; i < count; i++) {
                messages.push({ id: i, data: `Message ${i}` });
            }
            await client.push(queue, messages); 
        }


        // Test 1
        await pushMessages(10);

        let counter1 = 0;
        await client
        .pipeline(queue)
        .take(10)
        .process(async (message) => {
            counter1++;
            return { id: message.data.id, data: message.data.data };
        })
        .execute();

        if (counter1 !== 10) {
            throw new Error('Expected 10 messages to be processed, got ' + counter1);
        }

        // Test 2
        await pushMessages(100); 

        let counter2 = 0;
        await client 
        .pipeline(queue)
        .take(10)
        .processBatch(async (messages) => {
            counter2 += messages.length;
            return messages;
        })
        .repeat({ maxIterations: 1, continuous: false })
        .execute();

        if (counter2 !== 10) {
            throw new Error('Expected 10 messages to be processed, got ' + counter2);
        }

        // Test 3
        let counter3 = 0;
        await client 
        .pipeline(queue)
        .take(10)
        .processBatch(async (messages) => {
            counter3 += messages.length;
            return messages;
        })
        .repeat({ maxIterations: 9, continuous: false })
        .execute();        

        if (counter3 !== 90) {
            throw new Error('Expected 90 messages to be processed, got ' + counter3);
        }

        await pushMessages(100);

        await client
        .pipeline(queue)
        .take(10)
        .processBatch(async (messages) => {
            return messages;
        })
        .atomically((tx, originalMessages, processedMessages) => {
            tx.ack(originalMessages);
        })
        .repeat({ maxIterations: 10, continuous: true })
        .execute();

        const messagesPopped = await client.takeSingleBatch(queue);
        let counter4 = messagesPopped.length;
        if (counter4 !== 0) {
            throw new Error('Expected 0 message to be processed, got ' + counter4);
        }

        // Test transaction that works correctly
        await pushMessages(100);

        await client
        .pipeline(queue)
        .take(10)
        .processBatch(async (messages) => {
            return messages;
        })
        .atomically((tx, originalMessages, processedMessages) => {
            tx.ack(originalMessages);
            tx.push(processedQueue, processedMessages);
        })
        .repeat({ maxIterations: 10, continuous: true })
        .execute();

        const messagesPopped2 = [];
        for await (const messages of client.takeBatch(processedQueue, { batch: 100 })) {
            messagesPopped2.push(...messages);
        }
        if (messagesPopped2.length !== 100) {
            throw new Error('Expected 100 message to be processed, got ' + counter5);
        }

        // Test transaction that fails
        
        await pushMessages(100);

        try {
        await client
        .pipeline(queue)
        .take(10)
        .processBatch(async (messages) => {
            return messages;
        })
        .atomically((tx, originalMessages, processedMessages) => {
            originalMessages.map(x => x.leaseId = 'invalid-lease-id-12345');
            tx.ack(originalMessages);
            tx.push(processedQueue, processedMessages);
        })
        .repeat({ maxIterations: 10, continuous: true })
        .execute();
        failTest('Expected transaction to fail, got ' + error);
        } catch (error) {}

        passTest('Worker Queue test passed');
    } catch (error) {
        console.error(error);
        failTest('Worker Queue test failed', error);
    }
}