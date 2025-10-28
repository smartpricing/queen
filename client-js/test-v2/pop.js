export async function popEmptyQueue(client) {
    const queue = await client.queue('test-queue-v2-pop-empty').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    const res = await client
    .queue('test-queue-v2-pop-empty')
    .batch(1)      // Fetch 1 message
    .wait(false)
    .pop()
    return { success: res.length === 0 }
}

export async function popNonEmptyQueue(client) {
    const queue = await client.queue('test-queue-v2-pop-non-empty').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    await client
    .queue('test-queue-v2-pop-non-empty')
    .push([{ data: { message: 'Hello, world!' } }])

    const res = await client
    .queue('test-queue-v2-pop-non-empty')
    .batch(1)
    .wait(false)
    .pop()
    return { success: res.length === 1 }
}

export async function popWithWait(client) {
    const queue = await client.queue('test-queue-v2-pop-with-wait').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    setTimeout(async () => {
        await client
        .queue('test-queue-v2-pop-with-wait')
        .push([{ data: { message: 'Hello, world!' } }])
    }, 2000);

    const res = await client
    .queue('test-queue-v2-pop-with-wait')
    .batch(1)
    .wait(true)
    .pop()

    return { success: res.length === 1 }
}

export async function popWithAck(client) {
    const queue = await client.queue('test-queue-v2-pop-with-ack').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    await client
    .queue('test-queue-v2-pop-with-ack')
    .push([{ data: { message: 'Hello, world!' } }]) 


    const res = await client
    .queue('test-queue-v2-pop-with-ack')
    .batch(1)
    .wait(false)
    .pop()

    const resAck = await client.ack(res[0])

    if (!resAck.success) {
        return { success: false, message: `Ack failed: ${resAck.error}` }
    }

    return { success: true, message: 'Message popped and acked successfully' }
}

export async function popWithAckReconsume(client) {
    const queue = await client
    .queue('test-queue-v2-pop-with-ack-reconsume')
    .config({
        leaseTime: 1,  // 1 second lease
    })
    .create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    
    await client
    .queue('test-queue-v2-pop-with-ack-reconsume')
    .push([{ data: { message: 'Hello, world!' } }])

    const res = await client
    .queue('test-queue-v2-pop-with-ack-reconsume')
    .batch(1)
    .wait(false)
    .pop()

    if (res.length !== 1) {
        return { success: false, message: `Expected 1 message, got ${res.length}` }
    }

    await new Promise(r => setTimeout(r, 2000))

    const res2 = await client
    .queue('test-queue-v2-pop-with-ack-reconsume')
    .batch(1)
    .wait(true)
    .pop()

    if (res2.length === 1) {
        return { success: true, message: 'Message consumed and reconsumed after lease expiry' }
    }
    return { success: false, message: `Expected 1 message, got ${res2.length}` }
}

