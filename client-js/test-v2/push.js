import crypto from 'crypto';

export async function pushMessage(client) {
    const queue = await client.queue('test-queue-v2').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    const res = await client
    .queue('test-queue-v2')
    .push([{ data: { message: 'Hello, world!' } }])

    return { success: res[0].status === 'queued' }
}

export async function pushDuplicateMessage(client) {
    const queue = await client.queue('test-queue-v2').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    const res1 = await client
    .queue('test-queue-v2')
    .push([{ transactionId: 'test-transaction-id', data: { message: 'Hello, world!' } }])
    
    const res2 = await client
    .queue('test-queue-v2')
    .push([{ transactionId: 'test-transaction-id', data: { message: 'Hello, world!' } }])
    
    return { success: res1[0].status === 'queued' && res2[0].status === 'duplicate' }
}

export async function pushDuplicateMessageOnSpecificPartition(client) {
    const queue = await client.queue('test-queue-partition-duplicate').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    const res1 = await client
    .queue('test-queue-partition-duplicate')
    .partition('0')
    .push([{ transactionId: 'test-transaction-id', data: { message: 'Hello, world!' } }])
    
    const res2 = await client
    .queue('test-queue-partition-duplicate')
    .partition('0')
    .push([{ transactionId: 'test-transaction-id', data: { message: 'Hello, world!' } }])

    return { success: res1[0].status === 'queued' && res2[0].status === 'duplicate' }
}

export async function pushDuplicateMessageOnDifferentPartition(client) {
    const queue = await client.queue('test-queue-partition-duplicate-different').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    const res1 = await client
    .queue('test-queue-partition-duplicate-different')
    .partition('0')
    .push([{ transactionId: 'test-transaction-id', data: { message: 'Hello, world!' } }])
    
    const res2 = await client
    .queue('test-queue-partition-duplicate-different')
    .partition('1')
    .push([{ transactionId: 'test-transaction-id', data: { message: 'Hello, world!' } }])
    const success = res1[0].status === 'queued' && res2[0].status === 'queued'

    return { success: success }
}

export async function pushMessageWithTransactionId(client) {
    const queue = await client.queue('test-queue-transaction-id').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    const res = await client
    .queue('test-queue-transaction-id')
    .push([{ transactionId: 'test-transaction-id', data: { message: 'Hello, world!' } }])

    if (res[0].status !== 'queued' && res[0].transactionId !== 'test-transaction-id') {
        return { success: false, message: 'Transaction ID mismatch' }
    }
    return { success: true }
}

export async function pushBufferedMessage(client) {
    const queue = await client.queue('test-queue-buffered').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    const res = await client
    .queue('test-queue-buffered')
    .buffer({ messageCount: 10, timeMillis: 1000 })
    .push([{ data: { message: 'Hello, world!' } }])

    const pop = await client
    .queue('test-queue-buffered')
    .batch(1)
    .wait(false)
    .pop() 

    if (pop.length !== 0) {
        return { success: false, message: 'Message not buffered' }
    }

    await new Promise(r => setTimeout(r, 2000));

    const pop2 = await client
    .queue('test-queue-buffered')
    .batch(1)
    .wait(false)
    .pop()     

    if (pop2.length === 0) {
        return { success: false, message: 'Message not popped' }
    }

    return { success: true }
}

export async function pushDelayedMessage(client) {
    const queue = await client.queue('test-queue-delayed')
    .config({ delayedProcessing: 2 })
    .create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    const res = await client
    .queue('test-queue-delayed')
    .push([{ transactionId: 'test-transaction-dealyed-id', data: { message: 'Hello, world!', aaa: '1' } }])

    const pop = await client
    .queue('test-queue-delayed')
    .batch(1)
    .wait(false)
    .pop()

    

    if (pop.length !== 0) {
        return { success: false, message: 'Message not delayed' }
    }

    await new Promise(r => setTimeout(r, 2500));

    const pop2 = await client
    .queue('test-queue-delayed')
    .batch(1)
    .wait(true)
    .pop()

    if (pop2.length === 1) {
        return { success: true, message: 'Message delayed' }
    }

    return { success: false, message: 'Message not delayed' }
}

export async function pushWindowBuffer(client) {
    const queue = await client.queue('test-queue-window-buffer')
    .config({ windowBuffer: 2 })
    .create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    const res = await client
    .queue('test-queue-window-buffer')
    .push(
        [
            { data: { message: 'Hello, world 1!' } },
            { data: { message: 'Hello, world 2!' } },
            { data: { message: 'Hello, world 3!' } }
        ])

    const pop = await client
    .queue('test-queue-window-buffer')
    .batch(1)
    .wait(false)
    .pop()

    if (pop.length !== 0) {
        return { success: false, message: 'Message not delayed' }
    }

    await new Promise(r => setTimeout(r, 2500));

    const pop2 = await client
    .queue('test-queue-window-buffer')
    .batch(4)
    .wait(false)
    .pop()
    if (pop2.length === 3) {
        return { success: true, message: 'Window buffer works correctly' }
    }

    return { success: false, message: 'Window buffer does not work correctly' }
}

export async function pushLargePayload(client) {
    const queue = await client.queue('test-queue-large-payload').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    // Create a large payload (1MB of data)
    const largeArray = new Array(10000).fill({
        id: crypto.randomBytes(16).toString('hex'),
        data: 'x'.repeat(100),
        nested: {
          field1: 'value1',
          field2: 'value2',
          field3: 'value3'
        }
    });
      
    const largePayload = {
        array: largeArray,
        metadata: {
          size: JSON.stringify(largeArray).length,
          timestamp: Date.now()
        }
    };

    const res = await client
    .queue('test-queue-large-payload')
    .push([{ data: largePayload }])

    const received = await client
    .queue('test-queue-large-payload')
    .batch(1)
    .wait(false)
    .pop()
    
    if (!received || !received[0].data || !received[0].data.array || received[0].data.array.length !== 10000) {
        return { success: false, message: 'Large payload corrupted' }
    }

    return { success: true }
}

export async function pushNullPayload(client) {
    const queue = await client.queue('test-queue-null-payload').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    const res = await client
    .queue('test-queue-null-payload')
    .push([{ data: null }])

    const received = await client
    .queue('test-queue-null-payload')
    .batch(1)
    .wait(false)
    .pop()

    if (!received || received[0].data !== null) {
        return { success: false, message: 'Null data corrupted' }
    }

    return { success: true }
}

export async function pushEmptyPayload(client) {
    const queue = await client.queue('test-queue-empty-payload').create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    const res = await client
    .queue('test-queue-empty-payload')
    .push([{ data: {} }])

    const received = await client
    .queue('test-queue-empty-payload')
    .batch(1)
    .wait(false)
    .pop()
    
    if (!received || Object.keys(received[0].data).length !== 0) {
        return { success: false, message: 'Empty object payload corrupted' }
    }

    return { success: true }
}

export async function pushEncryptedPayload(client) {
    const queue = await client.queue('test-queue-encrypted-payload')
    .config({ encryptionEnabled: true })
    .create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }
    const res = await client
    .queue('test-queue-encrypted-payload')
    .push([{ data: { message: 'Hello, world!' } }])

    const received = await client
    .queue('test-queue-encrypted-payload')
    .batch(1)
    .wait(false)
    .pop()

    
    if (!received || received[0].data.message !== 'Hello, world!') {
        return { success: false, message: 'Encrypted payload corrupted' }
    }

    return { success: true }
}