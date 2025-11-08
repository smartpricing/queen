# Transaction Examples

Atomic operations across queues.

## Basic Transaction

```javascript
await queen.transaction()
  .ack(inputMessage)
  .queue('output')
  .push([{ data: result }])
  .commit()
```

## Multi-stage Pipeline

```javascript
await queen.queue('stage1').consume(async (msg) => {
  const result = await processStage1(msg.data)
  
  await queen.transaction()
    .ack(msg)
    .queue('stage2')
    .push([{ data: result }])
    .commit()
})
```

[More examples](https://github.com/smartpricing/queen/blob/master/examples/03-transactional-pipeline.js)
