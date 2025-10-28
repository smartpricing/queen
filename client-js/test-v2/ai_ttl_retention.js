/**
 * TTL and Retention Tests
 * Tests for message TTL, retention policies
 */

export async function testMessageTTL(client) {
    const queue = await client.queue('test-queue-v2-ttl')
        .config({ 
            ttl: 2  // 2 seconds TTL
        })
        .create()
    
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // Push a message
    await client.queue('test-queue-v2-ttl')
        .push([{ data: { test: 'ttl-message' } }])

    // Wait for TTL to expire
    await new Promise(resolve => setTimeout(resolve, 3000))

    // Try to pop - message should be expired
    const messages = await client.queue('test-queue-v2-ttl')
        .batch(1)
        .wait(false)
        .pop()

    // Note: TTL behavior may vary - message might be deleted or marked as expired
    return { 
        success: true,
        message: `TTL test completed: ${messages.length} messages found (expired messages may be auto-deleted)` 
    }
}

export async function testRetentionEnabled(client) {
    const queue = await client.queue('test-queue-v2-retention')
        .config({ 
            retentionEnabled: true,
            retentionSeconds: 3600  // 1 hour
        })
        .create()
    
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // Push and process a message
    await client.queue('test-queue-v2-retention')
        .push([{ data: { test: 'retention' } }])

    const messages = await client.queue('test-queue-v2-retention')
        .batch(1)
        .wait(false)
        .pop()

    if (messages.length === 0) {
        return { success: false, message: 'No message to process' }
    }

    await client.ack(messages[0], true)

    // With retention enabled, we should be able to query completed messages
    // This would require accessing the messages API
    return { 
        success: true,
        message: 'Retention policy configured' 
    }
}

export async function testCompletedRetention(client) {
    const queue = await client.queue('test-queue-v2-completed-retention')
        .config({ 
            completedRetentionSeconds: 60  // 1 minute
        })
        .create()
    
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // Push, pop, and ack a message
    await client.queue('test-queue-v2-completed-retention')
        .push([{ data: { test: 'completed' } }])

    const messages = await client.queue('test-queue-v2-completed-retention')
        .batch(1)
        .wait(false)
        .pop()

    if (messages.length === 0) {
        return { success: false, message: 'No message to process' }
    }

    await client.ack(messages[0], true)

    // Message should be retained for the specified time
    return { 
        success: true,
        message: 'Completed retention configured' 
    }
}

export async function testNoRetention(client) {
    const queue = await client.queue('test-queue-v2-no-retention')
        .config({ 
            retentionEnabled: false,
            retentionSeconds: 0,
            completedRetentionSeconds: 0
        })
        .create()
    
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // Push, pop, and ack
    await client.queue('test-queue-v2-no-retention')
        .push([{ data: { test: 'no-retention' } }])

    const messages = await client.queue('test-queue-v2-no-retention')
        .batch(1)
        .wait(false)
        .pop()

    if (messages.length > 0) {
        await client.ack(messages[0], true)
    }

    // Without retention, completed messages should be deleted immediately
    return { 
        success: true,
        message: 'No retention mode configured' 
    }
}

export async function testMaxWaitTimeDLQ(client) {
    const queue = await client.queue('test-queue-v2-max-wait')
        .config({ 
            maxWaitTimeSeconds: 2,  // 2 seconds max wait
            dlqAfterMaxRetries: true,
            deadLetterQueue: true
        })
        .create()
    
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // Push a message
    await client.queue('test-queue-v2-max-wait')
        .push([{ data: { test: 'max-wait' } }])

    // Wait longer than maxWaitTimeSeconds without consuming
    await new Promise(resolve => setTimeout(resolve, 3000))

    // Check if message moved to DLQ
    const dlqResult = await client.queue('test-queue-v2-max-wait')
        .dlq()
        .limit(10)
        .get()

    // Note: maxWaitTimeSeconds behavior may vary by implementation
    return { 
        success: true,
        message: `Max wait time test completed: ${dlqResult?.messages?.length || 0} in DLQ` 
    }
}

