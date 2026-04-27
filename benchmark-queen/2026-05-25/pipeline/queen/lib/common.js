// Shared helpers across producer/worker/consumer.

import fs from 'node:fs'
import path from 'node:path'

// ---------- Long-tail processing-time simulation ----------
// Calibrated so avg ≈ 13 ms with a real long tail.
//   95%: 5-15 ms uniform     (happy path)
//    4%: 20-50 ms uniform    (warm cache miss)
//    1%: 100-300 ms uniform  (cold path / retry)
export function longTailDelayMs() {
  const r = Math.random()
  if (r < 0.95) return 5 + Math.random() * 10
  if (r < 0.99) return 20 + Math.random() * 30
  return 100 + Math.random() * 200
}

export function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

// ---------- Token-bucket rate limiter ----------
// Used by producers to cap their per-process push rate.
export class TokenBucket {
  constructor(ratePerSec) {
    this.rate = ratePerSec
    this.tokens = 0
    this.last = Date.now()
  }

  async consume(n = 1) {
    while (true) {
      const now = Date.now()
      const elapsed = (now - this.last) / 1000
      this.tokens = Math.min(this.rate, this.tokens + elapsed * this.rate)
      this.last = now
      if (this.tokens >= n) {
        this.tokens -= n
        return
      }
      const waitMs = ((n - this.tokens) / this.rate) * 1000
      await sleep(Math.min(50, Math.max(1, waitMs)))
    }
  }
}

// ---------- JSONL logger (per-process append) ----------
export class JsonlLogger {
  constructor(filePath) {
    fs.mkdirSync(path.dirname(filePath), { recursive: true })
    this.stream = fs.createWriteStream(filePath, { flags: 'a' })
  }
  write(obj) {
    this.stream.write(JSON.stringify(obj) + '\n')
  }
  close() {
    return new Promise((r) => this.stream.end(r))
  }
}

// ---------- Random partition picker ----------
export function randomPartition(maxPartitions) {
  return Math.floor(Math.random() * maxPartitions).toString()
}

// ---------- Random ~300-byte payload ----------
const PAYLOAD_FILLER = 'x'.repeat(220) // padding to bring total payload to ~280 bytes
export function makePayload(extras = {}) {
  return {
    ...extras,
    filler: PAYLOAD_FILLER,
    nonce: Math.random().toString(36).slice(2, 14),
  }
}

// ---------- Periodic counter logger ----------
export class CounterLogger {
  constructor(role, instance, intervalMs = 5000) {
    this.role = role
    this.instance = instance
    this.intervalMs = intervalMs
    this.count = 0
    this.lastCount = 0
    this.lastTime = Date.now()
    this.timer = setInterval(() => this.tick(), intervalMs)
  }
  inc(n = 1) {
    this.count += n
  }
  tick() {
    const now = Date.now()
    const dt = (now - this.lastTime) / 1000
    const dc = this.count - this.lastCount
    const rate = dt > 0 ? dc / dt : 0
    process.stderr.write(
      `[${new Date().toISOString()}] [${this.role}-${this.instance}] processed=${this.count} rate=${rate.toFixed(0)}/s\n`,
    )
    this.lastCount = this.count
    this.lastTime = now
  }
  stop() {
    clearInterval(this.timer)
    this.tick()
  }
}
