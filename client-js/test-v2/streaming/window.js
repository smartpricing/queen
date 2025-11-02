import { Queen } from '../../client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'smartchat-agent'

await queen
.queue(queueName)
.config({
  leaseTime: 2000,
})
.create()

async function producer () {
    let i = 0
    while (true) {
        await queen
        .queue(queueName)
        .push([{ data: { 
            tenantId: i % 10,
            chatId: i, 
            aiScore: i % 5, 
            timestamp: new Date().toISOString() 
        } }])
        i++
        await new Promise(resolve => setTimeout(resolve, 1000))
    }
}
producer()

const windowedStream = queen
.stream(`${queueName}@window_test`)
.window({ type: 'tumbling', duration: 5, grace: 1 })
.collect()
for await (const window of windowedStream) {
    console.log(window)
}