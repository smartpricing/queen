export async function createQueue(client) {
    const res = await client.queue('test-queue-v2').create()
    return { success: res.configured === true }
}

export async function deleteQueue(client) {
    const res = await client.queue('test-queue-v2').delete()
    return { success: res.deleted === true }
}

export async function configureQueue(client) {
    const config = {
            completedRetentionSeconds: 1,
            deadLetterQueue: true,
            delayedProcessing: 1,
            dlqAfterMaxRetries: true,
            encryptionEnabled: true,
            leaseTime: 30,
            maxSize: 1000,
            maxWaitTimeSeconds: 10,
            priority: 5,
            retentionEnabled: false,
            retentionSeconds: 0,
            retryDelay: 1000,
            retryLimit: 3,
            ttl: 36,
            windowBuffer: 100
    }
    const res = await client.queue('test-queue-v2').config(config).create()
    if (!res.configured) {
        return { success: false, message: 'Queue not configured' }
    }
    for (const key in config) {
        if (res.options[key] !== config[key]) {
            return { success: false, message: `Queue configuration mismatch for ${key}` }
        }
    }
    return { success: res.configured === true }
}