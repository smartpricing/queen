import { Queen } from '../../client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'smartchat-agent'
await queen
.queue(queueName)
.config({
  leaseTime: 2000,
})
.create()

for (let i = 0; i < 10; i++) {
  await queen
  .queue(queueName)
  .push([{ data: { chatId: i, aiScore: i % 5, timestamp: new Date().toISOString() } }])
}

const result = await queen
.stream(`${queueName}@test-filter`, {
    from: '2025-10-31T14:51:30.460Z'
})
.filter({ 'payload.aiScore': { $eq: 4 } })
.execute()


console.log(result.messages.map(x => x.payload))