#!/usr/bin/env node

/**
 * Affinity Routing Example
 * Demonstrates how the client uses consistent hashing to route consumer groups
 * to the same backend server for optimal poll intention consolidation
 */

import { Queen } from '../client-js/client-v2/index.js'

// Configure Queen with affinity-based load balancing
const queen = new Queen({
  urls: [
    'http://localhost:6632',
    'http://localhost:6633',
  ],
  loadBalancingStrategy: 'affinity',  // Use affinity routing (with virtual nodes)
  affinityHashRing: 150,               // 150 virtual nodes per server (default)
  enableFailover: true                 // Failover to other servers if one is down
})

await queen.queue('test-affinity-1').delete();
await queen.queue('test-affinity-1').create();

await queen.queue('test-affinity-20').delete();
await queen.queue('test-affinity-20').create();

for (let i = 0; i < 30; i++) {
    await queen.queue('test-affinity-1').push([{
        data: {
            message: `Hello, world! ${i}`
        }
    }]);
}

for (let i = 0; i < 30; i++) {
    await queen.queue('test-affinity-20').push([{
        data: {
            message: `Hello, world! ${i}`
        }
    }]);
}

await queen.queue('test-affinity-1')
.group('test-affinity-group')
.concurrency(1)
.batch(1)
.limit(30)
.autoAck(true)
.each()
.consume(async (message) => {
    console.log(message)
})

await queen.queue('test-affinity-20')
.group('test-affinity-group')
.concurrency(1)
.batch(1)
.limit(30)
.autoAck(true)
.each()
.consume(async (message) => {
    console.log(message)
})

await queen.close()

