/**
 * Lease Renewal Tests
 * Tests manual lease extension functionality
 */

export async function testManualLeaseRenewal(client) {
    const queue = await client.queue('test-queue-v2-manual-renewal')
        .config({ leaseTime: 2 })
        .create()
    
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    await client.queue('test-queue-v2-manual-renewal')
        .push([{ data: { test: 'renewal' } }])

    const messages = await client.queue('test-queue-v2-manual-renewal')
        .batch(1)
        .wait(false)
        .pop()

    if (messages.length === 0) {
        return { success: false, message: 'No message to test' }
    }

    const message = messages[0]

    // Wait 1 second
    await new Promise(resolve => setTimeout(resolve, 1000))

    // Renew the lease
    const renewResult = await client.renew(message)

    if (!renewResult.success) {
        return { success: false, message: 'Lease renewal failed' }
    }

    // Wait another 2 seconds (total 3 seconds, original lease was 2 seconds)
    await new Promise(resolve => setTimeout(resolve, 2000))

    // Try to ack - should succeed because we renewed
    const ackResult = await client.ack(message, true)

    return { 
        success: ackResult.success === true,
        message: 'Manual lease renewal works correctly' 
    }
}

export async function testBatchLeaseRenewal(client) {
    const queue = await client.queue('test-queue-v2-batch-renewal')
        .config({ leaseTime: 2 })
        .create()
    
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    await client.queue('test-queue-v2-batch-renewal').push([
        { data: { id: 1 } },
        { data: { id: 2 } },
        { data: { id: 3 } }
    ])

    const messages = await client.queue('test-queue-v2-batch-renewal')
        .batch(3)
        .wait(false)
        .pop()

    if (messages.length !== 3) {
        return { success: false, message: 'Expected 3 messages' }
    }

    // Wait 1 second
    await new Promise(resolve => setTimeout(resolve, 1000))

    // Renew all leases
    const renewResults = await client.renew(messages)

    const allSuccess = renewResults.every(r => r.success === true)

    if (!allSuccess) {
        return { success: false, message: 'Batch renewal failed' }
    }

    // Wait another 2 seconds
    await new Promise(resolve => setTimeout(resolve, 2000))

    // Try to ack all - should succeed
    const ackResult = await client.ack(messages, true)

    return { 
        success: ackResult.success === true,
        message: 'Batch lease renewal works correctly' 
    }
}

export async function testRenewalWithLeaseId(client) {
    const queue = await client.queue('test-queue-v2-renewal-leaseid')
        .config({ leaseTime: 2 })
        .create()
    
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    await client.queue('test-queue-v2-renewal-leaseid')
        .push([{ data: { test: 'data' } }])

    const messages = await client.queue('test-queue-v2-renewal-leaseid')
        .batch(1)
        .wait(false)
        .pop()

    if (messages.length === 0 || !messages[0].leaseId) {
        return { success: false, message: 'No message or leaseId' }
    }

    const leaseId = messages[0].leaseId

    // Renew using just the leaseId string
    const renewResult = await client.renew(leaseId)

    return { 
        success: renewResult.success === true,
        message: 'Lease renewal with leaseId string works' 
    }
}

export async function testRenewalOfExpiredLease(client) {
    const queue = await client.queue('test-queue-v2-renewal-expired')
        .config({ leaseTime: 1 })
        .create()
    
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    await client.queue('test-queue-v2-renewal-expired')
        .push([{ data: { test: 'data' } }])

    const messages = await client.queue('test-queue-v2-renewal-expired')
        .batch(1)
        .wait(false)
        .pop()

    if (messages.length === 0) {
        return { success: false, message: 'No message to test' }
    }

    // Wait for lease to expire
    await new Promise(resolve => setTimeout(resolve, 2000))

    // Try to renew expired lease
    const renewResult = await client.renew(messages[0])

    // Should fail
    return { 
        success: renewResult.success === false,
        message: 'Expired lease renewal correctly rejected' 
    }
}

export async function testMultipleRenewals(client) {
    const queue = await client.queue('test-queue-v2-multiple-renewals')
        .config({ leaseTime: 2 })
        .create()
    
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    await client.queue('test-queue-v2-multiple-renewals')
        .push([{ data: { test: 'data' } }])

    const messages = await client.queue('test-queue-v2-multiple-renewals')
        .batch(1)
        .wait(false)
        .pop()

    if (messages.length === 0) {
        return { success: false, message: 'No message to test' }
    }

    const message = messages[0]

    // Renew multiple times
    await new Promise(resolve => setTimeout(resolve, 1000))
    const renew1 = await client.renew(message)

    await new Promise(resolve => setTimeout(resolve, 1000))
    const renew2 = await client.renew(message)

    await new Promise(resolve => setTimeout(resolve, 1000))
    const renew3 = await client.renew(message)

    // After 3 seconds, ack should still work
    const ackResult = await client.ack(message, true)

    return { 
        success: renew1.success && renew2.success && renew3.success && ackResult.success,
        message: 'Multiple lease renewals work correctly' 
    }
}

