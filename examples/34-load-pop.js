import { Queen } from 'queen-mq';

const queen = new Queen('http://localhost:6632')
const queueName = 'load'

let msgCounter = 0;
let startTime = Date.now();
// Worker 1 in analytics group
const analytics1 = await queen
  .queue(queueName)
  .group('load')
  .concurrency(10)
  .batch(100)
  .idleMillis(3000)  // Stop after 3s of no messages (workers share the load)
  .consume(async (messages) => {
    msgCounter += messages.length;  
    console.log(`Load Worker 1: Processing ${messages.length} messages, msg/s: ${messages.length / (Date.now() - startTime) * 1000}`)
    startTime = Date.now();
  })
