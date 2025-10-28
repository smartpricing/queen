export async function testConsumer(client) { 
    const queue = await client.queue('test-queue-v2-consume').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    await client
    .queue('test-queue-v2-consume')
    .push([{ data: { message: 'Hello, world!' } }])

    let msgToReturn = null

    await client
    .queue('test-queue-v2-consume')
    .batch(1)
    .limit(1)
    .each()
    .consume(async msg => {
        msgToReturn = msg
    })

    return { success: msgToReturn !== null, message: 'Consumer test completed successfully' }
}

export async function testConsumerTrace(client) { 
    const queue = await client.queue('test-queue-v2-consume-trace').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    await client
    .queue('test-queue-v2-consume-trace')
    .push([{ data: { message: 'Hello, world!' } }])

    let msgToReturn = null

    await client
    .queue('test-queue-v2-consume-trace')
    .batch(1)
    .limit(1)
    .each()
    .consume(async msg => {
        msgToReturn = msg
        await msg.trace({
            traceName: ['test-trace', 'test-trace-2'],
            eventType: 'info',
            data: { message: 'Hello, world!'  }
        })
    })

    return { success: msgToReturn !== null, message: 'Consumer test completed successfully' }
}

export async function testConsumerNamespace(client) { 
    const queue = await client.queue('test-queue-v2-namespace')
    .namespace('test-namespace')
    .create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    await client
    .queue('test-queue-v2-namespace')
    .push([{ data: { message: 'Hello, world!' } }])

    let msgToReturn = null

    await client
    .queue()
    .namespace('test-namespace')
    .batch(1)
    .limit(1)
    .each()
    .consume(async msg => {
        msgToReturn = msg
    })

    return { success: msgToReturn !== null, message: 'Consumer test completed successfully' }
}

export async function testConsumerTask(client) { 
    const queue = await client.queue('test-queue-v2-task')
    .task('test-task')
    .create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    await client
    .queue('test-queue-v2-task')
    .push([{ data: { message: 'Hello, world!' } }])

    let msgToReturn = null

    await client
    .queue()
    .task('test-task')
    .batch(1)
    .limit(1)
    .each()
    .consume(async msg => {
        msgToReturn = msg
    })

    return { success: msgToReturn !== null, message: 'Consumer test completed successfully' }
}

export async function testConsumerWithPartition(client) { 
    const queue = await client.queue('test-queue-v2-consume-with-partition').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    for (let i = 0; i < 100; i++) {
        await client
        .queue('test-queue-v2-consume-with-partition')
        .buffer({ messageCount: 100, timeMillis: 1000 })
        .partition('test-partition-01')
        .push([{ data: { message: 'Hello, world!' } }])
    }

    for (let i = 0; i < 100; i++) {
        await client
        .queue('test-queue-v2-consume-with-partition')
        .buffer({ messageCount: 100, timeMillis: 1000 })
        .partition('test-partition-02')
        .push([{ data: { message: 'Hello, world!' } }])
    }

    let msgToReturn1 = null
    let msgToReturn2 = null
    await client
    .queue('test-queue-v2-consume-with-partition')
    .partition('test-partition-01')
    .batch(100)
    .limit(1)
    .consume(async msgs => {
        msgToReturn1 = msgs.length
    })

    await client
    .queue('test-queue-v2-consume-with-partition')
    .partition('test-partition-02')
    .batch(100)
    .limit(1)
    .consume(async msgs => {
        msgToReturn2 = msgs.length
    })    

    return { success: msgToReturn1 == 100 && msgToReturn2 == 100, message: 'Consumer test completed successfully' }
}


export async function testConsumerBatchConsume(client) { 
    const queue = await client.queue('test-queue-v2-consume-batch').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    await client
    .queue('test-queue-v2-consume-batch')
    .push([
        { data: { message: 'Hello, world!' } },
        { data: { message: 'Hello, world 2!' } },
        { data: { message: 'Hello, world 3!' } }
    ])

    let msgLength = 0

    await client
    .queue('test-queue-v2-consume-batch')
    .batch(10)
    .wait(false)
    .limit(1)
    .consume(async msgs => {
        msgLength = msgs.length
    })

    return { success: msgLength === 3, message: 'Consumer test completed successfully' }
}

export async function testConsumerOrdering(client) { 
    const queue = await client.queue('test-queue-v2-consume-batch').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    let messagesToPush = 100

    for (let i = 0; i < messagesToPush; i++) {
        await client
        .queue('test-queue-v2-consume-batch')
        .push([{ data: { id: i } }])
    }

    
    let lastId = null
    await client
    .queue('test-queue-v2-consume-batch')
    .batch(1)
    .wait(false)
    .limit(messagesToPush)
    .each()
    .consume(async msg => {
        if (lastId === null) {
            lastId = msg.data.id
        } else {
            if (msg.data.id !== lastId + 1) {
                return { success: false, message: 'Message ordering violation' }
            }
            lastId = msg.data.id
        }
    })

    return { success: lastId === messagesToPush - 1, message: 'Consumer test completed successfully' }
}

export async function testConsumerOrderingBatch(client) { 
    const queue = await client.queue('test-queue-v2-consume-batch-100').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    let messagesToPush = 100

    for (let i = 0; i < messagesToPush; i++) {
        await client
        .queue('test-queue-v2-consume-batch-100')
        .push([{ data: { id: i } }])
    }

    let lastId = null
    await client
    .queue('test-queue-v2-consume-batch-100')
    .batch(messagesToPush)
    .wait(false)
    .limit(1)
    .consume(async msgs => {
        for (const msg of msgs) {
            if (lastId === null) {
                lastId = msg.data.id
            } else {
                if (msg.data.id !== lastId + 1) {
                    return { success: false, message: 'Message ordering violation' }
                }
                lastId = msg.data.id
            }
        }
    })

    return { success: lastId === messagesToPush - 1, message: 'Consumer test completed successfully' }
}


export async function testConsumerBatchConsumeBatch(client) { 
    const queue = await client.queue('test-queue-v2-consume-batch').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    await client
    .queue('test-queue-v2-consume-batch')
    .push([
        { data: { message: 'Hello, world!' } },
        { data: { message: 'Hello, world 2!' } },
        { data: { message: 'Hello, world 3!' } }
    ])

    let msgLength = 0

    await client
    .queue('test-queue-v2-consume-batch')
    .batch(10)
    .wait(false)
    .limit(1)
    .consume(async msgs => {
        msgLength = msgs.length
    })

    return { success: msgLength === 3, message: 'Consumer test completed successfully' }
}

export async function testConsumerOrderingConcurrency(client) { 
    const queue = await client.queue('test-queue-v2-consume-batch-concurrency').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    let messagesToPush = 100

    for (let i = 0; i < messagesToPush; i++) {
        await client
        .queue('test-queue-v2-consume-batch-concurrency')
        .push([{ data: { id: i } }])
    }

    let lastId = null
    await client
    .queue('test-queue-v2-consume-batch-concurrency')
    .concurrency(10)
    .batch(1)
    .wait(false)
    .limit(10)
    .each()
    .consume(async msg => {
        if (lastId === null) {
            lastId = msg.data.id
        } else {
            if (msg.data.id !== lastId + 1) {
                return { success: false, message: 'Message ordering violation' }
            }
            lastId = msg.data.id
        }
    })


    return { success: lastId === messagesToPush - 1, message: 'Consumer test completed successfully' }
}

export async function testConsumerOrderingConcurrencyWithBufferedPush(client) { 
    const queue = await client.queue('test-queue-v2-consume-batch-concurrency-with-buffered-push').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    let messagesToPush = 100

    for (let i = 0; i < messagesToPush; i++) {
        await client
        .queue('test-queue-v2-consume-batch-concurrency-with-buffered-push')
        .buffer({ messageCount: 100, timeMillis: 1000 })
        .push([{ data: { id: i } }])
    }

    let lastId = null
    await client
    .queue('test-queue-v2-consume-batch-concurrency-with-buffered-push')
    .concurrency(10)
    .batch(10)
    .wait(false)
    .limit(10)
    .each()
    .consume(async msg => {
        if (lastId === null) {
            lastId = msg.data.id
        } else {
            if (msg.data.id !== lastId + 1) {
                return { success: false, message: 'Message ordering violation' }
            }
            lastId = msg.data.id
        }
    })

    return { success: lastId === messagesToPush - 1, message: 'Consumer test completed successfully' }
}

export async function consumerGroup(client) { 
    const queue = await client.queue('test-queue-v2-consume-group').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    let messagesToPush = 100

    for (let i = 0; i < messagesToPush; i++) {
        await client
        .queue('test-queue-v2-consume-group')
        .buffer({ messageCount: 100, timeMillis: 1000 })
        .push([{ data: { id: i } }])
    }


    let group01Messages = 0
    let group02Messages = 0
    
    await client
    .queue('test-queue-v2-consume-group')
    .group('test-group-01')
    .batch(messagesToPush)
    .limit(1)
    .wait(false)
    .consume(async msgs => {
        group01Messages = msgs.length
    })

    await client
    .queue('test-queue-v2-consume-group')
    .group('test-group-02')
    .batch(messagesToPush)
    .limit(1)
    .wait(false)
    .consume(async msgs => {
        group02Messages = msgs.length
    })    

    return { success: group01Messages === messagesToPush && group02Messages === messagesToPush, message: 'Consumer test completed successfully' }
}

export async function consumerGroupWithPartition(client) { 
    const queue = await client.queue('test-queue-v2-consume-group-with-partition').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    let messagesToPush = 100

    for (let i = 0; i < messagesToPush; i++) {
        await client
        .queue('test-queue-v2-consume-group-with-partition')
        .partition('test-partition-01')
        .buffer({ messageCount: 100, timeMillis: 1000 })
        .push([{ data: { id: i } }])
    }


    let group01Messages = 0
    let group02Messages = 0
    
    await client
    .queue('test-queue-v2-consume-group-with-partition')
    .partition('test-partition-01')
    .group('test-group-01')
    .batch(messagesToPush)
    .limit(1)
    .wait(false)
    .consume(async msgs => {
        group01Messages = msgs.length
    })

    await client
    .queue('test-queue-v2-consume-group-with-partition')
    .partition('test-partition-01')
    .group('test-group-02')
    .batch(messagesToPush)
    .limit(1)
    .wait(false)
    .consume(async msgs => {
        group02Messages = msgs.length
    })    

    return { success: group01Messages === messagesToPush && group02Messages === messagesToPush, message: 'Consumer test completed successfully' }
}

export async function manualAck(client) { 
    const queue = await client
    .queue('test-queue-v2-manual-ack')
    .create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    const messagesToPush = 10000;

    let messages = [];
    for (let i = 0; i < messagesToPush; i++) {
        messages.push({ data: { id: i } })
        if (messages.length >= 1000) {
            await client.queue('test-queue-v2-manual-ack').push(messages)
            messages = [];
        }
    }
    await client.queue('test-queue-v2-manual-ack').push(messages)    

    let uniqueIds = new Set();
    let lastId = null;

    await client
    .queue('test-queue-v2-manual-ack')
    .concurrency(10)
    .batch(1000)
    .wait(false)
    .limit(1)
    .consume(async msgs => {
        for (const msg of msgs) {
            if (lastId === null) {
                lastId = msg.data.id
            } else {
                if (msg.data.id !== lastId + 1) {
                    throw new Error('Message ordering violation')
                }
                lastId = msg.data.id
            }
            uniqueIds.add(msg.data.id)
        }
    })
    .onSuccess(async (msgs, result) => {
        await client.ack(msgs, true)
    })
    .onError(async (msgs, error) => {
        await client.ack(msgs, false)
    })

    const uniqueIdsCount = uniqueIds.size;

    return { success: uniqueIdsCount === messagesToPush, message: 'Load test completed successfully' }
}

export async function retries(client) { 
    const queue = await client
    .queue('test-queue-v2-retries')
    .config({ 
        retryLimit: 3
    })
    .create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    const messagesToPush = 100;

    let messages = [];
    for (let i = 0; i < messagesToPush; i++) {
        messages.push({ data: { id: i } })
    }
    await client.queue('test-queue-v2-retries').push(messages)    

    let attemptCount = 0

    await client
    .queue('test-queue-v2-retries')
    .concurrency(1)
    .batch(100)
    .wait(false)
    .limit(300)  // Allow up to 300 messages (3 batches of 100)
    .consume(async msgs => {
        attemptCount++;
        if (attemptCount < 3) {
            // Fail first 2 attempts, succeed on 3rd
            throw new Error('Test error - triggering retry')
        }
        // Third attempt succeeds (no error thrown)
    })

    return { success: attemptCount === 3, message: `Retry test completed successfully (attempts: ${attemptCount})` }
}

export async function retriesConsumerGroup(client) { 
    const queue = await client
    .queue('test-queue-v2-retries-consumer-group')
    .config({ 
        retryLimit: 3
    })
    .create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    const messagesToPush = 100;

    let messages = [];
    for (let i = 0; i < messagesToPush; i++) {
        messages.push({ data: { id: i } })
    }
    await client.queue('test-queue-v2-retries-consumer-group').push(messages)    

    let attemptCount = 0

    await client
    .queue('test-queue-v2-retries-consumer-group')
    .group('test-group-01')
    .concurrency(1)
    .batch(100)
    .wait(false)
    .limit(300)  // Allow up to 300 messages (3 batches of 100)
    .autoAck(true)  // Auto-ack on success, auto-NACK on failure
    .consume(async msgs => {
        console.log('Group 01: ' + msgs.length)
        attemptCount++;
        if (attemptCount < 3) {
            // Fail first 2 attempts, succeed on 3rd
            throw new Error('Test error - triggering retry')
        }
        // Third attempt succeeds (no error thrown)
    })

    let group02Messages = 0

    await client
    .queue('test-queue-v2-retries-consumer-group')
    .group('test-group-02')
    .concurrency(1)
    .batch(100)
    .wait(false)
    .limit(100)  // Allow up to 300 messages (3 batches of 100)
    .autoAck(true)  // Auto-ack on success, auto-NACK on failure
    .consume(async msgs => {
        console.log('Group 02: ' + msgs.length)
        group02Messages += msgs.length
    })    

    return { success: attemptCount == 3 && group02Messages === messagesToPush, message: `Retry test completed successfully (attempts: ${attemptCount}, group02: ${group02Messages})` }
}

export async function autoRenewLease(client) { 
    const queue = await client
    .queue('test-queue-v2-auto-renew-lease')
    .config({ 
        leaseTime: 3
    })
    .create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    await client
    .queue('test-queue-v2-auto-renew-lease')
    .push([{ data: { message: 'Long processing task' } }])

    let shouldFailHasFailed = false;

    await client
    .queue('test-queue-v2-auto-renew-lease')
    .batch(1)
    .wait(false)
    .limit(1)
    .each()
    .consume(async msg => {
        console.log('Message: ' + msg.data.message)
        await new Promise(resolve => setTimeout(resolve, 5000))
    })
    .onSuccess(async (msgs, result) => {
        // This shoudl fail if not auto-renewed
        const resultAck = await client.ack(msgs, true)
        if (resultAck.success === false && resultAck.includes('invalid_lease')) {
            shouldFailHasFailed = true;
        }
    })
    .onError(async (msgs, error) => {
        await client.ack(msgs, false)
    })

    await client
    .queue('test-queue-v2-auto-renew-lease')
    .push([{ data: { message: 'Long processing task' } }])

    let shouldPassHasPass = false;

    await client
    .queue('test-queue-v2-auto-renew-lease')
    .batch(1)
    .wait(false)
    .limit(1)
    .renewLease(true, 1000)
    .each()
    .consume(async msg => {
        console.log('Message: ' + msg.data.message)
        await new Promise(resolve => setTimeout(resolve, 5000))
    })
    .onSuccess(async (msgs, result) => {
        // This shoudl pass, if auto-renewed
        const resultAck = await client.ack(msgs, true)
        if (resultAck.success === true) {
            shouldPassHasPass = true;
        }
    })
    .onError(async (msgs, error) => {
        await client.ack(msgs, false)
    })    

    return { success: shouldFailHasFailed === false && shouldPassHasPass === true, message: 'Auto renew lease test completed successfully' }
}