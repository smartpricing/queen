# Queen pipeline benchmark — throughput-tuned config · 2026-04-27

A second run of the same 4-stage pipeline (`[pipeline-queen.md](./pipeline-queen.md)`),
with the partition knob slid to the **other end of the design space** — fewer,
larger lanes optimised for raw throughput.

## What changed from the high-cardinality run

The pipeline shape is identical (2 producers → q1 → 7 workers → q2 → 7 analytics

- 7 log), the durability tier is identical (`synchronous_commit=on`), the
work simulation is identical (5–20 ms long-tail per message). Only the
partition-knob configuration changed:


| knob                           | high-cardinality run | **this run (throughput-tuned)**             |
| ------------------------------ | -------------------- | ------------------------------------------- |
| Partitions per queue           | 1 000                | **10**                                      |
| Pop batch size                 | 100                  | **1 000**                                   |
| Partitions claimed per pop     | 10                   | **1** (each consumer claims one whole lane) |
| Per-consumer concurrency       | 10                   | 2                                           |
| Producer in-flight per process | 40                   | **100**                                     |
| Per-producer target rate       | 5 000/s              | 10 000/s                                    |
| Run duration                   | 20 min               | 10 min                                      |
| Warm-up discarded              | 3 min                | 1 min                                       |


The architectural intuition: with 10 partitions and 14 consumer slots
(7 workers × concurrency=2), each lane is essentially "owned" at any
moment by a different consumer slot. With `partitions=1`, the multi-claim
loop stops after the first claim — so each pop drains one full lane's
worth of messages in a single round-trip, no advisory-lock contention,
no scan overhead.

## Headline numbers

Steady state (after 1-min warm-up, over the remaining 9 min):


| metric                   | value                                               |
| ------------------------ | --------------------------------------------------- |
| **Producer push rate**   | **6 673 msg/s** aggregate (2 procs × 3 337)         |
| **Worker drain rate**    | **6 665 msg/s** aggregate                           |
| **Analytics drain rate** | **6 662 msg/s** (matches worker — q2 stays drained) |
| **Log drain rate**       | **6 663 msg/s** (matches worker — q2 stays drained) |
| **Total pipeline ops/s** | ~40 000 (push + pop + push + ack across stages)     |
| Producer txids in window | 3 603 415                                           |
| Reached worker           | 3 599 084 (**99.88 %**)                             |
| Reached analytics        | 3 597 720 (**99.84 %**)                             |
| Reached log              | 3 597 892 (**99.85 %**)                             |
| Duplicate processing     | **0** (worker, analytics, log)                      |
| Deadlocks observed       | **0**                                               |
| File-buffer files at end | **0**                                               |


Producer and worker drain are throughput-balanced; q1 and q2 both stay
drained throughout the run.

## Latency profile (steady state, ms)


| stage                             | p50     | p90 | p99       | max       | avg |
| --------------------------------- | ------- | --- | --------- | --------- | --- |
| **end-to-end producer→analytics** | **755** | 960 | **1 103** | **1 747** | 766 |
| end-to-end producer→log           | 757     | 962 | 1 103     | 1 664     | 766 |
| q1 → worker (queue lag at q1)     | 440     | 634 | 745       | 1 010     | 433 |
| q2 → analytics (queue lag at q2)  | 312     | 450 | 601       | 999       | 322 |
| q2 → log (queue lag at q2)        | 313     | 453 | 598       | 816       | 323 |


**Max latency is 1.75 s — 9× tighter** than the high-cardinality run's 15.5 s
tail. With only 10 lanes and 14 concurrent claim slots, every lane is being
drained at any moment; no partition gets left unclaimed for many cycles.

The p50 of 755 ms is the **amortisation tax**: at 6 673 msg/s spread across
10 partitions, each partition fills 667 msg/s. A pop with `batch=1000`
collects ~1 000 messages worth of arrivals — meaning a message that lands
just after the previous pop on its partition waits ~1.5 s before the next
pop fires. Half-fill time is ~750 ms, which is exactly the p50 you see.
This is the textbook latency-vs-throughput trade.

## Resource usage (steady state)


| container | CPU                   | memory                      | block I/O        |
| --------- | --------------------- | --------------------------- | ---------------- |
| queen     | ~340 % (3.4 vCPU)     | ~175 MB                     | none (PG-backed) |
| postgres  | **~470 % (4.7 vCPU)** | grew to 12.3 GB over 10 min | 56.5 GB written  |


**Postgres CPU is 3.2× lower than the high-cardinality run** at nearly
double the message throughput. Bigger pop batches amortise the per-call
SQL overhead so dramatically that the per-message PG cost collapses.

The system is now **producer-bound, not PG-bound** — the single-message-per-call
HTTP push path is the limit. Adding 4–8 more producer processes would push
the system into the 30–50 k msg/s range without changing anything broker-side.

## What this run validates

1. **Partition count is a continuous knob, not a binary choice.**
  - 1 000 partitions with `partitions=10` per pop → 3.7 k/s end-to-end, p50 = 359 ms,
   per-entity ordering preserved
  - 10 partitions with `partitions=1` per pop and batch=1 000 → 6.7 k/s end-to-end,
  p50 = 755 ms, per-shard (weak) ordering preserved
  - Same engine, same SDK, same durability. One configuration setting.
2. **Throughput-mode achieves competing-consumers semantics without a separate
  code path.** With 10 partitions, the 14 consumer slots claim disjoint
   lanes; each acts as a competing consumer with prefetch=1 000. The semantics
   match the classic single-queue/multi-consumer pattern, while still
   preserving FIFO order *within each lane* as a free bonus.
3. **The PG cost per useful message drops sharply with bigger batches.**
  PG used 4.7 vCPU at 6.7 k/s here vs 15 vCPU at 3.7 k/s in the
   high-cardinality run — a 6× reduction in vCPU-per-msg. Bigger batches
   are how you scale on a Postgres-backed engine.
4. **0 duplicates, 0 deadlocks, 0 lost messages, 99.88 % delivery
  completeness.** The remaining 0.12 % are messages still in flight at the
   analyze cutoff (q1 / q2 had small in-flight counts). All correctness
   invariants held.

## Why we picked these specific knob values

- `**max_partitions=1` per pop**: with 10 lanes and 14 consumer slots, having
a pop claim 1 lane (instead of all 10 in one call) means every consumer
slot is drainable simultaneously. The `max_partitions=10` config we used
in the high-cardinality run was for the opposite case (1 000 lanes, 70
slots, partitions sparse), where you want one pop to harvest from many
lanes at once.
- `**batch=1000`**: with each lane filling at ~670 msg/s steady state, a
1 000-msg cap means the pop captures roughly one cycle's worth of
arrivals per call. Bigger would just wait longer; smaller would split
one cycle across multiple pops.
- `**concurrency=2**` per worker × 7 workers = 14 slots: 4 more than the
10 lanes, so when one consumer briefly drops out (between pop and ack)
another picks up. Higher concurrency would just mean more failed
advisory-lock probes.
- `**IN_FLIGHT=100**` per producer: pushes are now CPU/HTTP-bound on the
producer; more in-flight requests reduce per-call wait.

## Reproducing

```bash
ssh root@<host>
cd /root/pipeline-runs/queen
DURATION_SEC=600 WARMUP_SEC=60 \
  NUM_PRODUCERS=2 NUM_WORKERS=7 NUM_ANALYTICS=7 NUM_LOG=7 \
  NUM_PARTITIONS=10 \
  TARGET_RATE=10000 IN_FLIGHT=100 \
  CONCURRENCY=2 BATCH_SIZE=1000 \
  MAX_PARTITIONS_PER_POP=1 \
  QUEEN_IMAGE_TAG=0.14.0.alpha.5 \
  ./runner.sh
```

Run artifacts: `/root/bench-runs/pipeline-results/queen-20260427T121144Z/`
(summary.json/.txt, per-process JSONL log, queen.log, queue resource
snapshots, metadata.json).

## Caveats

- **Producer-side single-message-per-call** caps push rate at ~3 300 msg/s
per producer process. Real-world systems would use producer batching for
this scale (or just spin up more producer processes). We chose
single-message push to mirror "microservice publishes one event at a
time", which is the harder case.
- **p50 penalty is real.** This config trades latency for throughput. If
your application can tolerate ~750 ms median end-to-end latency, you get
6.7 k/s sustained; if you need 350 ms median, the high-cardinality
config gives you that at half the throughput.
- One run, one VM size. The trend (more partitions → more parallelism →
more PG cost; fewer partitions → bigger amortisation → more throughput
→ less PG cost) is robust to scale, but the exact numbers depend on
hardware.

