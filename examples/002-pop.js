import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'test-pop'

await queen.queue(queueName).create()

await queen.queue(queueName).push([{ data: { message: 'Hello, world!' } }])

const startTime = Date.now()
await queen.queue(queueName)
.partition('Default')
.batch(1)
.limit(1)
.wait(false)
.each()
.consume(async (message) => {
    const endTime = Date.now()
    const duration = endTime - startTime
    console.log(`Pop took ${duration}ms`)
    console.log(message)
})
