export async function testComplete(client) { 
    const queueInit = await client.queue('test-queue-v2-complete-init').create()
    const queueNext = await client.queue('test-queue-v2-complete-next').create()
    const queueFinal = await client.queue('test-queue-v2-complete-final').create()
    if (!queueInit.configured || !queueNext.configured || !queueFinal.configured) {
        return { success: false, message: 'Queue not created' }
    }

    await client
    .queue('test-queue-v2-complete-init')
    .push([
        { data: { message: 'First', count: 0 } 
    }])

    await client
    .queue('test-queue-v2-complete-init')
    .batch(1)
    .wait(false)
    .limit(1)
    .each()
    .autoAck(false)  // Disable auto-ack since we're manually acking in the transaction
    .consume(async msg => {
        await client
        .transaction()
        .queue('test-queue-v2-complete-next')
        .push([{ data: { message: 'Next', count: msg.data.count + 1 } }])
        .ack(msg)
        .commit()
    })

    await client
    .queue('test-queue-v2-complete-next')
    .batch(1)
    .wait(false)
    .limit(1)
    .each()
    .autoAck(false)  // Disable auto-ack since we're manually acking in the transaction
    .consume(async msg => {
        await client
        .transaction()
        .queue('test-queue-v2-complete-final')
        .push([{ data: { message: 'Final', count: msg.data.count + 1 } }])
        .ack(msg)
        .commit()
    })

    let finalMessage = null
    await client
    .queue('test-queue-v2-complete-final')
    .batch(1)
    .wait(false)
    .limit(1)
    .each()
    .consume(async msg => {
        finalMessage = msg
    })

    return { success: finalMessage !== null, message: 'Complete test completed successfully' }
}