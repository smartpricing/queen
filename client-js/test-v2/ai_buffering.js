/**
 * Client-Side Buffering Tests
 * Tests for buffer management and statistics
 */

export async function testBufferStatistics(client) {
    const queue = await client.queue('test-queue-v2-buffer-stats').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // Get initial stats
    const statsBefore = client.getBufferStats()
    
    if (!statsBefore || typeof statsBefore !== 'object') {
        return { success: false, message: 'Buffer stats not available' }
    }

    // Push some buffered messages
    for (let i = 0; i < 5; i++) {
        await client.queue('test-queue-v2-buffer-stats')
            .buffer({ messageCount: 10, timeMillis: 5000 })
            .push([{ data: { id: i } }])
    }

    // Get stats after buffering
    const statsAfter = client.getBufferStats()
    
    // Stats should show buffer activity
    return { 
        success: statsAfter !== null,
        message: `Buffer stats retrieved: ${JSON.stringify(statsAfter)}` 
    }
}

export async function testManualBufferFlush(client) {
    const queue = await client.queue('test-queue-v2-manual-flush').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // Push buffered messages (with high threshold so they don't auto-flush)
    for (let i = 0; i < 5; i++) {
        await client.queue('test-queue-v2-manual-flush')
            .buffer({ messageCount: 100, timeMillis: 30000 })
            .push([{ data: { id: i } }])
    }

    // Check that messages are not in queue yet
    const beforeFlush = await client.queue('test-queue-v2-manual-flush')
        .batch(10)
        .wait(false)
        .pop()

    if (beforeFlush.length > 0) {
        return { success: false, message: 'Messages not buffered (flushed too early)' }
    }

    // Manually flush all buffers
    await client.flushAllBuffers()

    // Now messages should be in the queue
    const afterFlush = await client.queue('test-queue-v2-manual-flush')
        .batch(10)
        .wait(false)
        .pop()

    return { 
        success: afterFlush.length === 5,
        message: `Manual flush successful: ${afterFlush.length} messages` 
    }
}

export async function testBufferTimeThreshold(client) {
    const queue = await client.queue('test-queue-v2-buffer-time').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // Push with time-based buffer (1 second)
    await client.queue('test-queue-v2-buffer-time')
        .buffer({ messageCount: 1000, timeMillis: 1000 })
        .push([{ data: { test: 'data' } }])

    // Wait for time threshold
    await new Promise(resolve => setTimeout(resolve, 1500))

    // Message should be auto-flushed by time
    const messages = await client.queue('test-queue-v2-buffer-time')
        .batch(1)
        .wait(false)
        .pop()

    return { 
        success: messages.length === 1,
        message: 'Time-based buffer flush works' 
    }
}

export async function testBufferCountThreshold(client) {
    const queue = await client.queue('test-queue-v2-buffer-count').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // Push exactly 5 messages with count threshold of 5
    for (let i = 0; i < 5; i++) {
        await client.queue('test-queue-v2-buffer-count')
            .buffer({ messageCount: 5, timeMillis: 30000 })
            .push([{ data: { id: i } }])
    }

    // Small delay for flush to process
    await new Promise(resolve => setTimeout(resolve, 200))

    // All 5 should be flushed when threshold reached
    const messages = await client.queue('test-queue-v2-buffer-count')
        .batch(10)
        .wait(false)
        .pop()

    return { 
        success: messages.length === 5,
        message: `Count-based buffer flush: ${messages.length} messages` 
    }
}

export async function testMultipleBuffers(client) {
    // Create multiple queues with buffering
    await client.queue('test-queue-v2-multi-buf-1').create()
    await client.queue('test-queue-v2-multi-buf-2').create()
    await client.queue('test-queue-v2-multi-buf-3').create()

    // Buffer messages to different queues
    for (let i = 0; i < 3; i++) {
        await client.queue('test-queue-v2-multi-buf-1')
            .buffer({ messageCount: 100, timeMillis: 10000 })
            .push([{ data: { queue: 1, id: i } }])
        
        await client.queue('test-queue-v2-multi-buf-2')
            .buffer({ messageCount: 100, timeMillis: 10000 })
            .push([{ data: { queue: 2, id: i } }])
        
        await client.queue('test-queue-v2-multi-buf-3')
            .buffer({ messageCount: 100, timeMillis: 10000 })
            .push([{ data: { queue: 3, id: i } }])
    }

    // Get stats to see multiple buffers
    const stats = client.getBufferStats()

    // Flush all buffers
    await client.flushAllBuffers()

    // Verify all queues received messages
    const msgs1 = await client.queue('test-queue-v2-multi-buf-1').batch(10).wait(false).pop()
    const msgs2 = await client.queue('test-queue-v2-multi-buf-2').batch(10).wait(false).pop()
    const msgs3 = await client.queue('test-queue-v2-multi-buf-3').batch(10).wait(false).pop()

    return { 
        success: msgs1.length === 3 && msgs2.length === 3 && msgs3.length === 3,
        message: `Multiple buffers flushed: ${msgs1.length + msgs2.length + msgs3.length} total` 
    }
}

export async function testBufferWithPartition(client) {
    const queue = await client.queue('test-queue-v2-buffer-partition').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // Buffer messages to specific partition
    for (let i = 0; i < 5; i++) {
        await client.queue('test-queue-v2-buffer-partition')
            .partition('test-partition-buf')
            .buffer({ messageCount: 100, timeMillis: 10000 })
            .push([{ data: { id: i } }])
    }

    await client.flushAllBuffers()

    // Pop from that partition
    const messages = await client.queue('test-queue-v2-buffer-partition')
        .partition('test-partition-buf')
        .batch(10)
        .wait(false)
        .pop()

    return { 
        success: messages.length === 5,
        message: 'Buffering with partition works' 
    }
}

