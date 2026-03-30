/**
 * Watermark Bug Tests
 *
 * These tests verify that consumer_watermarks are properly reset when:
 * 1. Seeking a consumer group backwards to a timestamp
 * 2. Deleting a consumer group (watermark should be cleaned up)
 *
 * The consumer_watermarks table optimizes POP by tracking when a consumer
 * last found the queue empty. However, if the watermark isn't reset when
 * the cursor moves backwards, old messages become invisible.
 *
 * BUG: Currently, seek and delete operations don't touch consumer_watermarks,
 * causing re-consumption to fail silently.
 *
 * IMPORTANT: The watermark filter has a 2-minute buffer:
 *   pl.updated_at >= (v_watermark - interval '2 minutes')
 *
 * So the bug only manifests after 2+ minutes have passed. To test this without
 * waiting, we directly manipulate the watermark timestamp in the database.
 *
 * Run: node run.js seekBackwardsAllowsReconsume
 *      node run.js deleteConsumerGroupAllowsReconsume
 */

import { dbPool } from './run.js'

const QUEUE_NAME_SEEK = 'test-queue-v2-watermark-seek'
const QUEUE_NAME_DELETE = 'test-queue-v2-watermark-delete'

/**
 * Helper: Move the watermark FORWARD in time to simulate that partitions
 * are "old" relative to the watermark.
 *
 * The filter is: pl.updated_at >= (v_watermark - interval '2 minutes')
 *
 * If watermark = NOW + 10 minutes, then:
 *   filter = pl.updated_at >= (NOW + 10 - 2) = pl.updated_at >= NOW + 8
 *
 * Partitions updated at NOW will fail this filter (NOW < NOW + 8)
 */
async function advanceWatermark(queueName, consumerGroup, minutesForward = 10) {
    const result = await dbPool.query(`
        UPDATE queen.consumer_watermarks
        SET last_empty_scan_at = NOW() + interval '${minutesForward} minutes',
            updated_at = NOW() + interval '${minutesForward} minutes'
        WHERE queue_name = $1 AND consumer_group = $2
        RETURNING *
    `, [queueName, consumerGroup])
    return result.rows[0]
}

/**
 * Helper: Check if watermark exists for a queue/consumer_group
 */
async function getWatermark(queueName, consumerGroup) {
    const result = await dbPool.query(`
        SELECT * FROM queen.consumer_watermarks
        WHERE queue_name = $1 AND consumer_group = $2
    `, [queueName, consumerGroup])
    return result.rows[0]
}

/**
 * Test: Seeking backwards should allow re-consumption of messages
 *
 * Steps:
 * 1. Push messages to queue
 * 2. Consume all messages with a consumer group (watermark advances)
 * 3. Artificially age the watermark (simulate 10 minutes passing)
 * 4. Seek the consumer group backwards to before the messages
 * 5. Try to consume again - should get all messages
 *
 * Expected: All messages are re-consumed after seek
 * Actual (bug): Watermark blocks re-consumption, 0 messages returned
 */
export async function seekBackwardsAllowsReconsume(client) {
    const consumerGroup = 'watermark-seek-test-cg'

    // Cleanup
    try {
        await client.deleteConsumerGroup(consumerGroup, true)
        // Also clean up any orphaned watermark from previous runs
        await dbPool.query(
            'DELETE FROM queen.consumer_watermarks WHERE consumer_group = $1',
            [consumerGroup]
        )
    } catch (e) {
        // Ignore
    }

    // 1. Create queue
    const queue = await client.queue(QUEUE_NAME_SEEK).create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // 2. Record timestamp BEFORE pushing messages
    const timestampBeforeMessages = new Date().toISOString()

    // Small delay to ensure timestamp ordering
    await new Promise(resolve => setTimeout(resolve, 100))

    // 3. Push messages
    const messageCount = 10
    for (let i = 0; i < messageCount; i++) {
        await client
            .queue(QUEUE_NAME_SEEK)
            .partition(`p-${i}`)
            .push([{ data: { id: i, batch: 'original' } }])
    }

    console.log(`  Pushed ${messageCount} messages`)

    // 4. Consume all messages with consumer group
    let firstConsumeCount = 0
    await client
        .queue(QUEUE_NAME_SEEK)
        .group(consumerGroup)
        .subscriptionMode('all')
        .concurrency(5)
        .batch(1)
        .wait(false)
        .idleMillis(3000)
        .consume(async msgs => {
            firstConsumeCount += msgs.length
        })

    console.log(`  First consume: ${firstConsumeCount} messages`)

    if (firstConsumeCount !== messageCount) {
        return {
            success: false,
            message: `First consume should get ${messageCount} messages, got ${firstConsumeCount}`
        }
    }

    // 5. Verify queue is empty for this consumer (triggers watermark advancement)
    const emptyCheck = await client
        .queue(QUEUE_NAME_SEEK)
        .group(consumerGroup)
        .batch(10)
        .wait(false)
        .pop()

    if (emptyCheck.length !== 0) {
        return {
            success: false,
            message: `Queue should be empty after first consume, got ${emptyCheck.length} messages`
        }
    }

    console.log(`  Verified queue is empty (watermark should be set)`)

    // 6. Check watermark was created
    const watermarkBefore = await getWatermark(QUEUE_NAME_SEEK, consumerGroup)
    if (!watermarkBefore) {
        return {
            success: false,
            message: 'Watermark was not created after empty queue check'
        }
    }
    console.log(`  Watermark exists: last_empty_scan_at = ${watermarkBefore.last_empty_scan_at}`)

    // 7. CRITICAL: Advance the watermark forward to simulate that partitions are "old"
    //    relative to the watermark. This triggers the bug where old partitions are filtered out.
    const advancedWatermark = await advanceWatermark(QUEUE_NAME_SEEK, consumerGroup, 10)
    console.log(`  Advanced watermark to: ${advancedWatermark.last_empty_scan_at} (10 minutes in future)`)

    // 8. Seek backwards to BEFORE the messages were pushed
    console.log(`  Seeking to timestamp: ${timestampBeforeMessages}`)

    const seekResult = await client.admin.seekConsumerGroup(
        consumerGroup,
        QUEUE_NAME_SEEK,
        { timestamp: timestampBeforeMessages }
    )

    console.log(`  Seek result: ${JSON.stringify(seekResult)}`)

    // 9. Check if watermark was reset (it should be, but currently isn't - that's the bug)
    const watermarkAfterSeek = await getWatermark(QUEUE_NAME_SEEK, consumerGroup)
    if (watermarkAfterSeek) {
        console.log(`  WARNING: Watermark still exists after seek: ${watermarkAfterSeek.last_empty_scan_at}`)
    } else {
        console.log(`  Watermark was properly deleted after seek`)
    }

    // Small delay after seek
    await new Promise(resolve => setTimeout(resolve, 500))

    // 10. Try to consume again - should get all messages back
    let secondConsumeCount = 0
    await client
        .queue(QUEUE_NAME_SEEK)
        .group(consumerGroup)
        .concurrency(5)
        .batch(1)
        .wait(false)
        .idleMillis(3000)
        .consume(async msgs => {
            secondConsumeCount += msgs.length
        })

    console.log(`  Second consume (after seek): ${secondConsumeCount} messages`)

    // 11. Verify we got all messages back
    if (secondConsumeCount !== messageCount) {
        return {
            success: false,
            message: `After seek backwards, should re-consume ${messageCount} messages, but got ${secondConsumeCount}. ` +
                     `This is because consumer_watermarks was not reset on seek (watermark still at ${watermarkAfterSeek?.last_empty_scan_at}).`
        }
    }

    return {
        success: true,
        message: `Seek backwards allowed re-consumption of ${secondConsumeCount} messages`
    }
}

/**
 * Test: Deleting consumer group should allow fresh consumption
 *
 * Steps:
 * 1. Push messages to queue
 * 2. Consume all messages with a consumer group (watermark advances)
 * 3. Artificially age the watermark (simulate 10 minutes passing)
 * 4. Delete the consumer group
 * 5. Create a new consumer group with the SAME name
 * 6. Try to consume - should get all messages
 *
 * Expected: All messages are consumed by the "new" consumer group
 * Actual (bug): Orphaned watermark blocks consumption, 0 messages returned
 */
export async function deleteConsumerGroupAllowsReconsume(client) {
    const consumerGroup = 'watermark-delete-test-cg'

    // Cleanup
    try {
        await client.deleteConsumerGroup(consumerGroup, true)
        // Also clean up any orphaned watermark from previous runs
        await dbPool.query(
            'DELETE FROM queen.consumer_watermarks WHERE consumer_group = $1',
            [consumerGroup]
        )
    } catch (e) {
        // Ignore
    }

    // 1. Create queue
    const queue = await client.queue(QUEUE_NAME_DELETE).create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // 2. Push messages
    const messageCount = 10
    for (let i = 0; i < messageCount; i++) {
        await client
            .queue(QUEUE_NAME_DELETE)
            .partition(`p-${i}`)
            .push([{ data: { id: i, batch: 'original' } }])
    }

    console.log(`  Pushed ${messageCount} messages`)

    // 3. Consume all messages with consumer group
    let firstConsumeCount = 0
    await client
        .queue(QUEUE_NAME_DELETE)
        .group(consumerGroup)
        .subscriptionMode('all')
        .concurrency(5)
        .batch(1)
        .wait(false)
        .idleMillis(3000)
        .consume(async msgs => {
            firstConsumeCount += msgs.length
        })

    console.log(`  First consume: ${firstConsumeCount} messages`)

    if (firstConsumeCount !== messageCount) {
        return {
            success: false,
            message: `First consume should get ${messageCount} messages, got ${firstConsumeCount}`
        }
    }

    // 4. Verify queue is empty for this consumer (triggers watermark advancement)
    const emptyCheck = await client
        .queue(QUEUE_NAME_DELETE)
        .group(consumerGroup)
        .batch(10)
        .wait(false)
        .pop()

    if (emptyCheck.length !== 0) {
        return {
            success: false,
            message: `Queue should be empty after first consume, got ${emptyCheck.length} messages`
        }
    }

    console.log(`  Verified queue is empty (watermark should be set)`)

    // 5. Check watermark was created
    const watermarkBefore = await getWatermark(QUEUE_NAME_DELETE, consumerGroup)
    if (!watermarkBefore) {
        return {
            success: false,
            message: 'Watermark was not created after empty queue check'
        }
    }
    console.log(`  Watermark exists: last_empty_scan_at = ${watermarkBefore.last_empty_scan_at}`)

    // 6. CRITICAL: Advance the watermark forward to simulate that partitions are "old"
    const advancedWatermark = await advanceWatermark(QUEUE_NAME_DELETE, consumerGroup, 10)
    console.log(`  Advanced watermark to: ${advancedWatermark.last_empty_scan_at} (10 minutes in future)`)

    // 7. Delete the consumer group
    console.log(`  Deleting consumer group: ${consumerGroup}`)

    const deleteResult = await client.deleteConsumerGroup(consumerGroup, true)
    console.log(`  Delete result: ${JSON.stringify(deleteResult)}`)

    // 8. Check if watermark was deleted (it should be, but currently isn't - that's the bug)
    const watermarkAfterDelete = await getWatermark(QUEUE_NAME_DELETE, consumerGroup)
    if (watermarkAfterDelete) {
        console.log(`  WARNING: Orphaned watermark still exists: ${watermarkAfterDelete.last_empty_scan_at}`)
    } else {
        console.log(`  Watermark was properly deleted`)
    }

    // Small delay after delete
    await new Promise(resolve => setTimeout(resolve, 500))

    // 9. Create a "new" consumer group with the same name and try to consume
    //    Since partition_consumers was deleted, this should start fresh
    //    But if consumer_watermarks wasn't deleted, it will skip all messages
    let secondConsumeCount = 0
    await client
        .queue(QUEUE_NAME_DELETE)
        .group(consumerGroup)
        .subscriptionMode('all')
        .concurrency(5)
        .batch(1)
        .wait(false)
        .idleMillis(3000)
        .consume(async msgs => {
            secondConsumeCount += msgs.length
        })

    console.log(`  Second consume (after delete + recreate): ${secondConsumeCount} messages`)

    // 10. Verify we got all messages
    if (secondConsumeCount !== messageCount) {
        return {
            success: false,
            message: `After deleting CG, new CG with same name should consume ${messageCount} messages, ` +
                     `but got ${secondConsumeCount}. Orphaned watermark at ${watermarkAfterDelete?.last_empty_scan_at}.`
        }
    }

    return {
        success: true,
        message: `Delete + recreate allowed fresh consumption of ${secondConsumeCount} messages`
    }
}

/**
 * Test: Deleting consumer group for specific queue should allow fresh consumption
 *
 * Similar to above but uses deleteConsumerGroupForQueue instead of deleteConsumerGroup
 */
export async function deleteConsumerGroupForQueueAllowsReconsume(client) {
    const consumerGroup = 'watermark-delete-queue-test-cg'
    const queueName = 'test-queue-v2-watermark-delete-queue'

    // Cleanup
    try {
        await client.admin.deleteConsumerGroupForQueue(consumerGroup, queueName, true)
        // Also clean up any orphaned watermark from previous runs
        await dbPool.query(
            'DELETE FROM queen.consumer_watermarks WHERE queue_name = $1 AND consumer_group = $2',
            [queueName, consumerGroup]
        )
    } catch (e) {
        // Ignore
    }

    // 1. Create queue
    const queue = await client.queue(queueName).create()
    if (!queue.configured) {
        return { success: false, message: 'Queue not created' }
    }

    // 2. Push messages
    const messageCount = 10
    for (let i = 0; i < messageCount; i++) {
        await client
            .queue(queueName)
            .partition(`p-${i}`)
            .push([{ data: { id: i, batch: 'original' } }])
    }

    console.log(`  Pushed ${messageCount} messages`)

    // 3. Consume all messages with consumer group
    let firstConsumeCount = 0
    await client
        .queue(queueName)
        .group(consumerGroup)
        .subscriptionMode('all')
        .concurrency(5)
        .batch(1)
        .wait(false)
        .idleMillis(3000)
        .consume(async msgs => {
            firstConsumeCount += msgs.length
        })

    console.log(`  First consume: ${firstConsumeCount} messages`)

    if (firstConsumeCount !== messageCount) {
        return {
            success: false,
            message: `First consume should get ${messageCount} messages, got ${firstConsumeCount}`
        }
    }

    // 4. Verify queue is empty (triggers watermark)
    const emptyCheck = await client
        .queue(queueName)
        .group(consumerGroup)
        .batch(10)
        .wait(false)
        .pop()

    if (emptyCheck.length !== 0) {
        return {
            success: false,
            message: `Queue should be empty after first consume, got ${emptyCheck.length} messages`
        }
    }

    console.log(`  Verified queue is empty (watermark should be set)`)

    // 5. Check watermark was created
    const watermarkBefore = await getWatermark(queueName, consumerGroup)
    if (!watermarkBefore) {
        return {
            success: false,
            message: 'Watermark was not created after empty queue check'
        }
    }
    console.log(`  Watermark exists: last_empty_scan_at = ${watermarkBefore.last_empty_scan_at}`)

    // 6. CRITICAL: Advance the watermark forward
    const advancedWatermark = await advanceWatermark(queueName, consumerGroup, 10)
    console.log(`  Advanced watermark to: ${advancedWatermark.last_empty_scan_at} (10 minutes in future)`)

    // 7. Delete consumer group for this specific queue
    console.log(`  Deleting consumer group for queue: ${consumerGroup} / ${queueName}`)

    const deleteResult = await client.admin.deleteConsumerGroupForQueue(consumerGroup, queueName, true)
    console.log(`  Delete result: ${JSON.stringify(deleteResult)}`)

    // 8. Check if watermark was deleted
    const watermarkAfterDelete = await getWatermark(queueName, consumerGroup)
    if (watermarkAfterDelete) {
        console.log(`  WARNING: Orphaned watermark still exists: ${watermarkAfterDelete.last_empty_scan_at}`)
    } else {
        console.log(`  Watermark was properly deleted`)
    }

    await new Promise(resolve => setTimeout(resolve, 500))

    // 9. Try to consume again
    let secondConsumeCount = 0
    await client
        .queue(queueName)
        .group(consumerGroup)
        .subscriptionMode('all')
        .concurrency(5)
        .batch(1)
        .wait(false)
        .idleMillis(3000)
        .consume(async msgs => {
            secondConsumeCount += msgs.length
        })

    console.log(`  Second consume (after delete): ${secondConsumeCount} messages`)

    if (secondConsumeCount !== messageCount) {
        return {
            success: false,
            message: `After deleting CG for queue, should consume ${messageCount} messages, ` +
                     `but got ${secondConsumeCount}. Orphaned watermark at ${watermarkAfterDelete?.last_empty_scan_at}.`
        }
    }

    return {
        success: true,
        message: `Delete CG for queue allowed fresh consumption of ${secondConsumeCount} messages`
    }
}
