// Metrics collection and reporting utilities
// Uses HDR Histogram for accurate latency percentiles

import hdr from 'hdr-histogram-js';

export class BenchmarkMetrics {
  constructor(name) {
    this.name = name;
    this.histogram = hdr.build({
      lowestDiscernibleValue: 1,           // 1 microsecond
      highestTrackableValue: 60000000,     // 60 seconds in microseconds
      numberOfSignificantValueDigits: 3,
    });
    this.startTime = null;
    this.endTime = null;
    this.messagesSent = 0;
    this.messagesReceived = 0;
    this.errors = 0;
    this.bytesSent = 0;
    this.bytesReceived = 0;
  }
  
  start() {
    this.startTime = process.hrtime.bigint();
    return this;
  }
  
  stop() {
    this.endTime = process.hrtime.bigint();
    return this;
  }
  
  recordLatency(startNs) {
    const latencyNs = Number(process.hrtime.bigint() - startNs);
    const latencyUs = Math.round(latencyNs / 1000);  // Convert to microseconds
    this.histogram.recordValue(latencyUs);
  }
  
  recordLatencyMs(latencyMs) {
    const latencyUs = Math.round(latencyMs * 1000);
    this.histogram.recordValue(latencyUs);
  }
  
  incrementSent(count = 1, bytes = 0) {
    this.messagesSent += count;
    this.bytesSent += bytes;
  }
  
  incrementReceived(count = 1, bytes = 0) {
    this.messagesReceived += count;
    this.bytesReceived += bytes;
  }
  
  incrementErrors() {
    this.errors++;
  }
  
  getDurationMs() {
    if (!this.startTime || !this.endTime) return 0;
    return Number(this.endTime - this.startTime) / 1_000_000;
  }
  
  getThroughput() {
    const durationSec = this.getDurationMs() / 1000;
    const messages = this.messagesSent || this.messagesReceived;
    const bytes = this.bytesSent || this.bytesReceived;
    return {
      messagesPerSecond: durationSec > 0 ? messages / durationSec : 0,
      bytesPerSecond: durationSec > 0 ? bytes / durationSec : 0,
    };
  }
  
  getLatencyStats() {
    return {
      min: this.histogram.minNonZeroValue / 1000,      // ms
      max: this.histogram.maxValue / 1000,             // ms
      mean: this.histogram.mean / 1000,                // ms
      stdDev: this.histogram.stdDeviation / 1000,      // ms
      p50: this.histogram.getValueAtPercentile(50) / 1000,
      p75: this.histogram.getValueAtPercentile(75) / 1000,
      p90: this.histogram.getValueAtPercentile(90) / 1000,
      p95: this.histogram.getValueAtPercentile(95) / 1000,
      p99: this.histogram.getValueAtPercentile(99) / 1000,
      p999: this.histogram.getValueAtPercentile(99.9) / 1000,
    };
  }
  
  getReport() {
    const throughput = this.getThroughput();
    const latency = this.getLatencyStats();
    
    return {
      name: this.name,
      duration: {
        ms: this.getDurationMs(),
        formatted: `${(this.getDurationMs() / 1000).toFixed(2)}s`,
      },
      messages: {
        sent: this.messagesSent,
        received: this.messagesReceived,
        errors: this.errors,
      },
      throughput: {
        messagesPerSecond: Math.round(throughput.messagesPerSecond),
        mbPerSecond: (throughput.bytesPerSecond / 1024 / 1024).toFixed(2),
      },
      latency: {
        min: latency.min.toFixed(3),
        max: latency.max.toFixed(3),
        mean: latency.mean.toFixed(3),
        p50: latency.p50.toFixed(3),
        p75: latency.p75.toFixed(3),
        p90: latency.p90.toFixed(3),
        p95: latency.p95.toFixed(3),
        p99: latency.p99.toFixed(3),
        p999: latency.p999.toFixed(3),
        unit: 'ms',
      },
    };
  }
  
  printReport() {
    const report = this.getReport();
    
    console.log(`\n${'='.repeat(60)}`);
    console.log(`  ${this.name} Benchmark Results`);
    console.log(`${'='.repeat(60)}`);
    console.log(`  Duration: ${report.duration.formatted}`);
    console.log(`  Messages sent: ${report.messages.sent.toLocaleString()}`);
    console.log(`  Messages received: ${report.messages.received.toLocaleString()}`);
    console.log(`  Errors: ${report.messages.errors}`);
    console.log(`  Throughput: ${report.throughput.messagesPerSecond.toLocaleString()} msg/s`);
    console.log(`  Throughput: ${report.throughput.mbPerSecond} MB/s`);
    console.log(`\n  Latency (ms):`);
    console.log(`    Min:  ${report.latency.min}`);
    console.log(`    Mean: ${report.latency.mean}`);
    console.log(`    p50:  ${report.latency.p50}`);
    console.log(`    p75:  ${report.latency.p75}`);
    console.log(`    p90:  ${report.latency.p90}`);
    console.log(`    p95:  ${report.latency.p95}`);
    console.log(`    p99:  ${report.latency.p99}`);
    console.log(`    p99.9:${report.latency.p999}`);
    console.log(`    Max:  ${report.latency.max}`);
    console.log(`${'='.repeat(60)}\n`);
    
    return report;
  }
  
  toJSON() {
    return this.getReport();
  }
}

export async function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

export function formatNumber(n) {
  return n.toLocaleString();
}
