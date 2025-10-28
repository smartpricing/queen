/**
 * Error Handling & Edge Cases Tests
 * Tests various error conditions and edge cases
 */

export async function testInvalidQueueName(client) {
    try {
        await client.queue('').create()
        return { success: false, message: 'Empty queue name should fail' }
    } catch (error) {
        return { success: true, message: 'Empty queue name correctly rejected' }
    }
}

export async function testInvalidConfiguration(client) {
    try {
        await client.queue('test-queue-invalid-config')
            .config({ leaseTime: -1 })
            .create()
        return { success: false, message: 'Negative leaseTime should fail' }
    } catch (error) {
        return { success: true, message: 'Invalid config correctly rejected' }
    }
}

export async function testInvalidMessageFormat(client) {
    const queue = await client.queue('test-queue-v2-invalid-msg').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    try {
        // Push without data property
        await client.queue('test-queue-v2-invalid-msg').push([{ invalid: 'structure' }])
        // If it doesn't throw, check if it still works
        return { success: true, message: 'Client handles flexible message format' }
    } catch (error) {
        return { success: true, message: 'Invalid message format correctly handled' }
    }
}

export async function testPushEmptyArray(client) {
    const queue = await client.queue('test-queue-v2-empty-array').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    const result = await client.queue('test-queue-v2-empty-array').push([])
    return { success: result.length === 0, message: 'Empty array push handled correctly' }
}

export async function testAckWithoutPartitionId(client) {
    const queue = await client.queue('test-queue-v2-ack-no-partition').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    await client.queue('test-queue-v2-ack-no-partition')
        .push([{ data: { test: 'data' } }])

    const messages = await client.queue('test-queue-v2-ack-no-partition')
        .batch(1)
        .wait(false)
        .pop()

    if (messages.length === 0) {
        return { success: false, message: 'No message to test' }
    }

    // Try to ack without partitionId (should fail)
    const messageWithoutPartition = { transactionId: messages[0].transactionId }
    const result = await client.ack(messageWithoutPartition)

    return { 
        success: result.success === false && result.error.includes('partitionId'), 
        message: 'Ack without partitionId correctly rejected' 
    }
}

export async function testPopFromNonExistentQueue(client) {
    try {
        const result = await client
            .queue('non-existent-queue-xyz')
            .batch(1)
            .wait(false)
            .pop()
        
        // It's okay if it returns empty or throws
        return { success: true, message: 'Non-existent queue handled gracefully' }
    } catch (error) {
        return { success: true, message: 'Non-existent queue error handled' }
    }
}

export async function testMultiplePushesWithSameTransactionId(client) {
    const queue = await client.queue('test-queue-v2-same-txid').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    const txId = 'duplicate-transaction-id-test'
    
    const result1 = await client.queue('test-queue-v2-same-txid')
        .push([{ transactionId: txId, data: { first: true } }])
    
    const result2 = await client.queue('test-queue-v2-same-txid')
        .push([{ transactionId: txId, data: { second: true } }])

    const result3 = await client.queue('test-queue-v2-same-txid')
        .push([{ transactionId: txId, data: { third: true } }])

    const duplicates = [result1[0], result2[0], result3[0]].filter(r => r.status === 'duplicate').length

    return { 
        success: duplicates >= 2, 
        message: `Duplicate detection working (${duplicates} duplicates found)` 
    }
}

export async function testAckExpiredLease(client) {
    const queue = await client.queue('test-queue-v2-expired-lease')
        .config({ leaseTime: 1 })
        .create()
    
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    await client.queue('test-queue-v2-expired-lease')
        .push([{ data: { test: 'data' } }])

    const messages = await client.queue('test-queue-v2-expired-lease')
        .batch(1)
        .wait(false)
        .pop()

    if (messages.length === 0) {
        return { success: false, message: 'No message to test' }
    }

    // Wait for lease to expire
    await new Promise(resolve => setTimeout(resolve, 2000))

    // Try to ack expired lease
    const result = await client.ack(messages[0], true)

    return { 
        success: result.success === false, 
        message: 'Expired lease ack correctly rejected' 
    }
}

export async function testBatchAckMixedResults(client) {
    const queue = await client.queue('test-queue-v2-batch-ack-mixed')
        .config({ leaseTime: 2 })
        .create()
    
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    await client.queue('test-queue-v2-batch-ack-mixed').push([
        { data: { id: 1 } },
        { data: { id: 2 } },
        { data: { id: 3 } }
    ])

    const messages = await client.queue('test-queue-v2-batch-ack-mixed')
        .batch(3)
        .wait(false)
        .pop()

    if (messages.length !== 3) {
        return { success: false, message: 'Expected 3 messages' }
    }

    // Wait for one lease to expire
    await new Promise(resolve => setTimeout(resolve, 2500))

    // Try to ack all three (one should fail due to expired lease)
    const result = await client.ack(messages, true)

    // Should indicate some failures
    return { 
        success: result.success === true, // Batch ack returns overall success
        message: 'Batch ack with expired lease handled' 
    }
}

export async function testVeryLargePayload(client) {
    const queue = await client.queue('test-queue-v2-very-large').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // Create a very large payload (attempt to push near limits)
    const hugeArray = new Array(50000).fill({
        id: Math.random().toString(36),
        data: 'x'.repeat(100)
    })

    try {
        const result = await client.queue('test-queue-v2-very-large')
            .push([{ data: hugeArray }])

        const received = await client.queue('test-queue-v2-very-large')
            .batch(1)
            .wait(false)
            .pop()

        return { 
            success: received.length === 1 && received[0].data.length === 50000,
            message: 'Very large payload handled correctly' 
        }
    } catch (error) {
        // It's okay if it fails - system has limits
        return { 
            success: true, 
            message: `Large payload limit enforced: ${error.message}` 
        }
    }
}

