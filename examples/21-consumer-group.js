import { Queen } from '../client-js/client-v2/index.js' 

const queen = new Queen('http://localhost:6632')

const queueName = 'test-queue-v3-consume-group'

await queen.queue(queueName).create()

// Push a message after 1 second
setTimeout(async () => {
    await queen.queue(queueName).push([{ data: { id: 1 } }])
}, 1000)

// Consume with consumer group
// Note: Change group name between runs to test subscription modes
await queen.queue(queueName)
  .group('test-group-42')
  // .subscriptionMode('new')  // Uncomment to skip historical messages
  .each()
  .consume(async (message) => {
    console.log('Message received:', message.data)
  })

await queen.close()

// Server default can be configured with:
// export DEFAULT_SUBSCRIPTION_MODE="new"
// ./bin/queen-server