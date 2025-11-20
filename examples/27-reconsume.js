import fs from 'fs'
import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'queue.to.reconsume'
const timeAgo = new Date(Date.now() - (3600000 * 12)).toISOString()
console.log(`Starting consumer from ${timeAgo}`)

const consumer = await queen
  .queue(queueName)
  .group('reprocess-alice')
  .autoAck(true)
  .subscriptionFrom(timeAgo)
  .batch(10)
  .each()
  .consume(async (message) => {
    try {
        const data = message.data
        console.log((new Date()).toISOString(), data.sentAt, message.transactionId)
        fs.appendFileSync('reconsume.json', JSON.stringify(data, null, 2) + '\n')
    } catch (error) {
        console.error(error)
    }
})
