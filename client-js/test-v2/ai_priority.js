/**
 * Priority Queue Tests
 * Tests for message priority handling
 */

export async function testBasicPriority(client) {
    // Create queues with different priorities
    const highPriorityQueue = await client.queue('test-queue-v2-priority-high')
        .config({ priority: 10 })
        .create()
    
    const lowPriorityQueue = await client.queue('test-queue-v2-priority-low')
        .config({ priority: 1 })
        .create()

    if (!highPriorityQueue.configured || !lowPriorityQueue.configured) {
        return { success: false, message: 'Queues not created' }
    }

    // Push to low priority first
    await client.queue('test-queue-v2-priority-low')
        .push([{ data: { priority: 'low', message: 'Should process second' } }])

    // Then push to high priority
    await client.queue('test-queue-v2-priority-high')
        .push([{ data: { priority: 'high', message: 'Should process first' } }])

    // Note: Testing actual priority processing order requires namespace/task filtering
    // as pop() from specific queue will only get that queue's messages
    
    return { 
        success: true,
        message: 'Priority queues configured successfully' 
    }
}

export async function testPriorityWithNamespace(client) {
    const namespace = 'test-priority-namespace'
    
    // Create multiple queues in same namespace with different priorities
    await client.queue('test-queue-v2-pri-ns-high')
        .namespace(namespace)
        .config({ priority: 10 })
        .create()
    
    await client.queue('test-queue-v2-pri-ns-medium')
        .namespace(namespace)
        .config({ priority: 5 })
        .create()
    
    await client.queue('test-queue-v2-pri-ns-low')
        .namespace(namespace)
        .config({ priority: 1 })
        .create()

    // Push messages in reverse priority order
    await client.queue('test-queue-v2-pri-ns-low')
        .push([{ data: { order: 'third', value: 1 } }])
    
    await client.queue('test-queue-v2-pri-ns-medium')
        .push([{ data: { order: 'second', value: 5 } }])
    
    await client.queue('test-queue-v2-pri-ns-high')
        .push([{ data: { order: 'first', value: 10 } }])

    // Pop from namespace (should respect priority)
    const messages = []
    await client.queue()
        .namespace(namespace)
        .batch(3)
        .wait(false)
        .limit(1)
        .consume(async (msgs) => {
            messages.push(...msgs)
        })

    // Messages should be in priority order
    if (messages.length !== 3) {
        return { success: false, message: `Expected 3 messages, got ${messages.length}` }
    }

    // High priority should come first
    const firstIsHighPriority = messages[0].data.value === 10
    
    return { 
        success: firstIsHighPriority,
        message: `Priority ordering: ${messages.map(m => m.data.order).join(', ')}` 
    }
}

export async function testPriorityWithTask(client) {
    const task = 'test-priority-task'
    
    // Create queues with same task but different priorities
    await client.queue('test-queue-v2-pri-task-urgent')
        .task(task)
        .config({ priority: 100 })
        .create()
    
    await client.queue('test-queue-v2-pri-task-normal')
        .task(task)
        .config({ priority: 50 })
        .create()

    // Push to normal first
    await client.queue('test-queue-v2-pri-task-normal')
        .push([{ data: { urgency: 'normal' } }])
    
    // Then urgent
    await client.queue('test-queue-v2-pri-task-urgent')
        .push([{ data: { urgency: 'urgent' } }])

    // Pop from task
    const messages = []
    await client.queue()
        .task(task)
        .batch(2)
        .wait(false)
        .limit(1)
        .consume(async (msgs) => {
            messages.push(...msgs)
        })

    if (messages.length !== 2) {
        return { success: false, message: `Expected 2 messages, got ${messages.length}` }
    }

    // Urgent should come first
    const urgentFirst = messages[0].data.urgency === 'urgent'
    
    return { 
        success: urgentFirst,
        message: 'Task-based priority ordering works' 
    }
}

export async function testDynamicPriorityChange(client) {
    const queueName = 'test-queue-v2-priority-dynamic'
    
    // Create with low priority
    await client.queue(queueName)
        .config({ priority: 1 })
        .create()

    // Push a message
    await client.queue(queueName)
        .push([{ data: { phase: 'low-priority' } }])

    // Reconfigure with high priority
    await client.queue(queueName)
        .config({ priority: 10 })
        .create()

    // Push another message
    await client.queue(queueName)
        .push([{ data: { phase: 'high-priority' } }])

    // Pop messages
    const messages = await client.queue(queueName)
        .batch(2)
        .wait(false)
        .pop()

    return { 
        success: messages.length === 2,
        message: 'Dynamic priority change handled' 
    }
}

