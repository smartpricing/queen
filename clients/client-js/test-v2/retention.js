
// To pass this test the server needs to 
// be started with RETENTION_INTERVAL=2000
export async function retentionTest(client) {
    const queueName = 'test-queue-retention-01';
    const queue = await client
    .queue(queueName)
    .config({
        retentionEnabled: true,
        retentionSeconds: 10,
        completedRetentionSeconds: 5
    })
    .create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    for (let i = 0; i < 100; i++) {
        const res = await client
        .queue(queueName)
        .push([{ data: { message: i } }])
    }

    await new Promise(resolve => setTimeout(resolve, 15000))

    const messages = await client
    .queue(queueName)
    .batch(100)
    .wait(false)
    .pop()

    if (messages.length === 0) {
        return { success: true, message: 'Messages cleaned up' }
    }

    return { success: false }
}


// To pass this test the server needs to 
// be started with RETENTION_INTERVAL=2000
export async function retentionTestMaxTime(client) {
    const queueName = 'test-queue-retention-02';
    const queue = await client
    .queue(queueName)
    .config({ retentionEnabled: true, maxWaitTimeSeconds: 10 })
    .create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created with retention enabled' }
    }
    for (let i = 0; i < 100; i++) {
        const res = await client
        .queue(queueName)
        .push([{ data: { message: i } }])
    }

    await new Promise(resolve => setTimeout(resolve, 15000))

    const messages = await client
    .queue(queueName)
    .batch(100)
    .wait(false)
    .pop()

    if (messages.length === 0) {
        return { success: true, message: 'Messages cleaned up' }
    }

    return { success: false }
}