# RabbitMQ comparison — `rabbitmq-1000q` (measured)

**Run:** `2026-04-26T11:56:55Z → 12:11:55Z` (15 min steady state)
Image: `rabbitmq:3.12-management` (single-node, classic queues, persistent)
Same host as Queen `bp-10` and Kafka `kafka-1000p` (32 vCPU, 62 GiB RAM, no swap).

> **Methodology note**: The official RabbitMQ images had a stubborn `eacces` error reading `/var/lib/rabbitmq/.erlang.cookie` on this kernel when launched normally. Working around required bypassing `docker-entrypoint.sh` and running `rabbitmq-server` directly with `RABBITMQ_NODENAME=rabbit@localhost`. The benchmark itself used the official `pivotalrabbitmq/perf-test` tool with standard parameters — no atypical tuning.

## Setup

| Parameter | Value |
|---|---|
| Queues | **1000 classic queues** (`bench-q-1` … `bench-q-1000`) |
| Producer | `pivotalrabbitmq/perf-test`, 1 producer, `--rate -1` (max), `--confirm 200` (publisher confirms with 200 outstanding) |
| Consumer | 1 consumer, `--qos 100`, `--multi-ack-every 100` (manual ack every 100 msgs) |
| Persistence | `--flag persistent` (delivery_mode=2, message persisted to disk) |
| Message size | 28 bytes (matching Queen's payload) |
| Connection | Single AMQP connection from perf-test container, multi-channel |

## Producer/consumer — final perf-test result

```
test stopped (Reached time limit)
sending rate avg:    34 750 msg/s
receiving rate avg:  34 750 msg/s
consumer latency min/median/75th/95th/99th/max:   159 / 1755 / 2443 / 3710 / 4731 / 11340 µs
confirm latency min/median/75th/95th/99th/max:    1121 / 4866 / 5814 / 7460 / 9276 / 22840 µs
```

**34 750 msg/s sustained** for 14 min (perf-test stopped at the time limit). Producer and consumer perfectly balanced.

## Resource consumption

| Metric | Value |
|---|---|
| RabbitMQ CPU avg | **~145 %** (~1.5 vCPU), peaks to ~150 % |
| RabbitMQ RSS at end | **187.8 MB** |
| RabbitMQ block I/O | ~9 GB written |
| RabbitMQ network I/O | 5.3 GB / 5.3 GB (in/out) |
| Mnesia data dir | 624 KB at end (queue metadata only; messages in WAL) |
| perf-test CPU | ~50 % (~0.5 vCPU client) |
| perf-test RSS | ~770 MB JVM heap |

## Direct comparison: Queen `bp-10` vs Kafka `kafka-1000p` vs RabbitMQ `rabbitmq-1000q` (all measured)

| Metric | Queen `bp-10` | Kafka `kafka-1000p` | RabbitMQ `rabbitmq-1000q` |
|---|---:|---:|---:|
| Topology | 1 queue, 1001 partitions | 1 topic, 1000 partitions | 1000 queues |
| Persistence | PG fsync on commit | acks=1 (page cache) | delivery_mode=2 + confirms |
| **Push msg/s** | **39 060** | **1 520 538** | **34 750** |
| Push p50 latency | 11 ms | 1 117 ms | 1.8 ms (consumer p50, similar) |
| **Push p99 latency** | **38 ms** | **2 966 ms** (saturated) | **9.3 ms** (confirm p99) |
| Pop msg/s | 38 351 | ~1 500 000 | 34 750 |
| **Server RSS / heap** | **52 MB** | 3.1–7.2 GB | **188 MB** |
| **Server CPU avg** | **7.4 vCPU** | 3.5 vCPU | **~1.5 vCPU** |
| **CPU per Mmsg/s** | 190 vCPU | **2.3 vCPU** | **43 vCPU** |
| Disk written | ~14 GB (msgs table) | ~36 GB (1B msgs) | ~9 GB |

## What this comparison actually shows

### Throughput

At single-node with persistent durability:
- **RabbitMQ ≈ Queen** at this workload shape (34.7 k vs 39 k msg/s — 12 % spread, within tuning noise).
- Kafka is in a different league (~40× faster), but also runs unsaturated here.

### Latency

RabbitMQ is the **clear winner on latency**:
- RabbitMQ confirm p99: **9.3 ms**
- Queen push p99: 38 ms (~4× higher)
- Kafka push p99: 2 966 ms under saturation (Kafka would match the others if rate-limited; the unbounded run produced this back-pressure)

For **request/response or near-real-time pipelines under modest load**, RabbitMQ has the lowest tail latency on this hardware.

### Memory

| | RSS | per-Mmsg/s |
|---|---:|---:|
| Queen | 52 MB | 1.3 KB |
| RabbitMQ | 188 MB | 5.4 KB |
| Kafka | ~5 GB (avg) | 3.3 KB |

Queen has the smallest absolute footprint by ~3.6×, but **RabbitMQ is much smaller than I expected** (and much smaller than Kafka). All three are reasonable on a modern host. **Queen's "70 MB RSS" advantage over RabbitMQ is real but only matters on very small VMs (< 2 GiB)**.

### CPU efficiency

This is where the comparison gets interesting. **RabbitMQ used 5× less CPU than Queen for similar throughput** (1.5 vCPU vs 7.4 vCPU). Two likely reasons:

1. **Protocol cost**: Queen uses HTTP/JSON. RabbitMQ uses AMQP binary. AMQP is much cheaper to encode/decode per message.
2. **Per-message DB cost**: Queen does a real Postgres INSERT + index update + trigger fire per push batch. RabbitMQ writes to its own optimized log + Mnesia metadata. Less per-message machinery.

Kafka is even more CPU-efficient because it's just appending to mmap'd log files at the broker level.

So the throughput-per-CPU ranking is: **Kafka >> RabbitMQ > Queen** (190 vCPU/Mmsg/s for Queen vs 43 for RabbitMQ vs 2.3 for Kafka).

## Where Queen wins despite the resource numbers

The architectural differences (which I covered in earlier conversations) still favor Queen for specific workload shapes:

1. **Per-key ordering with parallel consumers** — RabbitMQ classic queues don't natively offer this. Multiple parallel consumers on a single queue process in parallel and break end-to-end ordering. The only RabbitMQ workaround is **1 queue per ordering scope**, which is operationally heavy at high cardinality (100k+ entities). Queen's lease-based partition consumers give this for free.
2. **Replay from timestamp** — Queen has it (subscribe-from-timestamp + UUIDv7 + retention). RabbitMQ classic queues don't. (RabbitMQ streams do, but they're a different feature with different semantics.)
3. **Transactional integration with Postgres** — Insert business row + push message in one PG transaction. RabbitMQ has no equivalent.
4. **Dynamic high-cardinality partitions** — Queen creates a partition row on first push to a new key. Free. RabbitMQ would need 1 queue per scope, with all the overhead that implies.

## Where RabbitMQ wins decisively

1. **Latency under modest load**: ~4× lower p99 latency.
2. **CPU efficiency**: ~5× less CPU for similar throughput.
3. **Rich routing** (exchanges, routing keys, headers, alternate exchanges, dead-letter routing, priority queues) — Queen's model is simpler and doesn't have these.
4. **Mature ecosystem**: 15+ years, dozens of clients, proven at scale by many companies.
5. **Streams** for high-throughput log-style use (~1 M msg/s, but a different feature set).

## Honest assessment

> **At 1000 ordering scopes on a single node, RabbitMQ ≈ Queen on throughput, RabbitMQ ≈ 4× lower latency, Queen ≈ 3.6× smaller memory footprint, RabbitMQ ≈ 5× lower CPU. The decisive differences are architectural (per-key ordering, replay, transactional integration with PG, dynamic partition cardinality) — not resource cost.**

For a workload that **specifically needs per-key ordering with parallel consumers** or **replay from timestamp** or **transactional consistency with PG**, Queen is the right choice. For a workload that needs **rich routing, lowest-possible latency, or fits the established RabbitMQ patterns**, RabbitMQ is the right choice.

The "Queen vs RabbitMQ on the same workload" comparison is more nuanced than I initially expected. **Throughput is a tie, footprint trades off** (Queen lighter on memory, RabbitMQ lighter on CPU and latency). What separates them is the feature set and the architectural fit, not the raw numbers.

## Caveats on this run

1. **`acks=1` was Kafka's setting; RabbitMQ used `delivery_mode=2 + confirm`; Queen used `synchronous_commit=on`**. These are not exactly the same durability tier — Queen is strictly stronger (PG WAL fsync), Kafka is weakest (page cache write only on single broker), RabbitMQ is in between (writes to disk before confirm).
2. **Single-broker comparison**. All three would behave differently with replication. Kafka especially benefits from multi-broker. Queen has no native multi-broker. RabbitMQ classic mirrored queues exist but are deprecated; quorum queues (Raft) are the path forward.
3. **Single-producer, single-consumer comparison**. RabbitMQ scales poorly under N parallel consumers on a single queue (HOL/ordering loss); Queen scales well across partitions. The 34.7 k figure here is RabbitMQ's "best case" with 1 consumer; under the workloads where Queen's lease-based-partition design shines, RabbitMQ classic queues either lose ordering or need 1-queue-per-scope.
4. **Persistent classic queues only**. RabbitMQ has streams (~1 M msg/s, log-style) and quorum queues (~30 k msg/s, Raft-replicated) as alternative queue types. Each has its place; classic was the closest analog to Queen's queue-with-ack model.
