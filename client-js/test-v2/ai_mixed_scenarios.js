/**
 * Mixed Scenario Tests
 * Complex tests combining multiple features
 */

export async function testMultipleQueuesSimultaneous(client) {
    // Create multiple queues with different configurations
    await client.queue('test-queue-v2-mixed-1')
        .config({ priority: 1, leaseTime: 60 })
        .create()
    
    await client.queue('test-queue-v2-mixed-2')
        .config({ priority: 5, retryLimit: 5 })
        .create()
    
    await client.queue('test-queue-v2-mixed-3')
        .config({ priority: 10, delayedProcessing: 1 })
        .create()

    // Push to all queues
    await Promise.all([
        client.queue('test-queue-v2-mixed-1').push([{ data: { queue: 1 } }]),
        client.queue('test-queue-v2-mixed-2').push([{ data: { queue: 2 } }]),
        client.queue('test-queue-v2-mixed-3').push([{ data: { queue: 3 } }])
    ])

    // Wait for delayed queue
    await new Promise(resolve => setTimeout(resolve, 1500))

    // Pop from all
    const results = await Promise.all([
        client.queue('test-queue-v2-mixed-1').batch(1).wait(false).pop(),
        client.queue('test-queue-v2-mixed-2').batch(1).wait(false).pop(),
        client.queue('test-queue-v2-mixed-3').batch(1).wait(false).pop()
    ])

    const totalMessages = results.reduce((sum, msgs) => sum + msgs.length, 0)

    return { 
        success: totalMessages === 3,
        message: `Multiple queues working: ${totalMessages} messages` 
    }
}

export async function testCrossQueueWorkflow(client) {
    // Setup: 3-stage processing pipeline
    await client.queue('test-queue-v2-workflow-stage1').create()
    await client.queue('test-queue-v2-workflow-stage2').create()
    await client.queue('test-queue-v2-workflow-stage3').create()

    // Stage 1: Initial message
    await client.queue('test-queue-v2-workflow-stage1')
        .push([{ data: { stage: 1, value: 10 } }])

    // Process stage 1 -> stage 2
    const msg1 = await client.queue('test-queue-v2-workflow-stage1')
        .batch(1).wait(false).pop()
    
    if (msg1.length === 0) {
        return { success: false, message: 'Stage 1 failed' }
    }

    await client.transaction()
        .queue('test-queue-v2-workflow-stage2')
        .push([{ data: { stage: 2, value: msg1[0].data.value * 2 } }])
        .ack(msg1[0])
        .commit()

    // Process stage 2 -> stage 3
    const msg2 = await client.queue('test-queue-v2-workflow-stage2')
        .batch(1).wait(false).pop()
    
    if (msg2.length === 0) {
        return { success: false, message: 'Stage 2 failed' }
    }

    await client.transaction()
        .queue('test-queue-v2-workflow-stage3')
        .push([{ data: { stage: 3, value: msg2[0].data.value + 5 } }])
        .ack(msg2[0])
        .commit()

    // Verify final result
    const msg3 = await client.queue('test-queue-v2-workflow-stage3')
        .batch(1).wait(false).pop()

    return { 
        success: msg3.length === 1 && msg3[0].data.value === 25,
        message: `Cross-queue workflow completed: final value ${msg3[0]?.data.value}` 
    }
}

export async function testPartitionWithConsumerGroupAndRetries(client) {
    const queue = await client.queue('test-queue-v2-complex-partition')
        .config({ retryLimit: 2 })
        .create()
    
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // Push to specific partition
    await client.queue('test-queue-v2-complex-partition')
        .partition('complex-partition-1')
        .push([{ data: { test: 'complex' } }])

    let attemptCount = 0

    // Consume with consumer group, fail first time
    await client.queue('test-queue-v2-complex-partition')
        .partition('complex-partition-1')
        .group('complex-group')
        .batch(1)
        .wait(false)
        .limit(2)
        .each()
        .consume(async (msg) => {
            attemptCount++
            if (attemptCount === 1) {
                throw new Error('First attempt fails')
            }
            // Second attempt succeeds
        })

    return { 
        success: attemptCount === 2,
        message: 'Complex partition/group/retry scenario works' 
    }
}

export async function testBufferedPushWithTransactionConsume(client) {
    await client.queue('test-queue-v2-buf-txn-source').create()
    await client.queue('test-queue-v2-buf-txn-dest').create()

    // Buffered push to source
    for (let i = 0; i < 10; i++) {
        await client.queue('test-queue-v2-buf-txn-source')
            .buffer({ messageCount: 10, timeMillis: 2000 })
            .push([{ data: { id: i } }])
    }

    // Wait for flush
    await new Promise(resolve => setTimeout(resolve, 2500))

    // Consume with transaction to move to destination
    let processed = 0
    await client.queue('test-queue-v2-buf-txn-source')
        .batch(10)
        .wait(false)
        .limit(1)
        .autoAck(false)
        .consume(async (msgs) => {
            for (const msg of msgs) {
                await client.transaction()
                    .queue('test-queue-v2-buf-txn-dest')
                    .push([{ data: { processed: true, originalId: msg.data.id } }])
                    .ack(msg)
                    .commit()
                processed++
            }
        })

    // Verify destination
    const destMsgs = await client.queue('test-queue-v2-buf-txn-dest')
        .batch(20).wait(false).pop()

    return { 
        success: processed === 10 && destMsgs.length === 10,
        message: `Buffered push + transaction consume: ${destMsgs.length} messages` 
    }
}

export async function testNamespaceWithMultipleQueues(client) {
    const namespace = 'test-mixed-namespace'

    // Create multiple queues in same namespace
    await client.queue('test-queue-v2-ns-multi-a')
        .namespace(namespace)
        .config({ priority: 10 })
        .create()
    
    await client.queue('test-queue-v2-ns-multi-b')
        .namespace(namespace)
        .config({ priority: 5 })
        .create()
    
    await client.queue('test-queue-v2-ns-multi-c')
        .namespace(namespace)
        .config({ priority: 1 })
        .create()

    // Push to all
    await client.queue('test-queue-v2-ns-multi-a').push([{ data: { from: 'a' } }])
    await client.queue('test-queue-v2-ns-multi-b').push([{ data: { from: 'b' } }])
    await client.queue('test-queue-v2-ns-multi-c').push([{ data: { from: 'c' } }])

    // Consume from namespace (should get all 3)
    const messages = []
    await client.queue()
        .namespace(namespace)
        .batch(10)
        .wait(false)
        .limit(1)
        .consume(async (msgs) => {
            messages.push(...msgs)
        })

    return { 
        success: messages.length === 3,
        message: `Namespace multi-queue: ${messages.length} messages` 
    }
}

export async function testEncryptedWithPartitionAndGroup(client) {
    const queue = await client.queue('test-queue-v2-encrypted-complex')
        .config({ encryptionEnabled: true })
        .create()
    
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // Push encrypted to partition
    await client.queue('test-queue-v2-encrypted-complex')
        .partition('secure-partition')
        .push([{ data: { secret: 'sensitive-data', id: 123 } }])

    // Consume with group
    let receivedData = null
    await client.queue('test-queue-v2-encrypted-complex')
        .partition('secure-partition')
        .group('secure-group')
        .batch(1)
        .wait(false)
        .limit(1)
        .each()
        .consume(async (msg) => {
            receivedData = msg.data
        })

    return { 
        success: receivedData && receivedData.secret === 'sensitive-data',
        message: 'Encrypted + partition + group works' 
    }
}

export async function testHighConcurrencyMixedOperations(client) {
    await client.queue('test-queue-v2-concurrency-mixed').create()

    // Push many messages
    const pushPromises = []
    for (let i = 0; i < 50; i++) {
        pushPromises.push(
            client.queue('test-queue-v2-concurrency-mixed')
                .push([{ data: { id: i } }])
        )
    }
    await Promise.all(pushPromises)

    // Consume with high concurrency
    let processedCount = 0
    await client.queue('test-queue-v2-concurrency-mixed')
        .concurrency(10)
        .batch(5)
        .wait(false)
        .limit(10)
        .each()
        .consume(async (msg) => {
            processedCount++
            await new Promise(resolve => setTimeout(resolve, 10))
        })

    return { 
        success: processedCount === 50,
        message: `High concurrency mixed ops: ${processedCount} messages` 
    }
}

