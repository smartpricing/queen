/**
 * TypeScript integration tests for Queen MQ client
 * Tests all major functionality against a running Queen server on localhost:6632
 *
 * Run: npx tsx test-ts/run.ts
 */

import {
  Queen,
  Admin,
  QueueBuilder,
  TransactionBuilder,
  PushBuilder,
  OperationBuilder,
  ConsumeBuilder,
  DLQBuilder,
  generateUUID,
  CLIENT_DEFAULTS,
  QUEUE_DEFAULTS,
  CONSUME_DEFAULTS,
  POP_DEFAULTS,
  BUFFER_DEFAULTS,
  type QueenConfig,
  type QueenMessage,
  type QueueConfig,
  type BufferOptions,
  type AckResult,
  type RenewResult,
  type BufferStats,
  type PushResult,
  type ConsumeOptions,
  type TraceConfig,
  type DLQResult,
} from '../dist/index.js'

// ===========================
// Test Infrastructure
// ===========================

const SERVER_URL = process.env.QUEEN_URL || 'http://localhost:6632'

interface TestResult {
  success: boolean
  message?: string
}

const testResults: { name: string; success: boolean; message: string; durationMs: number }[] = []

function log(success: boolean, ...args: unknown[]) {
  console.log(new Date().toISOString(), success ? '\x1b[32m✅\x1b[0m' : '\x1b[31m❌\x1b[0m', ...args)
}

async function runTest(name: string, fn: (client: Queen) => Promise<TestResult>, client: Queen) {
  const start = Date.now()
  try {
    const result = await fn(client)
    const duration = Date.now() - start
    const message = result.message || 'OK'
    testResults.push({ name, success: result.success, message, durationMs: duration })
    log(result.success, `${name} (${duration}ms)`, message)
  } catch (error) {
    const duration = Date.now() - start
    const message = `Threw: ${(error as Error).message}`
    testResults.push({ name, success: false, message, durationMs: duration })
    log(false, `${name} (${duration}ms)`, message)
  }
}

function printResults() {
  console.log('\n' + '='.repeat(80))
  console.log('RESULTS:')
  console.log('='.repeat(80))
  for (const r of testResults) {
    console.log(`${r.success ? '✅' : '❌'} ${r.name} (${r.durationMs}ms): ${r.message}`)
  }
  const passed = testResults.filter(r => r.success).length
  const failed = testResults.filter(r => !r.success).length
  console.log('='.repeat(80))
  console.log(`\x1b[1m${passed}/${testResults.length} passed, ${failed} failed\x1b[0m`)
  console.log('='.repeat(80))
}

// ===========================
// TYPE SYSTEM TESTS (compile-time)
// ===========================

async function testTypeExports(_client: Queen): Promise<TestResult> {
  // Verify all types are accessible (compile-time check)
  const config: QueenConfig = { url: SERVER_URL }
  const queueConfig: QueueConfig = { leaseTime: 300, retryLimit: 3 }
  const bufferOpts: BufferOptions = { messageCount: 100, timeMillis: 1000 }
  const traceConfig: TraceConfig = { data: { test: true } }

  // Verify defaults are exported correctly
  if (CLIENT_DEFAULTS.timeoutMillis !== 30000) return { success: false, message: 'CLIENT_DEFAULTS wrong' }
  if (QUEUE_DEFAULTS.leaseTime !== 300) return { success: false, message: 'QUEUE_DEFAULTS wrong' }
  if (CONSUME_DEFAULTS.concurrency !== 1) return { success: false, message: 'CONSUME_DEFAULTS wrong' }
  if (POP_DEFAULTS.batch !== 1) return { success: false, message: 'POP_DEFAULTS wrong' }
  if (BUFFER_DEFAULTS.messageCount !== 100) return { success: false, message: 'BUFFER_DEFAULTS wrong' }

  // Verify generateUUID works
  const uuid = generateUUID()
  if (typeof uuid !== 'string' || uuid.length < 30) return { success: false, message: `UUID invalid: ${uuid}` }

  // Verify class constructors are exported
  if (typeof Queen !== 'function') return { success: false, message: 'Queen not exported' }

  // Suppress unused variable warnings
  void config; void queueConfig; void bufferOpts; void traceConfig

  return { success: true, message: 'All types and exports verified' }
}

// ===========================
// CLIENT INITIALIZATION TESTS
// ===========================

async function testClientStringUrl(_client: Queen): Promise<TestResult> {
  const q = new Queen(SERVER_URL)
  const stats = q.getBufferStats()
  if (stats.activeBuffers !== 0) return { success: false, message: 'Should start with 0 buffers' }
  await q.close()
  return { success: true, message: 'String URL constructor works' }
}

async function testClientArrayUrls(_client: Queen): Promise<TestResult> {
  const q = new Queen([SERVER_URL])
  const stats = q.getBufferStats()
  if (stats.activeBuffers !== 0) return { success: false, message: 'Should start with 0 buffers' }
  await q.close()
  return { success: true, message: 'Array URL constructor works' }
}

async function testClientObjectConfig(_client: Queen): Promise<TestResult> {
  const q = new Queen({ url: SERVER_URL, timeoutMillis: 5000, retryAttempts: 2 })
  const stats = q.getBufferStats()
  if (stats.activeBuffers !== 0) return { success: false, message: 'Should start with 0 buffers' }
  await q.close()
  return { success: true, message: 'Object config constructor works' }
}

async function testClientInvalidUrl(_client: Queen): Promise<TestResult> {
  try {
    new Queen('not-a-url')
    return { success: false, message: 'Should have thrown' }
  } catch (e) {
    return { success: true, message: 'Correctly rejects invalid URL' }
  }
}

async function testClientNoUrl(_client: Queen): Promise<TestResult> {
  try {
    new Queen({})
    return { success: false, message: 'Should have thrown' }
  } catch (e) {
    return { success: true, message: 'Correctly rejects missing URL' }
  }
}

// ===========================
// QUEUE CREATE/DELETE TESTS
// ===========================

async function testQueueCreate(client: Queen): Promise<TestResult> {
  const res = await client.queue('ts-test-create').create() as { configured?: boolean }
  return { success: res.configured === true, message: res.configured ? 'Queue created' : 'Queue not created' }
}

async function testQueueCreateWithConfig(client: Queen): Promise<TestResult> {
  const res = await client
    .queue('ts-test-config')
    .config({
      leaseTime: 60,
      retryLimit: 5,
      priority: 1,
      encryptionEnabled: false
    })
    .create() as { configured?: boolean; options?: Record<string, unknown> }

  if (!res.configured) return { success: false, message: 'Queue not configured' }
  if (res.options?.leaseTime !== 60) return { success: false, message: `leaseTime mismatch: ${res.options?.leaseTime}` }
  if (res.options?.retryLimit !== 5) return { success: false, message: `retryLimit mismatch: ${res.options?.retryLimit}` }

  return { success: true, message: 'Queue configured with custom options' }
}

async function testQueueDelete(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-delete').create()
  const res = await client.queue('ts-test-delete').delete() as { deleted?: boolean }
  return { success: res.deleted === true, message: res.deleted ? 'Queue deleted' : 'Queue not deleted' }
}

async function testQueueCreateOnSuccess(client: Queen): Promise<TestResult> {
  let callbackCalled = false
  await client.queue('ts-test-onsuccess')
    .create()
    .onSuccess(async (_result) => {
      callbackCalled = true
    })

  return { success: callbackCalled, message: callbackCalled ? 'onSuccess called' : 'onSuccess NOT called' }
}

// ===========================
// PUSH TESTS
// ===========================

async function testPushSingle(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-push').create()
  const res = await client
    .queue('ts-test-push')
    .push([{ data: { hello: 'world' } }]) as PushResult[]

  return {
    success: Array.isArray(res) && res[0]?.status === 'queued',
    message: `Push result: ${res[0]?.status}`
  }
}

async function testPushMultiple(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-push-multi').create()
  const res = await client
    .queue('ts-test-push-multi')
    .push([
      { data: { id: 1 } },
      { data: { id: 2 } },
      { data: { id: 3 } }
    ]) as PushResult[]

  const allQueued = Array.isArray(res) && res.every(r => r.status === 'queued')
  return { success: allQueued && res.length === 3, message: `Pushed ${res.length} messages` }
}

async function testPushWithPartition(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-push-partition').create()
  const res = await client
    .queue('ts-test-push-partition')
    .partition('my-partition')
    .push([{ data: { partitioned: true } }]) as PushResult[]

  return { success: res[0]?.status === 'queued', message: 'Pushed to partition' }
}

async function testPushDuplicate(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-push-dup').create()
  const txId = generateUUID()

  const res1 = await client.queue('ts-test-push-dup').push([{ transactionId: txId, data: { v: 1 } }]) as PushResult[]
  const res2 = await client.queue('ts-test-push-dup').push([{ transactionId: txId, data: { v: 1 } }]) as PushResult[]

  return {
    success: res1[0]?.status === 'queued' && res2[0]?.status === 'duplicate',
    message: `First: ${res1[0]?.status}, Second: ${res2[0]?.status}`
  }
}

async function testPushNullPayload(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-push-null').create()
  const res = await client.queue('ts-test-push-null').push([{ data: null }]) as PushResult[]
  if (res[0]?.status !== 'queued') return { success: false, message: 'Push failed' }

  const msgs = await client.queue('ts-test-push-null').batch(1).wait(false).pop()
  return {
    success: msgs.length === 1 && (msgs[0] as any).data === null,
    message: 'Null payload round-trips correctly'
  }
}

async function testPushOnSuccess(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-push-callback').create()
  let callbackItems: unknown[] = []

  await client
    .queue('ts-test-push-callback')
    .push([{ data: { cb: true } }])
    .onSuccess(async (items) => {
      callbackItems = items as unknown[]
    })

  return {
    success: callbackItems.length > 0,
    message: `onSuccess received ${callbackItems.length} items`
  }
}

// ===========================
// POP TESTS
// ===========================

async function testPopEmpty(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-pop-empty').create()
  const msgs = await client.queue('ts-test-pop-empty').batch(1).wait(false).pop()
  return { success: msgs.length === 0, message: 'Empty queue returns no messages' }
}

async function testPopSingle(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-pop-single').create()
  await client.queue('ts-test-pop-single').push([{ data: { msg: 'pop-me' } }])

  const msgs = await client.queue('ts-test-pop-single').batch(1).wait(false).pop()
  return {
    success: msgs.length === 1 && (msgs[0] as any).data?.msg === 'pop-me',
    message: `Popped ${msgs.length} message(s)`
  }
}

async function testPopBatch(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-pop-batch').create()
  await client.queue('ts-test-pop-batch').push([
    { data: { id: 1 } },
    { data: { id: 2 } },
    { data: { id: 3 } }
  ])

  const msgs = await client.queue('ts-test-pop-batch').batch(10).wait(false).pop()
  return { success: msgs.length === 3, message: `Popped ${msgs.length} messages` }
}

async function testPopWithPartition(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-pop-part').create()
  await client.queue('ts-test-pop-part').partition('p1').push([{ data: { p: 1 } }])
  await client.queue('ts-test-pop-part').partition('p2').push([{ data: { p: 2 } }])

  const p1 = await client.queue('ts-test-pop-part').partition('p1').batch(10).wait(false).pop()
  const p2 = await client.queue('ts-test-pop-part').partition('p2').batch(10).wait(false).pop()

  return {
    success: p1.length === 1 && p2.length === 1,
    message: `p1: ${p1.length}, p2: ${p2.length}`
  }
}

// ===========================
// ACK TESTS
// ===========================

async function testAckSingle(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-ack').create()
  await client.queue('ts-test-ack').push([{ data: { ack: true } }])

  const msgs = await client.queue('ts-test-ack').batch(1).wait(false).pop()
  if (msgs.length === 0) return { success: false, message: 'No message to ack' }

  const result = await client.ack(msgs[0], true)
  return { success: result.success === true, message: `Ack result: ${result.success}` }
}

async function testAckBatch(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-ack-batch').create()
  await client.queue('ts-test-ack-batch').push([
    { data: { id: 1 } },
    { data: { id: 2 } }
  ])

  const msgs = await client.queue('ts-test-ack-batch').batch(2).wait(false).pop()
  if (msgs.length !== 2) return { success: false, message: `Expected 2, got ${msgs.length}` }

  const result = await client.ack(msgs, true)
  return { success: result.success === true, message: `Batch ack: ${result.success}` }
}

async function testAckNack(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-nack').config({ retryLimit: 1 }).create()
  await client.queue('ts-test-nack').push([{ data: { nack: true } }])

  const msgs = await client.queue('ts-test-nack').batch(1).wait(false).pop()
  if (msgs.length === 0) return { success: false, message: 'No message to nack' }

  const result = await client.ack(msgs[0], false)
  return { success: result.success === true, message: `Nack result: ${result.success}` }
}

async function testAckWithGroup(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-ack-group').create()
  await client.queue('ts-test-ack-group').push([{ data: { group: true } }])

  const msgs = await client.queue('ts-test-ack-group').group('test-grp').subscriptionMode('from_beginning').batch(1).wait(false).pop()
  if (msgs.length === 0) return { success: false, message: 'No message' }

  const result = await client.ack(msgs[0], true, { group: 'test-grp' })
  return { success: result.success === true, message: 'Ack with group succeeded' }
}

// ===========================
// LEASE RENEWAL TESTS
// ===========================

async function testRenewLease(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-renew').config({ leaseTime: 60 }).create()
  await client.queue('ts-test-renew').push([{ data: { renew: true } }])

  const msgs = await client.queue('ts-test-renew').batch(1).wait(false).pop()
  if (msgs.length === 0) return { success: false, message: 'No message' }

  const result = await client.renew(msgs[0]) as RenewResult
  return {
    success: result.success === true,
    message: `Renew: success=${result.success}, newExpiresAt=${result.newExpiresAt}`
  }
}

// ===========================
// CONSUME TESTS
// ===========================

async function testConsumeEach(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-consume-each').create()
  await client.queue('ts-test-consume-each').push([
    { data: { id: 1 } },
    { data: { id: 2 } }
  ])

  const received: QueenMessage[] = []

  await client
    .queue('ts-test-consume-each')
    .batch(1)
    .limit(2)
    .each()
    .consume(async (msg) => {
      received.push(msg as QueenMessage)
    })

  return {
    success: received.length === 2,
    message: `Received ${received.length} messages individually`
  }
}

async function testConsumeBatch(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-consume-batch').create()
  await client.queue('ts-test-consume-batch').push([
    { data: { id: 1 } },
    { data: { id: 2 } },
    { data: { id: 3 } }
  ])

  let batchSize = 0

  await client
    .queue('ts-test-consume-batch')
    .batch(10)
    .wait(false)
    .limit(1)
    .consume(async (msgs) => {
      batchSize = (msgs as QueenMessage[]).length
    })

  return { success: batchSize === 3, message: `Batch size: ${batchSize}` }
}

async function testConsumeWithGroup(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-consume-group').create()

  // Push messages
  for (let i = 0; i < 5; i++) {
    await client.queue('ts-test-consume-group')
      .buffer({ messageCount: 5, timeMillis: 1000 })
      .push([{ data: { id: i } }])
  }

  let g1Count = 0
  let g2Count = 0

  await client
    .queue('ts-test-consume-group')
    .group('ts-grp-1')
    .subscriptionMode('from_beginning')
    .batch(10)
    .wait(false)
    .limit(1)
    .consume(async (msgs) => {
      g1Count = (msgs as QueenMessage[]).length
    })

  await client
    .queue('ts-test-consume-group')
    .group('ts-grp-2')
    .subscriptionMode('from_beginning')
    .batch(10)
    .wait(false)
    .limit(1)
    .consume(async (msgs) => {
      g2Count = (msgs as QueenMessage[]).length
    })

  return {
    success: g1Count === 5 && g2Count === 5,
    message: `group1: ${g1Count}, group2: ${g2Count}`
  }
}

async function testConsumeOnSuccessOnError(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-consume-cb').create()
  await client.queue('ts-test-consume-cb').push([{ data: { id: 1 } }])

  let successCalled = false

  await client
    .queue('ts-test-consume-cb')
    .batch(1)
    .limit(1)
    .each()
    .consume(async (msg) => {
      // process
    })
    .onSuccess(async (msgs, result) => {
      successCalled = true
      await client.ack(msgs as QueenMessage, true)
    })
    .onError(async (msgs, error) => {
      await client.ack(msgs as QueenMessage, false)
    })

  return { success: successCalled, message: successCalled ? 'onSuccess called' : 'onSuccess NOT called' }
}

async function testConsumeWithSignal(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-consume-signal').create()
  await client.queue('ts-test-consume-signal').push([{ data: { id: 1 } }])

  const controller = new AbortController()
  let processedCount = 0

  // Abort after 500ms
  setTimeout(() => controller.abort(), 500)

  await client
    .queue('ts-test-consume-signal')
    .batch(1)
    .wait(false)
    .each()
    .consume(async (msg) => {
      processedCount++
    }, { signal: controller.signal })

  return {
    success: processedCount >= 1,
    message: `Processed ${processedCount} before abort`
  }
}

async function testConsumeTrace(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-consume-trace').create()
  await client.queue('ts-test-consume-trace').push([{ data: { trace: true } }])

  let traceResult: { success: boolean; error?: string } | null = null

  await client
    .queue('ts-test-consume-trace')
    .batch(1)
    .limit(1)
    .each()
    .consume(async (msg) => {
      const m = msg as QueenMessage
      if (m.trace) {
        traceResult = await m.trace({
          traceName: ['ts-test-trace'],
          eventType: 'info',
          data: { test: true, timestamp: Date.now() }
        })
      }
    })

  return {
    success: traceResult !== null && traceResult!.success === true,
    message: `Trace result: ${JSON.stringify(traceResult)}`
  }
}

// ===========================
// BUFFER TESTS
// ===========================

async function testBufferedPush(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-buffer').create()

  // Push with buffer
  await client
    .queue('ts-test-buffer')
    .buffer({ messageCount: 10, timeMillis: 1000 })
    .push([{ data: { buffered: true } }])

  // Should not be available yet (buffered)
  const immediate = await client.queue('ts-test-buffer').batch(1).wait(false).pop()
  if (immediate.length !== 0) {
    return { success: false, message: 'Message should be buffered, not yet available' }
  }

  // Check buffer stats
  const stats = client.getBufferStats()
  if (stats.totalBufferedMessages !== 1) {
    return { success: false, message: `Expected 1 buffered, got ${stats.totalBufferedMessages}` }
  }

  // Wait for time-based flush
  await new Promise(r => setTimeout(r, 1500))

  const afterFlush = await client.queue('ts-test-buffer').batch(1).wait(false).pop()
  return {
    success: afterFlush.length === 1,
    message: `After time flush: ${afterFlush.length} messages`
  }
}

async function testFlushAllBuffers(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-flush-all').create()

  for (let i = 0; i < 5; i++) {
    await client
      .queue('ts-test-flush-all')
      .buffer({ messageCount: 100, timeMillis: 60000 })
      .push([{ data: { id: i } }])
  }

  const statsBefore = client.getBufferStats()
  if (statsBefore.totalBufferedMessages !== 5) {
    return { success: false, message: `Expected 5 buffered, got ${statsBefore.totalBufferedMessages}` }
  }

  await client.flushAllBuffers()

  const statsAfter = client.getBufferStats()
  const msgs = await client.queue('ts-test-flush-all').batch(10).wait(false).pop()

  return {
    success: msgs.length === 5 && statsAfter.totalBufferedMessages === 0,
    message: `Flushed: ${msgs.length} messages, buffers remaining: ${statsAfter.totalBufferedMessages}`
  }
}

async function testQueueFlushBuffer(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-queue-flush').create()

  for (let i = 0; i < 3; i++) {
    await client
      .queue('ts-test-queue-flush')
      .buffer({ messageCount: 100, timeMillis: 60000 })
      .push([{ data: { id: i } }])
  }

  await client.queue('ts-test-queue-flush').flushBuffer()

  const msgs = await client.queue('ts-test-queue-flush').batch(10).wait(false).pop()
  return {
    success: msgs.length === 3,
    message: `Queue-level flush: ${msgs.length} messages`
  }
}

// ===========================
// TRANSACTION TESTS
// ===========================

async function testTransactionPushAck(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-txn-a').create()
  await client.queue('ts-test-txn-b').create()

  await client.queue('ts-test-txn-a').push([{ data: { value: 42 } }])

  const msgs = await client.queue('ts-test-txn-a').batch(1).wait(false).pop()
  if (msgs.length === 0) return { success: false, message: 'No message to consume' }

  await client
    .transaction()
    .queue('ts-test-txn-b')
    .push([{ data: { value: (msgs[0] as any).data.value + 1 } }])
    .ack(msgs[0])
    .commit()

  const resultA = await client.queue('ts-test-txn-a').batch(1).wait(false).pop()
  const resultB = await client.queue('ts-test-txn-b').batch(1).wait(false).pop()

  return {
    success: resultA.length === 0 && resultB.length === 1 && (resultB[0] as any).data.value === 43,
    message: 'Transaction push+ack succeeded'
  }
}

async function testTransactionMultiQueue(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-txn-mq-a').create()
  await client.queue('ts-test-txn-mq-b').create()
  await client.queue('ts-test-txn-mq-c').create()

  await client.queue('ts-test-txn-mq-a').push([{ data: { source: true } }])
  const msgs = await client.queue('ts-test-txn-mq-a').batch(1).wait(false).pop()
  if (msgs.length === 0) return { success: false, message: 'No message' }

  await client
    .transaction()
    .queue('ts-test-txn-mq-b').push([{ data: { from: 'a' } }])
    .queue('ts-test-txn-mq-c').push([{ data: { from: 'a' } }])
    .ack(msgs[0])
    .commit()

  const rB = await client.queue('ts-test-txn-mq-b').batch(1).wait(false).pop()
  const rC = await client.queue('ts-test-txn-mq-c').batch(1).wait(false).pop()
  const rA = await client.queue('ts-test-txn-mq-a').batch(1).wait(false).pop()

  return {
    success: rA.length === 0 && rB.length === 1 && rC.length === 1,
    message: 'Multi-queue transaction succeeded'
  }
}

async function testTransactionEmpty(client: Queen): Promise<TestResult> {
  let errorThrown = false
  try {
    await client.transaction().commit()
  } catch (e) {
    errorThrown = (e as Error).message === 'Transaction has no operations to commit'
  }
  return { success: errorThrown, message: 'Empty transaction correctly throws' }
}

async function testTransactionWithPartitions(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-txn-part').create()

  await client.queue('ts-test-txn-part').partition('p1').push([{ data: { p: 1 } }])
  await client.queue('ts-test-txn-part').partition('p2').push([{ data: { p: 2 } }])

  const m1 = await client.queue('ts-test-txn-part').partition('p1').batch(1).wait(false).pop()
  const m2 = await client.queue('ts-test-txn-part').partition('p2').batch(1).wait(false).pop()

  if (m1.length === 0 || m2.length === 0) return { success: false, message: 'No messages in partitions' }

  await client.transaction().ack(m1[0]).ack(m2[0]).commit()

  const r1 = await client.queue('ts-test-txn-part').partition('p1').batch(1).wait(false).pop()
  const r2 = await client.queue('ts-test-txn-part').partition('p2').batch(1).wait(false).pop()

  return {
    success: r1.length === 0 && r2.length === 0,
    message: 'Transaction across partitions succeeded'
  }
}

// ===========================
// DLQ TESTS
// ===========================

async function testDLQ(client: Queen): Promise<TestResult> {
  const queueName = 'ts-test-dlq'

  await client.queue(queueName).config({ retryLimit: 1 }).create()
  await client.queue(queueName).push([{ data: { dlq: true } }])

  // Consume and fail to trigger DLQ
  await client
    .queue(queueName)
    .batch(1)
    .wait(false)
    .limit(2)
    .each()
    .consume(async (_msg) => {
      throw new Error('Force DLQ')
    })
    .onError(async (msg, _error) => {
      await client.ack(msg as QueenMessage, false, { error: 'Force DLQ' })
    })

  await new Promise(r => setTimeout(r, 500))

  const dlqResult = await client.queue(queueName).dlq().limit(10).get()

  return {
    success: dlqResult.messages.length > 0,
    message: `DLQ contains ${dlqResult.total} message(s)`
  }
}

async function testDLQWithFilters(client: Queen): Promise<TestResult> {
  const queueName = 'ts-test-dlq' // reuse from previous test

  const result = await client
    .queue(queueName)
    .dlq()
    .limit(5)
    .offset(0)
    .get()

  return {
    success: result !== null && Array.isArray(result.messages),
    message: `DLQ query returned ${result.messages.length} messages`
  }
}

// ===========================
// ADMIN TESTS
// ===========================

async function testAdminHealth(client: Queen): Promise<TestResult> {
  const result = await client.admin.health() as { status?: string } | null
  return {
    success: result !== null,
    message: `Health: ${JSON.stringify(result)?.substring(0, 100)}`
  }
}

async function testAdminOverview(client: Queen): Promise<TestResult> {
  const result = await client.admin.getOverview() as object | null
  return {
    success: result !== null,
    message: 'Overview retrieved'
  }
}

async function testAdminListQueues(client: Queen): Promise<TestResult> {
  const result = await client.admin.listQueues() as { queues?: unknown[] } | null
  return {
    success: result !== null,
    message: `Listed queues`
  }
}

async function testAdminGetStatus(client: Queen): Promise<TestResult> {
  const result = await client.admin.getStatus() as object | null
  return {
    success: result !== null,
    message: 'Status retrieved'
  }
}

async function testAdminConsumerGroups(client: Queen): Promise<TestResult> {
  const result = await client.admin.listConsumerGroups() as unknown[] | null
  return {
    success: result !== null,
    message: 'Consumer groups listed'
  }
}

async function testAdminSingleton(client: Queen): Promise<TestResult> {
  const admin1 = client.admin
  const admin2 = client.admin
  return {
    success: admin1 === admin2,
    message: 'Admin is singleton'
  }
}

// ===========================
// CONSUMER GROUP MANAGEMENT TESTS
// ===========================

async function testDeleteConsumerGroup(client: Queen): Promise<TestResult> {
  // Create a consumer group by consuming with it
  await client.queue('ts-test-del-cg').create()
  await client.queue('ts-test-del-cg').push([{ data: { id: 1 } }])

  await client
    .queue('ts-test-del-cg')
    .group('ts-cg-to-delete')
    .subscriptionMode('from_beginning')
    .batch(1).wait(false).limit(1)
    .consume(async () => {})

  try {
    const result = await client.deleteConsumerGroup('ts-cg-to-delete')
    return { success: true, message: 'Consumer group deleted' }
  } catch (e) {
    return { success: false, message: (e as Error).message }
  }
}

// ===========================
// DELAYED PROCESSING TESTS
// ===========================

async function testDelayedProcessing(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-delayed').config({ delayedProcessing: 2 }).create()
  await client.queue('ts-test-delayed').push([{ data: { delayed: true } }])

  // Should not be available immediately
  const immediate = await client.queue('ts-test-delayed').batch(1).wait(false).pop()
  if (immediate.length !== 0) return { success: false, message: 'Message should be delayed' }

  // Wait for delay
  await new Promise(r => setTimeout(r, 2500))

  const afterDelay = await client.queue('ts-test-delayed').batch(1).wait(true).pop()
  return {
    success: afterDelay.length === 1,
    message: afterDelay.length === 1 ? 'Delayed message received' : 'Delayed message NOT received'
  }
}

// ===========================
// BUFFER STATS TESTS
// ===========================

async function testBufferStats(client: Queen): Promise<TestResult> {
  const stats: BufferStats = client.getBufferStats()

  const hasAllFields =
    typeof stats.activeBuffers === 'number' &&
    typeof stats.totalBufferedMessages === 'number' &&
    typeof stats.oldestBufferAge === 'number' &&
    typeof stats.flushesPerformed === 'number'

  return {
    success: hasAllFields,
    message: `Stats: ${JSON.stringify(stats)}`
  }
}

// ===========================
// ORDERING TESTS
// ===========================

async function testConsumeOrdering(client: Queen): Promise<TestResult> {
  await client.queue('ts-test-ordering').create()

  const count = 20
  for (let i = 0; i < count; i++) {
    await client.queue('ts-test-ordering').push([{ data: { id: i } }])
  }

  let lastId: number | null = null
  let orderingCorrect = true

  await client
    .queue('ts-test-ordering')
    .batch(1)
    .wait(false)
    .limit(count)
    .each()
    .consume(async (msg) => {
      const id = (msg as any).data.id as number
      if (lastId !== null && id !== lastId + 1) {
        orderingCorrect = false
      }
      lastId = id
    })

  return {
    success: orderingCorrect && lastId === count - 1,
    message: `Ordering correct: ${orderingCorrect}, lastId: ${lastId}`
  }
}

// ===========================
// MAIN
// ===========================

async function main() {
  console.log(`\n🐝 Queen MQ TypeScript Client Tests`)
  console.log(`Server: ${SERVER_URL}`)
  console.log('='.repeat(80) + '\n')

  const client = new Queen(SERVER_URL)

  // Type system
  await runTest('testTypeExports', testTypeExports, client)

  // Client initialization
  await runTest('testClientStringUrl', testClientStringUrl, client)
  await runTest('testClientArrayUrls', testClientArrayUrls, client)
  await runTest('testClientObjectConfig', testClientObjectConfig, client)
  await runTest('testClientInvalidUrl', testClientInvalidUrl, client)
  await runTest('testClientNoUrl', testClientNoUrl, client)

  // Queue create/delete
  await runTest('testQueueCreate', testQueueCreate, client)
  await runTest('testQueueCreateWithConfig', testQueueCreateWithConfig, client)
  await runTest('testQueueDelete', testQueueDelete, client)
  await runTest('testQueueCreateOnSuccess', testQueueCreateOnSuccess, client)

  // Push
  await runTest('testPushSingle', testPushSingle, client)
  await runTest('testPushMultiple', testPushMultiple, client)
  await runTest('testPushWithPartition', testPushWithPartition, client)
  await runTest('testPushDuplicate', testPushDuplicate, client)
  await runTest('testPushNullPayload', testPushNullPayload, client)
  await runTest('testPushOnSuccess', testPushOnSuccess, client)

  // Pop
  await runTest('testPopEmpty', testPopEmpty, client)
  await runTest('testPopSingle', testPopSingle, client)
  await runTest('testPopBatch', testPopBatch, client)
  await runTest('testPopWithPartition', testPopWithPartition, client)

  // Ack
  await runTest('testAckSingle', testAckSingle, client)
  await runTest('testAckBatch', testAckBatch, client)
  await runTest('testAckNack', testAckNack, client)
  await runTest('testAckWithGroup', testAckWithGroup, client)

  // Lease renewal
  await runTest('testRenewLease', testRenewLease, client)

  // Consume
  await runTest('testConsumeEach', testConsumeEach, client)
  await runTest('testConsumeBatch', testConsumeBatch, client)
  await runTest('testConsumeWithGroup', testConsumeWithGroup, client)
  await runTest('testConsumeOnSuccessOnError', testConsumeOnSuccessOnError, client)
  await runTest('testConsumeWithSignal', testConsumeWithSignal, client)
  await runTest('testConsumeTrace', testConsumeTrace, client)

  // Buffer
  await runTest('testBufferedPush', testBufferedPush, client)
  await runTest('testFlushAllBuffers', testFlushAllBuffers, client)
  await runTest('testQueueFlushBuffer', testQueueFlushBuffer, client)
  await runTest('testBufferStats', testBufferStats, client)

  // Transactions
  await runTest('testTransactionPushAck', testTransactionPushAck, client)
  await runTest('testTransactionMultiQueue', testTransactionMultiQueue, client)
  await runTest('testTransactionEmpty', testTransactionEmpty, client)
  await runTest('testTransactionWithPartitions', testTransactionWithPartitions, client)

  // DLQ
  await runTest('testDLQ', testDLQ, client)
  await runTest('testDLQWithFilters', testDLQWithFilters, client)

  // Delayed processing
  await runTest('testDelayedProcessing', testDelayedProcessing, client)

  // Ordering
  await runTest('testConsumeOrdering', testConsumeOrdering, client)

  // Admin
  await runTest('testAdminHealth', testAdminHealth, client)
  await runTest('testAdminOverview', testAdminOverview, client)
  await runTest('testAdminListQueues', testAdminListQueues, client)
  await runTest('testAdminGetStatus', testAdminGetStatus, client)
  await runTest('testAdminConsumerGroups', testAdminConsumerGroups, client)
  await runTest('testAdminSingleton', testAdminSingleton, client)

  // Consumer group management
  await runTest('testDeleteConsumerGroup', testDeleteConsumerGroup, client)

  printResults()

  await client.close()

  const failed = testResults.filter(r => !r.success).length
  process.exit(failed > 0 ? 1 : 0)
}

main().catch(err => {
  console.error('Fatal error:', err)
  process.exit(1)
})
