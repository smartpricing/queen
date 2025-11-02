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

/*const result = await queen
.stream(`${queueName}@test-filter`, {
    from: '2025-10-31T14:51:30.460Z'
})
.filter({ 'payload.aiScore': { $eq: 4 } })
.groupBy('payload.aiScore')
.count()
.execute()
console.log(result)*/

for await (const group of queen
    .stream(`${queueName}@test-filter`, {
      from: '2025-10-31T14:51:30.460Z'  // Start from now, only new messages
    })
    .filter({ 'payload.aiScore': { $eq: 4 } })
    .groupBy('payload.aiScore')
    .count()
  ) {
    console.log('New group result:', group)
    // { aiscore: "4", count: 2 }
    
    // Process the aggregated result
    if (group.count > 10) {
      console.log(`Alert: ${group.count} messages with score ${group.aiscore}`)
    }
}


