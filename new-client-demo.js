import { queen } from 'queen-mq'

queen.queue('queue', { leaseTime: 30 })
.tx('queue/partition0', [ { id: 1 }, { id: 2 }, { id: 3 } ])
.rx('queue', { limit: 1, batch: 10, wait: true, timeout: 30000, autoRenewalInterval: 5000, workers: 2 })
.process(async (msgs) => {
  console.log(msgs.map(x => x.data.id))
  return msgs.map(x => x.data.id * 2)
})
.transaction(async (trx, msgs) => {
  trx.ack(msgs)
  trx.tx('queue-another/partition0', msgs.map(x => x.data.id * 2))
})
.execute()