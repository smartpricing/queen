import { Queen } from '../client-js/client-v2/index.js'
import fs from 'fs'
const queenUrl = process.env.QUEEN_URL || 'http://localhost:6638'

const client = new Queen({
    urls: [queenUrl]
})

async function run (numberOfParallelWorkers = 1) {
    const cutoffTimestamp = (new Date('2025-12-19 04:00:00')).toISOString()
    await client
    .queue('smartchat.agent.reservations')
    .group('alice-stef-01-01')
    .subscriptionFrom(cutoffTimestamp)
    .subscriptionMode('timestamp')
    .renewLease(true, 5000)
    .concurrency(numberOfParallelWorkers)
    .batch(100)
    .consume(async (messages) => {
      console.log(new Date().toISOString(), 'Processing messages', messages.length)
      try {
        for (const message of messages) {
            fs.appendFileSync('messages.json', JSON.stringify(message.data))
        }
        const msgs = messages.map(message => message.data) 
        const unknown = msgs.filter(msg => msg.spec.properties.length === 0 || msg.spec.properties[0].id === 'unknown')
        if (unknown.length > 0) {
            console.log(unknown)
            fs.appendFileSync('unknown.json', JSON.stringify(unknown))
        }
      } catch (error) {
        console.error(error)
      }
    })
}

run(1)