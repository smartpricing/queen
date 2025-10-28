/**
 * Resource & Status API Tests
 * Tests for querying system resources and status
 */

// Helper to make direct HTTP calls
async function httpGet(url) {
    const response = await fetch(url)
    if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`)
    }
    return await response.json()
}

export async function testListQueues(client) {
    // Create some test queues
    await client.queue('test-queue-v2-list-1').create()
    await client.queue('test-queue-v2-list-2').create()
    await client.queue('test-queue-v2-list-3').create()

    try {
        const result = await httpGet('http://localhost:6632/api/v1/resources/queues')
        
        if (!result || !Array.isArray(result)) {
            return { success: false, message: 'Invalid queue list response' }
        }

        const testQueues = result.filter(q => q.name && q.name.startsWith('test-queue-v2-list'))
        
        return { 
            success: testQueues.length >= 3,
            message: `Found ${testQueues.length} test queues` 
        }
    } catch (error) {
        return { success: false, message: `Error listing queues: ${error.message}` }
    }
}

export async function testGetQueueDetails(client) {
    const queueName = 'test-queue-v2-details'
    await client.queue(queueName)
        .config({ 
            leaseTime: 123,
            maxSize: 5000,
            retryLimit: 5 
        })
        .create()

    try {
        const result = await httpGet(`http://localhost:6632/api/v1/resources/queues/${queueName}`)
        
        if (!result || !result.name) {
            return { success: false, message: 'Invalid queue details response' }
        }

        const hasCorrectConfig = result.name === queueName
        
        return { 
            success: hasCorrectConfig,
            message: 'Queue details retrieved successfully' 
        }
    } catch (error) {
        return { success: false, message: `Error getting queue details: ${error.message}` }
    }
}

export async function testGetNamespaces(client) {
    // Create queues with different namespaces
    await client.queue('test-queue-v2-ns-1')
        .namespace('test-namespace-alpha')
        .create()
    
    await client.queue('test-queue-v2-ns-2')
        .namespace('test-namespace-beta')
        .create()

    try {
        const result = await httpGet('http://localhost:6632/api/v1/resources/namespaces')
        
        if (!result || !Array.isArray(result)) {
            return { success: false, message: 'Invalid namespaces response' }
        }

        const testNamespaces = result.filter(ns => 
            typeof ns === 'string' && ns.startsWith('test-namespace')
        )
        
        return { 
            success: testNamespaces.length >= 2,
            message: `Found ${testNamespaces.length} test namespaces` 
        }
    } catch (error) {
        return { success: false, message: `Error listing namespaces: ${error.message}` }
    }
}

export async function testGetTasks(client) {
    // Create queues with different tasks
    await client.queue('test-queue-v2-task-1')
        .task('test-task-one')
        .create()
    
    await client.queue('test-queue-v2-task-2')
        .task('test-task-two')
        .create()

    try {
        const result = await httpGet('http://localhost:6632/api/v1/resources/tasks')
        
        if (!result || !Array.isArray(result)) {
            return { success: false, message: 'Invalid tasks response' }
        }

        const testTasks = result.filter(task => 
            typeof task === 'string' && task.startsWith('test-task')
        )
        
        return { 
            success: testTasks.length >= 2,
            message: `Found ${testTasks.length} test tasks` 
        }
    } catch (error) {
        return { success: false, message: `Error listing tasks: ${error.message}` }
    }
}

export async function testGetMessages(client) {
    const queueName = 'test-queue-v2-get-messages'
    await client.queue(queueName).create()

    // Push some messages
    await client.queue(queueName).push([
        { data: { id: 1 } },
        { data: { id: 2 } },
        { data: { id: 3 } }
    ])

    try {
        const result = await httpGet(
            `http://localhost:6632/api/v1/messages?queue=${queueName}&status=pending&limit=10`
        )
        
        if (!result || !result.messages) {
            return { success: false, message: 'Invalid messages response' }
        }

        return { 
            success: result.messages.length >= 3,
            message: `Found ${result.messages.length} pending messages` 
        }
    } catch (error) {
        return { success: false, message: `Error getting messages: ${error.message}` }
    }
}

export async function testSystemOverview(client) {
    try {
        const result = await httpGet('http://localhost:6632/api/v1/resources/overview')
        
        if (!result) {
            return { success: false, message: 'Invalid overview response' }
        }

        // Check for expected overview fields
        const hasExpectedFields = 'totalQueues' in result || 'queues' in result
        
        return { 
            success: hasExpectedFields,
            message: 'System overview retrieved successfully' 
        }
    } catch (error) {
        return { success: false, message: `Error getting overview: ${error.message}` }
    }
}

export async function testHealthCheck(client) {
    try {
        const result = await httpGet('http://localhost:6632/health')
        
        if (!result || result.status !== 'healthy') {
            return { success: false, message: 'Health check failed' }
        }

        return { 
            success: true,
            message: 'Health check passed' 
        }
    } catch (error) {
        return { success: false, message: `Health check error: ${error.message}` }
    }
}

export async function testDeleteQueue(client) {
    const queueName = 'test-queue-v2-to-delete'
    await client.queue(queueName).create()

    // Verify it exists
    const queues1 = await httpGet('http://localhost:6632/api/v1/resources/queues')
    const existsBefore = queues1.some(q => q.name === queueName)

    if (!existsBefore) {
        return { success: false, message: 'Queue not created' }
    }

    // Delete it
    const deleteResult = await client.queue(queueName).delete()

    if (!deleteResult.deleted) {
        return { success: false, message: 'Queue deletion failed' }
    }

    return { 
        success: true,
        message: 'Queue deleted successfully' 
    }
}

