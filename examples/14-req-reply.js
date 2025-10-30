import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'req-reply-1'
const reqId = '123'

await queen
.queue(queueName)
.config({
    leaseTime: 10,
})
.create()

await queen
.queue(queueName)
.partition(`request`)
.push([
  { data: { id: 1, message: 'Hello', responseId: reqId } }
])

await queen
.queue(queueName)
.partition(`request`)
.each()
.limit(1)
.consume(async (message) => {
  console.log('Received:', message.data, `response-${message.data.responseId}`)
  await queen
  .queue(queueName)
  .partition(`response-${message.data.responseId}`)
  .push([
    { data: { id: 2, message: 'World', responseId: reqId } }
  ])
})
console.log('Consumed message')

const response = await queen
.queue(queueName)
.partition(`response-${reqId}`)
.wait(true)
.pop()
console.log('Popped response')

console.log('Response:', response)

console.log(await queen.ack(response[0]))