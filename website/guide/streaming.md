# Streaming

Real-time message streaming with WebSocket support for building aggregation pipelines and processing messages as they arrive.

## Coming Soon

This page is under construction. For now, see:

- [Streaming Examples](/clients/examples/streaming)
- [Source Documentation](https://github.com/smartpricing/queen/tree/master/docs)

## Quick Example

```javascript
// Stream messages in real-time
await queen
  .queue('events')
  .stream()
  .subscribe(async (message) => {
    console.log('Real-time:', message.data)
  })
```

[Full documentation coming soon]
