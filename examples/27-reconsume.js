import fs from 'fs'
import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'smartchat.router.outgoing'
const timeAgo = new Date(Date.now() - (3600000 * 1000)).toISOString()
console.log(`Starting consumer from ${timeAgo}`)

const consumer = await queen
  .queue(queueName)
  .group('reprocess-alice-01')
  .concurrency(20)
  .autoAck(true)
  .subscriptionFrom(timeAgo)
  .batch(10)
  .each()
  .consume(async (message) => {
    try {
        console.log(message.createdAt)
        const data = message.data
        console.log((new Date()).toISOString(), data.sentAt, message.transactionId)
    } catch (error) {
        console.error(error)
    }
})
