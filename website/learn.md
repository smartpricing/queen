# What I learned developing Queen 

*This document describe both the technical aspects of the development of Queen and the lessons learned, and some benchmarks I've made to test the scalability and performance of the system.*

<img src="/learn/queen_logo.png" alt="Queen" style="max-height: 200px;">

## Preface

Queen was born from a simple idea to make queue management easier for Smartchat, which didn't play well with Kafka. When I started developing it, I didn't really know where I was going or what I was doing.  
The first working version with all the core features was developed in a few days in early October, in Node.js. Once I had the basic version running, I realized something good could come out of it and decided to rewrite the server in C++, a language I hold dear and consider the best for this kind of "system" application.


I knew very well I was getting myself into a mess: it's hard to build a program that does mainly async I/O in C++ as fast as Node.js — with Node, speed comes for free. 
Those who've interviewed with me know I always ask "why is Node fast at I/O", and even though I knew the answer in theory (libuv), developing Queen allowed me to actually apply it. After 8 years developing mainly in Node, my C++ skills had dropped to a very low level, but with the help of AI and making a thousand mistakes, I somehow managed to get to something good.

This document is an attempt to share what I learned developing Queen, so that if anyone else approaches something like this they can do it faster and with fewer mistakes, and to try to explain where Node's I/O speed comes from.

Structure:
- [The problem](#the-problem)
- [Sockets and file descriptors](#sockets-and-file-descriptors)
- [libuv](#libuv)
- [Queen's Architecture](#queens-architecture)
- [Microbatching](#microbatching)
- [Benchmarks](#benchmarks)
  - [Sustained load test (billions of messages in 3 days at 10k req/s)](#1-sustained-load-test)
  - [Additional benchmarks (push batch and consumer group consumption)](#2-additional-benchmarks)
  - [Benchmark Results](#3-benchmark-results)
- [Conclusions](#conclusions)

## The problem

Both the Node and C++ versions of Queen use uWebSockets as the HTTP server, an ultra-fast C++ library for handling HTTP and WebSocket connections: uWebSockets is the HTTP engine behind Bun. 
When I chose to use it, in my ignorance I thought everything would be easy and fast, and indeed with Node.js it was.
When I started building the C++ version, I realized pretty quickly there was a problem: uWebSockets works by spinning up an event loop that handles all HTTP and WebSocket connections, using mechanisms we'll see later, and this is very fast, but with one fundamental rule: **the event loop must be free to run, it must not be blocked by I/O operations.**

This is hard, because every call to Postgres blocks the event loop, and if the event loop is blocked, it can't handle HTTP and WebSocket connections, turning a potentially fast system into an embarrassingly slow one. 

To understand why, let's look at what happens when you use libpq (the PostgreSQL C library) the "normal" way:

```c
void handle_push_request(Request* req) {
    // 1. You're in the event loop, handling an HTTP request
    
    // 2. You call Postgres synchronously
    PGresult* result = PQexec(conn, "INSERT INTO messages ...");
    //                 ^^^^^^^^
    //                 This call BLOCKS. The CPU just sits there
    //                 waiting for the network round-trip to Postgres.
    //                 Meanwhile, the event loop is frozen.
    //                 No other HTTP requests can be processed.
    //                 If this takes 5ms, you've just wasted 5ms
    //                 where you could have handled hundreds of requests.
    
    // 3. Only now can you respond
    send_response(req, "OK");
}
```

*No "await" magic available in C*

The problem is that `PQexec()` is synchronous: it sends the query to Postgres, then **waits** for the response before returning. During this wait, your thread is doing nothing useful — it's just blocked on network I/O. And since the event loop runs on a single thread, nothing else can happen.

With a fast local Postgres, maybe that's 1-2ms per query. If you have 1000 requests per second and each one blocks for 2ms, you can only handle 500 req/s on a single thread.

The first solution I adopted was the classic one: use a thread pool to handle Postgres operations, so the event loop can run freely. I needed Queen ready as soon as possible for Smartchat's queues, so even though I knew it wasn't the best solution, I went with it. 

This is the "old school" approach that servers like Apache use: new connection, new thread. One thread per Postgres connection. I could have stopped there, for Smartchat's needs it was more than fine. This approach has the problem of not being very scalable and efficient, because it creates a lot of context switching.

After a fairly long series of problems, experiments, errors, and various moments of despair, I eventually developed the fully asynchronous version of Queen, which, lo and behold, uses libuv itself to handle Postgres operations, using PG async API.  

## Sockets and file descriptors

Before showing the final solution, we need to review some basic I/O programming concepts. In Unix/Linux, every network connection is represented by a file descriptor. A file descriptor is just an integer that represents a network connection.

```c
// IPv4 socket stream (aka TCP)
int fd = socket(AF_INET, SOCK_STREAM, 0);  // returns 5
```

What can we do with a file descriptor? Read and write to it. And how do we do that? With system calls.

```c
ssize_t write(int fd, const void *buf, size_t count);
ssize_t read(int fd, void *buf, size_t count);
```

If there's no data to read, read blocks. If there's no space to write, write blocks. You can see that if you need to read/write from multiple sockets, this approach doesn't work well.
So we can use the *select* call to monitor multiple file descriptors at once.

```c
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
```

*select* takes an array of file descriptors and a timeout as input. It returns the number of file descriptors that are ready to read or write.

```c
while (1) {
    // 1. Ask which fds are ready (doesn't block, or blocks with timeout)
    select(max_fd + 1, &read_fds, NULL, NULL, NULL);
    
    // 2. Read ONLY from those that are ready (will never block)
    for (int fd = 0; fd < max_fd; fd++) {
        if (FD_ISSET(fd, &read_fds)) {
            read(fd, buffer, 1024);  // There's definitely data to read
        }
    }
}
```

This approach is very good, but it has a problem: it scales with O(n) file descriptors. If we have 1000 file descriptors to monitor, we have to call select every time passing all 1000 file descriptors and the kernel has to loop through them, and then we have to loop through all the file descriptors again and read/write only those that are ready. 

To solve this problem, more sophisticated mechanisms are used like *epoll* (for Linux) and *kqueue* (for BSD/macOS). These mechanisms are much more efficient because they don't have the O(n) scaling problem.

```c
// Register once
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);

// Then just ask "who's ready?"
int n = epoll_wait(epoll_fd, events, MAX, timeout);
// → returns ONLY the 2 ready fds, not all 1000
```

So by using mechanisms like epoll and kqueue, we can send our queries to Postgres asynchronously, and then check only the file descriptors that are ready to read or write. 

## libuv 

What does this blessed library used by Node.js, uvloop in Python, and a thousand other libraries/software (also the first versions of Rust used libuv under the hood, before Tokio and other async crates) actually do? 
Mainly two things: 
1. Handle I/O operations asynchronously with the same interface on the three main operating systems
2. Expose a set of async primitives to handle I/O operations asynchronously

In practice libuv uses epoll, kqueue under the hood.

The main async primitives are:
- uv_poll: monitor file descriptors for reading or writing
- uv_timer: timers to execute operations periodically
- uv_async: signal to execute operations asynchronously
- uv_fs: file system operations 
- uv_work: thread operations

It also provides a threadpool to execute those operations that can't be done asynchronously by the operating system, like DNS resolution, compression, filesystem reads, etc.

### How Node.js maps to libuv

If you've ever wondered what happens when you call `setTimeout()` in Node.js, here's the answer:

| Node.js API | libuv call | Notes |
|-------------|------------|-------|
| `setTimeout(fn, ms)` | `uv_timer_start()` | Single-shot timer |
| `setInterval(fn, ms)` | `uv_timer_start()` | With repeat flag |
| `setImmediate(fn)` | `uv_check_t` | Runs after I/O poll phase |
| `fs.readFile()` | `uv_fs_read()` | Uses threadpool |
| `net.createServer()` | `uv_tcp_init()` + `uv_listen()` | Event-based |
| `dns.lookup()` | `uv_getaddrinfo()` | Uses threadpool |

So when you write:

```javascript
setTimeout(() => console.log('Hello!'), 1000);
```

Node.js is essentially doing:

```c
uv_timer_t timer;
uv_timer_init(loop, &timer);
uv_timer_start(&timer, on_timeout_callback, 1000, 0);  
//                                          ^^^^  ^^^
//                                          delay  repeat (0 = no repeat)
```

Here's an example of how to use libuv to handle I/O operations asynchronously:

```c
#include <uv.h>
#include <libpq-fe.h>  // PostgreSQL C library

uv_poll_t pg_poll;
PGconn* conn;

// Callback called when the Postgres socket is ready
void on_postgres_ready(uv_poll_t* handle, int status, int events) {
    // Socket is ready → read the result (does NOT block!)
    PQconsumeInput(conn);
    
    if (PQisBusy(conn) == 0) {
        // Query complete!
        PGresult* result = PQgetResult(conn);
        printf("Got %d rows!\n", PQntuples(result));
        PQclear(result);
    }
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    // 1. Connect to Postgres in NON-BLOCKING mode
    conn = PQconnectdb("host=localhost dbname=queen");
    PQsetnonblocking(conn, 1);
    
    // 2. Get the file descriptor of the Postgres socket
    int pg_socket = PQsocket(conn);  // ← it's just a number!
    
    // 3. Register the socket with libuv (like epoll_ctl)
    uv_poll_init(&loop, &pg_poll, pg_socket);
    uv_poll_start(&pg_poll, UV_READABLE, on_postgres_ready);
    
    // 4. Send a query in async mode (does NOT wait for response)
    PQsendQuery(conn, "SELECT * FROM messages");
    
    // 5. Start the loop - it will wake us when Postgres responds
    uv_run(&loop, UV_RUN_DEFAULT);
    
    return 0;
}
```

This is how Queen talsk to Postgres. We register it with libuv, and when the response arrives, libuv wakes us up and we call `PQconsumeInput()` — which never blocks because we already know the data is ready.

## Queen's Architecture

With these concepts clear, we can look at Queen's final architecture. The system uses the **acceptor/worker pattern** to scale across multiple cores:

```
                         ┌─────────────────────────────────────────────────────────────┐
                         │                         QUEEN SERVER                        │
                         └─────────────────────────────────────────────────────────────┘
                                                      │
                                                      ▼
                         ┌─────────────────────────────────────────────────────────────┐
                         │              ACCEPTOR (port 6632, round-robin)              │
                         │                    uWebSockets event loop                   │
                         └────────────┬───────────────┬───────────────┬────────────────┘
                                      │               │               │
              ┌───────────────────────┼───────────────┼───────────────┼────────────────────────┐
              │                       │               │               │                        │
              ▼                       ▼               ▼               ▼                        ▼
┌─────────────────────┐ ┌─────────────────────┐           ┌─────────────────────┐ ┌─────────────────────┐
│    UWS WORKER 0     │ │    UWS WORKER 1     │    ...    │   UWS WORKER N-1    │ │    UWS WORKER N     │
│   (event loop)      │ │   (event loop)      │           │   (event loop)      │ │   (event loop)      │
│                     │ │                     │           │                     │ │                     │
│  ┌───────────────┐  │ │  ┌───────────────┐  │           │  ┌───────────────┐  │ │  ┌───────────────┐  │
│  │ HTTP Handler  │  │ │  │ HTTP Handler  │  │           │  │ HTTP Handler  │  │ │  │ HTTP Handler  │  │
│  └───────┬───────┘  │ │  └───────┬───────┘  │           │  └───────┬───────┘  │ │  └───────┬───────┘  │
│          │          │ │          │          │           │          │          │ │          │          │
│          ▼          │ │          ▼          │           │          ▼          │ │          ▼          │
│  ┌───────────────┐  │ │  ┌───────────────┐  │           │  ┌───────────────┐  │ │  ┌───────────────┐  │
│  │  Mutex Queue  │  │ │  │  Mutex Queue  │  │           │  │  Mutex Queue  │  │ │  │  Mutex Queue  │  │
│  └───────┬───────┘  │ │  └───────┬───────┘  │           │  └───────┬───────┘  │ │  └───────┬───────┘  │
│          │          │ │          │          │           │          │          │ │          │          │
│          ▼          │ │          ▼          │           │          ▼          │ │          ▼          │
│  ┌───────────────┐  │ │  ┌───────────────┐  │           │  ┌───────────────┐  │ │  ┌───────────────┐  │
│  │ LIBQUEEN 0    │  │ │  │ LIBQUEEN 1    │  │           │  │ LIBQUEEN N-1  │  │ │  │ LIBQUEEN N    │  │
│  │ (libuv loop)  │  │ │  │ (libuv loop)  │  │           │  │ (libuv loop)  │  │ │  │ (libuv loop)  │  │
│  │               │  │ │  │               │  │           │  │               │  │ │  │               │  │
│  │ Timer (5ms)   │  │ │  │ Timer (5ms)   │  │           │  │ Timer (5ms)   │  │ │  │ Timer (5ms)   │  │
│  │      │        │  │ │  │      │        │  │           │  │      │        │  │ │  │      │        │  │
│  │      ▼        │  │ │  │      ▼        │  │           │  │      ▼        │  │ │  │      ▼        │  │
│  │ Microbatch    │  │ │  │ Microbatch    │  │           │  │ Microbatch    │  │ │  │ Microbatch    │  │
│  │      │        │  │ │  │      │        │  │           │  │      │        │  │ │  │      │        │  │
│  │      ▼        │  │ │  │      ▼        │  │           │  │      ▼        │  │ │  │      ▼        │  │
│  │ PG Pool (M)   │  │ │  │ PG Pool (M)   │  │           │  │ PG Pool (M)   │  │ │  │ PG Pool (M)   │  │
│  └───────────────┘  │ │  └───────────────┘  │           │  └───────────────┘  │ │  └───────────────┘  │
└─────────────────────┘ └─────────────────────┘           └─────────────────────┘ └─────────────────────┘
              │                       │               │               │                        │
              └───────────────────────┴───────────────┴───────────────┴────────────────────────┘
                                                      │
                                                      ▼
                                         ┌─────────────────────────┐
                                         │       PostgreSQL        │
                                         │    (N×M connections)    │
                                         └─────────────────────────┘
```

There are also some other services like Retention and Eviction, that are not shown in the diagram above, usually run in a separate thread started from the worker 0.

### How it works

1. **Acceptor**: A single thread that listens on port 6632 and distributes connections to workers in round-robin fashion. It does nothing but pass sockets along.

2. **UWS Workers**: Each worker is a thread with its own uWebSockets event loop. When an HTTP request arrives (e.g. push), the worker:
   - Parses the request
   - Pushes the operation onto a mutex-protected queue
   - **Does not wait** — the HTTP response will be sent when Postgres responds

3. **libqueen**: Each worker has its own libqueen instance, which runs in a separate thread with its own libuv event loop. libqueen:
   - Has a timer that fires every 5ms
   - Drains the operations queue
   - Groups operations by type (PUSH, POP, ACK, etc.)
   - Sends **a single query** to Postgres for each type
   - When Postgres responds, invokes the callbacks for each request

4. **No lock contention between workers**: Each worker has its own queue, its own libqueen instance, and its own connection pool. Workers never talk to each other.

### Why 1 libqueen per 1 uWS worker?

The alternative would be to have a single libqueen instance shared among all workers. But this would create:
- Lock contention on the shared queue
- A single libuv loop becoming the bottleneck
- More complexity in callback management

With the 1:1 approach, each worker is completely independent. If you have 12 workers, you have 12 libuv loops running in parallel, each with its own pool of Postgres connections.

## Microbatching 

In practice, Queen doesn't use *uv_async_send* to wake up the loop, but uses *uv_timer_start* to execute an operation periodically. In short, every time one of the uWebSockets workers receives a request, it pushes the operation to be performed onto a mutex-protected queue. 
The libuv timer is set to fire every 5 ms, so every time it's called, the loop finds a series of operations to execute. 
These operations can be of type:
- PUSH
- POP
- ACK
- TRANSACTION
- RENEW_LEASE
- CUSTOM

The code then groups operations by type, and sends a single query to Postgres for each type of operation, batching the operations and reducing the number of round trips with the database. For this to work, obviously the queries need to be designed to work in batch. This technique slightly increases the latency of a single operation, but increases throughput significantly. For example, during the benchmark described below, each Queen worker executed on average 1000 batched operations per second, which means that from 1000 requests per second arriving on that worker via HTTP, a single call to Postgres was made. 

During the final phase of development, all the queries that were previously scattered throughout the application code were transformed into Postgres stored procedures, which also makes it possible to "use Queen" directly from psql. 

## Benchmarks

### 1. Sustained load test

Queen wasn't designed to break records or to be blazing fast, but rather to be simple, maintainable, and to easily handle slow processes: this led to the choice of using HTTP and "dumb" stateless clients and servers, without any special coordination between the server and the clients.

But from the beginning I wanted to see how fast it could go. The versions before libuv were very slow, and the only way to make them fast was to do client-side batching, to reduce the number of round trips with the server and database, i.e. pushing and popping multiple messages in a single round trip (which is absolutely normal and done by all queue systems). With the C++ version with libuv and microbatching, Queen turned out to be pretty fast, showing push peaks of 45k req/s. But dealing with Postgres and all the vacuum mechanisms and tables growing, the question that came to mind was: how much sustained load can it handle? **So I set myself the challenge: push 10000 messages per second (in single requests of 1 message each) and consume them for a week straight.**

Queen has three main operations: PUSH, POP and ACK. PUSH and POP are the most important, and are the ones that are most often used. ACK is used to acknowledge that a message has been processed. So the flow is:
1. Client sends a PUSH request to the server
2. Server pushes the message to the database
3. Another client sends a POP request to the server
4. Server pops the message from the database
5. The client receives the message, processes it and sends an ACK request to the server
6. Server acknowledges the message

For the benchmark, I used a special POP request that ACK the message automatically, moving down the quality of service to "at most once". This due the fact the Queen clients are not designed to be a benchmark tools, so I needed to use some other tool to create a decent load. Those third party tools usually do not allow to implement logic like "do pop and then ack it".
Due the fact that Queen uses HTTP, I had a plethora of tools to choose from, and I used *autocannon* with this configuration: 

Producer:

```javascript
// Per 10 workers like this, using cluster mode
const instance = autocannon({
  url: SERVER_URL,
  connections: 100,
  duration: '1 week',
  requests: [Array of requests]
});
```

```javascript
requests.push({
    method: 'POST',
    path: '/api/v1/push',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      items: [{
        queue: QUEUE_NAME,
        partition: `${i}`,
        payload: { 
          message: "Hello World",
          partition_id: i
        }
      }]
    })
});
```

Consumer:
```javascript
const instance = autocannon({
  url: SERVER_URL,
  connections: 50,
  duration: '1 week',
  requests: [Array of requests],
  workers: 1
});
```

```javascript
    requests.push({
      method: 'GET',
      path: `/api/v1/pop/queue/${QUEUE_NAME}?batch=${batchSize}&wait=true&autoAck=true`, // Note autoAck=true to acknowledge the message automatically
      headers: { 'Content-Type': 'application/json' }
    });
```

Also note that for POP we do not specify the partition, simulating a real consumer that usually do not know the partition of the message, but rather the queue name. This require the server to find available partitions for each POP request.

So the benchmark setup is like this: 
- 1 single machine with 32 cores and 64GB of RAM, 2TB disk 
- Postgres and Queen as docker containers, without cpuset
- 1 queue with 1000 partitions
- 10 producers with total 1000 connections 
- 1 consumer with 50 connections, batch size to 50 (so the POP calls usually return more than one message, if present)

The Queen and Postgres config were:

```sh
docker network create queen

docker run -d --ulimit nofile=65535:65535 --name postgres --network queen -e POSTGRES_PASSWORD=postgres -p 5432:5432 postgres -c shared_buffers=4GB -c max_connections=300 -c temp_buffers=128MB -c work_mem=64MB -c max_parallel_workers=18 -c max_worker_processes=18 -c checkpoint_timeout=30min -c checkpoint_completion_target=0.9 -c max_wal_size=16GB -c min_wal_size=4GB -c wal_buffers=64MB -c wal_compression=on -c synchronous_commit=off -c autovacuum_vacuum_cost_limit=2000 -c autovacuum_vacuum_cost_delay=2ms -c autovacuum_max_workers=4 -c maintenance_work_mem=2GB -c effective_io_concurrency=200 -c random_page_cost=1.1 -c effective_cache_size=32GB -c huge_pages=try

docker run -d --ulimit nofile=65535:65535 --name queen -p 6632:6632 --network queen -e PG_HOST=postgres -e PG_PASSWORD=postgres -e NUM_WORKERS=10  -e DB_POOL_SIZE=50 -e SIDECAR_POOL_SIZE=250 -e SIDECAR_MICRO_BATCH_WAIT_MS=20 -e POP_WAIT_INITIAL_INTERVAL_MS=10 -e POP_WAIT_BACKOFF_THRESHOLD=3 -e POP_WAIT_BACKOFF_MULTIPLIER=2.0 -e POP_WAIT_MAX_INTERVAL_MS=1000 -e DEFAULT_SUBSCRIPTION_MODE=new -e RETENTION_INTERVAL=300000 -e RETENTION_BATCH_SIZE=500000 -e LOG_LEVEL=info -e DB_STATEMENT_TIMEOUT=300000 -e STATS_RECONCILE_INTERVAL_MS=8640000 smartnessai/queen-mq:0.11.0-dev-7
```

![Degradation](/learn/degradation.png)
*Degradation of performance after a few million messages*

The first experiments were embarrassing. After a few million messages, performance degraded drastically. With the invaluable help of AI debugging, which knows Postgres way better than me, I discovered that most of the problem was due to these things:

#### Indexes blocking HOT updates

PostgreSQL has a mechanism called **HOT (Heap-Only Tuple) updates**: when you update a row and the indexed columns DON'T change, Postgres can do an "update in place" without creating dead tuples. But if even ONE indexed column changes, you have to create a new tuple and the old one becomes "dead".

![Index bloat visualization](/learn/index_bloat_2.png)

*You can see the situation in the screenshot above, where the index is bloated with dead tuples, 6k req/s, than full vacuum and finally index removed*


I had an index `(queue_name, last_message_created_at)` on a table that was updated on every push. Result: after 10 hours, a table with 1000 rows weighed **94MB** instead of ~100KB. 600x bloat!

```sql
-- Before: 0.8% HOT updates (disaster)
-- After dropping the useless index: 99.9% HOT updates
SELECT n_tup_hot_upd::numeric / n_tup_upd * 100 as hot_pct
FROM pg_stat_user_tables WHERE relname = 'partition_lookup';
```

The lesson: **every index has a cost**. If a column is updated frequently, don't index it unless strictly necessary.

#### NOT IN vs NOT EXISTS

I had a query to find partitions without messages:

```sql
-- WRONG: scans the ENTIRE messages table to build the list
WHERE partition_id NOT IN (
    SELECT DISTINCT partition_id FROM queen.messages
)

-- CORRECT: uses the index and stops at first match
WHERE NOT EXISTS (
    SELECT 1 FROM queen.messages m 
    WHERE m.partition_id = p.id LIMIT 1
)
```

With millions of rows, the first version took 77 seconds and blocked everything. The second is instant.

#### Duplicate indexes

I had accidentally created two identical indexes:
```sql
idx_messages_partition_created    = (partition_id, created_at, id)
idx_messages_partition_created_id = (partition_id, created_at, id)  -- IDENTICAL!
```

3.1GB of wasted space, and every INSERT had to update both.

#### Checkpoints too frequent

With the `max_wal_size=16GB` setting, Postgres was doing a checkpoint every ~5 minutes under load. Every checkpoint = flush of all dirty buffers = I/O spike = latency spike.

```sql
-- 202 forced checkpoints, 0 scheduled = WAL too small
SELECT num_timed, num_requested FROM pg_stat_checkpointer;
```

It wasn't possible to increase the WAL size during the benchmark because it would have meant stopping it, but it's something to keep in mind for next time.

After these fixes, discoverable only through sustained effort, Queen and Postgres became steady as a clock, doing between 9500 and 10500 req/s every second. 

Those are the useful queries we made to monitor the situation:

```sql
SELECT pid, now() - query_start AS duration, state, query 
FROM pg_stat_activity                                          
WHERE state != 'idle' 
ORDER BY duration DESC;

-- Get dead tuples
SELECT schemaname, relname, n_dead_tup, n_live_tup, 
       round(n_dead_tup::numeric / nullif(n_live_tup, 0) * 100, 2) as dead_pct,
       last_vacuum, last_autovacuum
FROM pg_stat_user_tables
ORDER BY n_dead_tup DESC
LIMIT 10;

-- Check for any slow queries
SELECT pid, now() - query_start AS duration, left(query, 80) 
FROM pg_stat_activity 
WHERE state = 'active' AND query_start < now() - interval '1 second'
ORDER BY duration DESC;

-- Check autovacuum isn't stuck
SELECT relname, last_autovacuum, n_dead_tup 
FROM pg_stat_user_tables 
WHERE n_dead_tup > 1000 
ORDER BY n_dead_tup DESC;

-- HOT check
SELECT 
    n_tup_upd,
    n_tup_hot_upd,
    round(n_tup_hot_upd::numeric / nullif(n_tup_upd, 0) * 100, 2) as hot_update_pct
FROM pg_stat_user_tables 
WHERE relname = 'partition_lookup';
```

Obviously it's not possible to keep all messages on disk for such a long period (we're talking about several TB of data), so I set the cleanup service very aggressively, so there were never more than half an hour of messages on disk (about 30 million). The performance figures above include the cleanup service that removes old messages.

After almost three days of pushing and consuming with a steady load, I interrupted the benchmark: it was clear that there will be any issue in the next days, so I stopped, deployed a new version of Queen (with better charts) and resumed the benchmark. 

**In the end the challenge was won: Queen is able to push and consume around 10000 messages per second for days straight, using only 80 GB of disk space.**

![1 Billion](/learn/1billion.png)

*The first billion (1.322B) messages*

![2Gb/s](/learn/do.png)
*Writing 2Gb/s of "hello world" inside Postgres for days (of course this include a lot more that "hello world")*


After 2.5 days the situation was like this:

```sh
CONTAINER ID   NAME       CPU %      MEM USAGE / LIMIT     MEM %     NET I/O           BLOCK I/O         PIDS 
a78d0500a542   queen      324.78%    146.5MiB / 62.79GiB   0.23%     2.55TB / 6.64TB   14.3MB / 0B       51 
673130dd43d8   postgres   1001.05%   46.82GiB / 62.79GiB   74.57%    456GB / 982GB     30.1GB / 43.3TB   307
```

We have produced, consumed and also deleted more than 2 billion messages, at an average rate of 9500 req/s. In this time, Queen used 133 MB or RAM steadily.

![Final](/learn/operations-1.png)

### 2. Additional benchmarks

Due to the fact that Queen was able to sustain the load for days, I've also made some other smaller benchmarks to test the scalability of the system and the performance of the different features.

#### Push batch (1000 messages per request) and Pop

Avg throughput: 31170 msg/s push and pop, disk writing at almost 1GB/s (10 Gb/s), > 2M messages per minute.
I'm not sure but maybe here we are very close to the limit of WAL write speed.

To make this throughput sustainable, I needed to tweak the Postgres settings:

```ini
shared_buffers=1GB 
temp_buffers=64MB
work_mem=64MB
max_wal_size=4GB
min_wal_size=1GB
```

And also tweak the autovacuum settings:
```sql
ALTER TABLE queen.messages SET (
    autovacuum_vacuum_scale_factor = 0.001, 
    autovacuum_vacuum_threshold = 100,
    autovacuum_vacuum_cost_delay = 0,
    autovacuum_vacuum_cost_limit = 10000, 
    toast.autovacuum_vacuum_cost_delay = 0
);

ALTER TABLE queen.messages SET (
    autovacuum_vacuum_insert_scale_factor = 0.001
);
```

![Push batch and Pop](/learn/10gb.png)

![Push batch and Pop](/learn/30k.png)

#### Consumer group consumption 

In this test we have a producer that produce on a queue with 1000 partitions, 1 with 100 connections and batch size 10, and 10 consumers group on 5 worker with 50 connections each. This test aims to test the scalability of the consumer group feature, a pub-sub pattern, where each consumer group receives all the messages from the queue.

![Consumer group consumption](/learn/cg1.png)
*At the peak, Queen was using 10 cores, with a huge event loop lag, with 250k msg/s pop throughput*

At the beginning, probably due to the fact that the benchmark did not warm up and started nuking Queen, the consumer pop rate was zero, accumulating messages in the queue. Then after some minutes, the consumer group started to pop messages at an initial rate of 250k msg/s, then stabilized at 6k push and (6k push × 10 consumers) 60k msg/s pop rate, using almost three cores and 1GB of RAM.

![Consumer group consumption](/learn/cg2.png)
*I was surprised about the pop performance in the consumer group mode. Queen is primarily designed to be a queue system, but it seems to be able to handle the pub-sub pattern with ease.*

### 3. Benchmark Results

| Test | Producer Config | Consumer Config | Throughput | Resources |
|------|-----------------|-----------------|------------|-----------|
| **Sustained load** | 10 workers, 1000 connections, 1 msg/req | 1 worker, 50 connections, batch 50, autoAck | ~10k req/s (sustained for days) | Queen: 133 MB RAM, ~3 cores |
| **Push batch** | 1 worker, 50 connections, 1000 msgs/request | 5 workers, 50 connections each, autoAck | ~31k msg/s | Disk: ~1 GB/s write |
| **Consumer groups** | 1 producer, 100 connections, batch 10 | 10 consumer groups, 5 workers, 50 connections each, autoAck | 6k push / 60k pop msg/s | Queen: 1 GB RAM, ~3 cores |

## Conclusions

This is not rocket science and mainly due to my own ignorance, developing Queen in C++ cost me a lot in terms of time and mental health, but now that the end is in sight, I feel I understand mechanisms that I used to know only in theory much better in practice. I hope this document can help someone else do the same. I'm sure Queen can push more than this (I've just discovered that I'm using the slowest JSON library in the C++ world, for instance), but I think this is a good starting point.


### References
 
- [uWebSockets](https://github.com/uNetworking/uWebSockets)
- [libuv](https://docs.libuv.org/en/v1.x/)
- [queen](https://smartpricing.github.io/queen/)

