# Streaming Examples

Real-time message streaming.

## Basic Stream

```javascript
await queen.queue('events')
  .stream()
  .subscribe(async (message) => {
    console.log('Real-time:', message.data)
  })
```

[More examples](https://github.com/smartpricing/queen/tree/master/examples)
