export async function testLoad(client) { 
    const queue = await client
    .queue('test-queue-v2-load')
    .create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    const messagesToPush = 100000;

    let messages = [];
    for (let i = 0; i < messagesToPush; i++) {
        messages.push({ data: { id: i } })
        if (messages.length >= 10000) {
            await client.queue('test-queue-v2-load').push(messages)
            messages = [];
        }
    }
    await client.queue('test-queue-v2-load').push(messages)    

    let uniqueIds = new Set();
    let lastId = null;

    await client
    .queue('test-queue-v2-load')
    .concurrency(10)
    .batch(10000)
    .wait(false)
    .limit(1)
    .consume(async msgs => {
        console.log(`Messages: ${msgs.length}`)
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

    const uniqueIdsCount = uniqueIds.size;
    console.log(`Unique IDs count: ${uniqueIdsCount}`)

    return { success: uniqueIdsCount === messagesToPush, message: 'Load test completed successfully' }
}

export async function testLoadPartition(client) { 
    const queue = await client
    .queue('test-queue-v2-load-partition')
    .create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    const messagesToPush = 100000;

    let messages = [];
    let k = 0
    for (let i = 0; i < messagesToPush; i++) {
        messages.push({ data: { id: i } })
        if (messages.length >= 10000) {
            await client
            .queue('test-queue-v2-load-partition')
            .partition(k.toString())
            .push(messages)
            messages = [];
            k++;
        }
    }
    await client.queue('test-queue-v2-load-partition').push(messages)    

    let uniqueIds = new Set();
    let lastId = null;
    
    for (let i = 0; i < 10; i++) {
    await client
    .queue('test-queue-v2-load-partition')
    .partition(i.toString())
    .batch(10000)
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
    }

    const uniqueIdsCount = uniqueIds.size;

    return { success: uniqueIdsCount === messagesToPush, message: 'Load test completed successfully' }
}

export async function testLoadConsumerGroup(client) { 
    const queue = await client
    .queue('test-queue-v2-load-consumer-group')
    .create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    const messagesToPush = 100000;

    let messages = [];
    for (let i = 0; i < messagesToPush; i++) {
        messages.push({ data: { id: i } })
        if (messages.length >= 10000) {
            await client.queue('test-queue-v2-load-consumer-group').push(messages)
            messages = [];
        }
    }
    await client.queue('test-queue-v2-load-consumer-group').push(messages)    

    let uniqueIdsA = new Set();
    let lastIdA = null;

    await client
    .queue('test-queue-v2-load-consumer-group')
    .group('test-consumer-group-a')
    .concurrency(10)
    .batch(10000)
    .wait(false)
    .limit(1)
    .consume(async msgs => {
        for (const msg of msgs) {
            if (lastIdA === null) {
                lastIdA = msg.data.id
            } else {
                if (msg.data.id !== lastIdA + 1) {
                    throw new Error('Message ordering violation')
                }
                lastIdA = msg.data.id
            }
            uniqueIdsA.add(msg.data.id)
        }
    })
    
    let uniqueIdsB = new Set();
    let lastIdB = null;

    await client
    .queue('test-queue-v2-load-consumer-group')
    .group('test-consumer-group-b')
    .concurrency(10)
    .batch(10000)
    .wait(false)
    .limit(1)
    .consume(async msgs => {
        for (const msg of msgs) {
            if (lastIdB === null) {
                lastIdB = msg.data.id
            } else {
                if (msg.data.id !== lastIdB + 1) {
                    throw new Error('Message ordering violation')
                }
                lastIdB = msg.data.id
            }
            uniqueIdsB.add(msg.data.id)
        }
    })    

    const uniqueIdsCountA = uniqueIdsA.size;
    const uniqueIdsCountB = uniqueIdsB.size;
    return { success: uniqueIdsCountA === messagesToPush && uniqueIdsCountB === messagesToPush, message: 'Load test completed successfully' }
}
