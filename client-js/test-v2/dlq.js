export async function testDLQ(client) { 
    const queueName = 'test-queue-v2-dlq'
    
    // 1. Create queue with low retry limit
    const queue = await client
        .queue(queueName)
        .config({
            retryLimit: 1,  // Only 1 retry before DLQ
            dlqAfterMaxRetries: true  // Enable DLQ after max retries
        })
        .create()
    
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // 2. Push a test message
    const testMessage = { message: 'Test DLQ message', timestamp: Date.now() }
    await client
        .queue(queueName)
        .push([{ data: testMessage }])

    // 3. Consume and fail the message (will trigger retry then DLQ)
    await client
        .queue(queueName)
        .batch(1)
        .wait(false)
        .limit(1)  // Process up to 2 messages (original + retry)
        .each()
        .consume(async msg => {
            // Always fail to trigger DLQ
            throw new Error('Test error - triggering DLQ')
        })
        .onError(async (msg, error) => {
            // Ack as failed to trigger DLQ logic
            await client.ack(msg, false, { error: error.message })
        })

    // 4. Wait a moment for DLQ processing
    await new Promise(resolve => setTimeout(resolve, 500))
    // 5. Check DLQ for the message
    const dlqResult = await client
        .queue(queueName)
        .dlq()
        .limit(10)
        .get()

    console.log('DLQ result: ', JSON.stringify(dlqResult, null, 2))

    if (!dlqResult || !dlqResult.messages) {
        return { success: false, message: 'Failed to query DLQ' }
    }

    if (dlqResult.messages.length === 0) {
        return { 
            success: false, 
            message: `No messages in DLQ. Expected at least 1 message. Total: ${dlqResult.total}` 
        }
    }

    // 6. Verify the message content
    const dlqMessage = dlqResult.messages[0]
    
    if (!dlqMessage.data || dlqMessage.data.message !== testMessage.message) {
        return { 
            success: false, 
            message: 'DLQ message data does not match expected content' 
        }
    }

    if (!dlqMessage.errorMessage || !dlqMessage.errorMessage.includes('Test error')) {
        return { 
            success: false, 
            message: 'DLQ message missing or incorrect error message' 
        }
    }

    return { 
        success: true, 
        message: `DLQ test completed successfully. Found ${dlqResult.total} message(s) in DLQ` 
    }
}
