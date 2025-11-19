import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'beanchmark-buffer-example'

await queen.queue(queueName).delete()
await queen.queue(queueName).create()

// Push a single message

let startTime = Date.now()
let totalPushed = 0

async function pushMessage(partition) {
    let pushed = 0
    while (pushed < 1000) {
        await queen
        .queue(queueName)
        .partition(partition.toString())
        .buffer({ messageCount: 100, timeMillis: 10 })
        .push([
            { data: { id: 1, message: 'Hello from Queen!' } }
        ])
        if (pushed % 100 === 0) {
            console.log(`Pushed ${pushed} messages... on partition ${partition}`)
        }
        pushed++
        totalPushed++
    }
    return pushed
}
let promises = []
for (let i = 0; i < 100; i++) {
    promises.push(pushMessage(i))
}
await Promise.all(promises)
await queen.flushAllBuffers()
const endTime = Date.now()
const duration = endTime - startTime
console.log(`Pushed 1000 messages in ${duration / 1000}s, ${totalPushed / (duration / 1000)} msg/s`)