import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

await queen.queue('test-concurrency-queue').delete()
await queen.queue('test-concurrency-queue').create()

await queen.queue('test-concurrency-queue').push([
    { data: { message: 'Hello, world 1!' } },
    { data: { message: 'Hello, world 2!' } },
    { data: { message: 'Hello, world 3!' } },
    { data: { message: 'Hello, world 4!' } },
    { data: { message: 'Hello, world 5!' } },
    { data: { message: 'Hello, world 6!' } },
    { data: { message: 'Hello, world 7!' } },
    { data: { message: 'Hello, world 8!' } },
    { data: { message: 'Hello, world 9!' } },
    { data: { message: 'Hello, world 10!' } },
])

await queen
.queue('test-concurrency-queue')
.group('test-concurrency-group')
.subscriptionMode('all')
.concurrency(10)
.batch(3)
.limit(10)
.autoAck(false)
.consume(async (message) => {
    console.log(message)
})
.onSuccess(async (messages) => {
    await queen.ack(messages, true, { group: 'test-concurrency-group' })
})
.onError(async (messages, error) => {
    console.log(messages, error)
})