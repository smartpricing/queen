import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

await queen.queue('test-concurrency-multi-queue').delete()
await queen.queue('test-concurrency-multi-queue').create()

for (let i = 0; i < 10; i++) {
    await queen.queue('test-concurrency-multi-queue')
    .partition(`p${i}`)
    .push([
        { data: { message: '1', partition: `p${i}` } },
        { data: { message: '2', partition: `p${i}` } },
        { data: { message: '3', partition: `p${i}` } },
        { data: { message: '4', partition: `p${i}` } },
        { data: { message: '5', partition: `p${i}` } },
        { data: { message: '6', partition: `p${i}` } },
        { data: { message: '7', partition: `p${i}` } },
        { data: { message: '8', partition: `p${i}` } },
        { data: { message: '9', partition: `p${i}` } },
        { data: { message: '10', partition: `p${i}` } },
    ])
}

await queen
.queue('test-concurrency-multi-queue')
.group('test-concurrency-multi-group')
.subscriptionMode('all')
.concurrency(10)
.batch(3)
.limit(10)
.autoAck(true)
.consume(async (message) => {
    console.log(message)
})