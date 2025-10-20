# Queen MQ - PostgreSQL-backed C++ Message Queue System

<div align="center">

**A modern, high-performance message queue system built on PostgreSQL**

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE.md)
[![Node](https://img.shields.io/badge/node-%3E%3D22.0.0-brightgreen.svg)](https://nodejs.org/)

[Quick Start](#-quick-start) • [Client Examples](#-client-examples) • [Server Setup](#-server-setup) • [Core Concepts](#-core-concepts) • [API Reference](#-http-api-reference) • [Dashboard](#-dashboard)

<p align="center">
  <img src="assets/queen-logo-rose.svg" alt="Queen Logo" width="120" />
</p>

</div>

---

## Introduction

QueenMQ is a queue system written in C++ and backed by Postgres. Supports queues and consumer groups.

## JS Client usage

```js
import { Queen } from 'queen-mq'

const client = new Queen({ 
    baseUrls: ['http://localhost:6632'],
    timeout: 30000,
    retryAttempts: 3
});

const queue = 'html-processing'

// Create a queue
await client.queue(queue, { leaseTime: 30 });

// Push some data, specifyng the partition
await client.push(`${queue}/customer-1828`, [ { id: 1 } ]); 

// Consume data with iterators
for await (const msg of client.take(queue, { limit: 1 })) {
    console.log(msg.data.id)
    await client.ack(msg) //  OR await client.ack(msg, false) for nack
}

// Consume data with iterators, getting the entire batch
for await (const messages of client.takeBatch(queue, { limit: 1, batch: 10 })) {
    const newMex = messages.map(x => x.data.id * 2)
    await client.ack(messages) //  OR await client.ack(messages, false) for nack
}

// Consume data from any partition of the queue, continusly
// This "pipeline" is useful for doing exactly one processing
await client 
.pipeline(queue)
.take(10)
.processBatch(async (messages) => {
    return messages.map(x => x.data.id * 2);
})
.atomically((tx, originalMessages, processedMessages) => { // ack and push are transactional
    tx.ack(originalMessages);
    tx.push('another-queue', processedMessages); 
})
.repeat({ continuous: true })
.execute();
```

## Use cases and examples

#### Advanced queue configuration

MaxSize, window buffer, delay

#### Using namespace and task filters 

#### Client usage

#### Transactional pipelines

## Webapp


## Install server and configure it

### Compile

```sh
cd server
make clean
make deps
make build-only
./bin/queen-server
```

The full list of enviroment variables is here:

[ENVVAR](server/ENV_VARIABLES.md)


### With Docker
```sh
./build.sh
```

## Raw HTTP API

You can use Queen directly from HTTP.


