#!/usr/bin/env node

const fs = require('fs');
const path = require('path');

/**
 * Analyze Queen MQ logs to extract POP, QPOP, and ACK statistics
 * Usage: node analyze-logs.js <log-file-1> <log-file-2>
 */

function parseTimestamp(logLine) {
  const match = logLine.match(/\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]/);
  if (match) {
    return new Date(match[1].replace(' ', 'T') + 'Z');
  }
  return null;
}

function analyzeLogFile(filePath) {
  console.log(`\n${'='.repeat(80)}`);
  console.log(`Analyzing: ${path.basename(filePath)}`);
  console.log('='.repeat(80));

  const content = fs.readFileSync(filePath, 'utf-8');
  const lines = content.split('\n').filter(line => line.trim());

  const stats = {
    qpop: {
      total: 0,
      byConsumerGroup: new Map(),
      byQueue: new Map(),
      byWorker: new Map(),
    },
    qpopRegistered: {
      total: 0,
      timestamps: [],
    },
    popFulfilled: {
      total: 0,
      totalMessages: 0,
      byConsumerGroup: new Map(),
      timestamps: [],
      responseTimeMs: [],
    },
    popTimeout: {
      total: 0,
      timestamps: [],
    },
    ack: {
      total: 0,
      immediate: 0,
      timestamps: [],
    },
    push: {
      total: 0,
      totalItems: 0,
      timestamps: [],
      durationMs: [],
    },
    transaction: {
      total: 0,
      timestamps: [],
    },
    backoff: {
      total: 0,
      byGroup: new Map(),
      intervals: [],
    },
  };

  let firstTimestamp = null;
  let lastTimestamp = null;

  for (const line of lines) {
    const timestamp = parseTimestamp(line);
    if (timestamp) {
      if (!firstTimestamp) firstTimestamp = timestamp;
      lastTimestamp = timestamp;
    }

    // QPOP: Initial request
    if (line.includes('QPOP:') && line.includes('batch=')) {
      stats.qpop.total++;
      
      // Extract worker
      const workerMatch = line.match(/\[Worker (\d+)\]/);
      if (workerMatch) {
        const worker = workerMatch[1];
        stats.qpop.byWorker.set(worker, (stats.qpop.byWorker.get(worker) || 0) + 1);
      }

      // Extract queue and consumer group: [queue/partition@consumer]
      const queueMatch = line.match(/\[([\w.-]+)\/([\w*-]+)@([\w.-]+)\]/);
      if (queueMatch) {
        const queue = queueMatch[1];
        const consumer = queueMatch[3];
        
        stats.qpop.byQueue.set(queue, (stats.qpop.byQueue.get(queue) || 0) + 1);
        stats.qpop.byConsumerGroup.set(consumer, (stats.qpop.byConsumerGroup.get(consumer) || 0) + 1);
      }
    }

    // QPOP: Registered poll intention
    if (line.includes('QPOP: Registered poll intention')) {
      stats.qpopRegistered.total++;
      if (timestamp) stats.qpopRegistered.timestamps.push(timestamp);
    }

    // Poll worker fulfilled intention (successful pop)
    if (line.includes('Poll worker') && line.includes('fulfilled intention')) {
      stats.popFulfilled.total++;
      if (timestamp) stats.popFulfilled.timestamps.push(timestamp);

      // Extract message count
      const msgMatch = line.match(/with (\d+) messages/);
      if (msgMatch) {
        stats.popFulfilled.totalMessages += parseInt(msgMatch[1]);
      }

      // Extract consumer group from group key
      const groupMatch = line.match(/\(group '([^']+)'\)/);
      if (groupMatch) {
        const groupKey = groupMatch[1];
        const consumerGroup = groupKey.split(':').pop();
        stats.popFulfilled.byConsumerGroup.set(
          consumerGroup,
          (stats.popFulfilled.byConsumerGroup.get(consumerGroup) || 0) + 1
        );
      }
    }

    // Poll worker TIMEOUT
    if (line.includes('Poll worker') && line.includes('TIMEOUT:')) {
      stats.popTimeout.total++;
      if (timestamp) stats.popTimeout.timestamps.push(timestamp);
    }

    // ACK operations
    if (line.includes('ACK: Executing')) {
      stats.ack.total++;
      if (timestamp) stats.ack.timestamps.push(timestamp);
      if (line.includes('immediate')) {
        stats.ack.immediate++;
      }
    }

    // PUSH operations
    if (line.includes('PUSH: Async push_messages completed')) {
      stats.push.total++;
      if (timestamp) stats.push.timestamps.push(timestamp);

      // Extract duration
      const durationMatch = line.match(/in (\d+)ms/);
      if (durationMatch) {
        stats.push.durationMs.push(parseInt(durationMatch[1]));
      }

      // Extract item count
      const itemsMatch = line.match(/\((\d+) items?\)/);
      if (itemsMatch) {
        stats.push.totalItems += parseInt(itemsMatch[1]);
      }
    }

    // TRANSACTION operations
    if (line.includes('TRANSACTION: Executing')) {
      stats.transaction.total++;
      if (timestamp) stats.transaction.timestamps.push(timestamp);
    }

    // Backoff activation
    if (line.includes('Backoff activated for group')) {
      stats.backoff.total++;

      const groupMatch = line.match(/for group '([^']+)':/);
      const intervalMatch = line.match(/(\d+)ms -> (\d+)ms/);
      
      if (groupMatch) {
        const group = groupMatch[1];
        stats.backoff.byGroup.set(group, (stats.backoff.byGroup.get(group) || 0) + 1);
      }

      if (intervalMatch) {
        stats.backoff.intervals.push({
          from: parseInt(intervalMatch[1]),
          to: parseInt(intervalMatch[2]),
        });
      }
    }
  }

  // Calculate time range and frequencies
  const durationSeconds = lastTimestamp && firstTimestamp 
    ? (lastTimestamp - firstTimestamp) / 1000 
    : 0;

  // Print results
  console.log(`\nTime Range: ${firstTimestamp?.toISOString()} to ${lastTimestamp?.toISOString()}`);
  console.log(`Duration: ${durationSeconds.toFixed(1)}s (${(durationSeconds / 60).toFixed(1)} minutes)\n`);

  console.log('━'.repeat(80));
  console.log('QPOP OPERATIONS (Initial Requests)');
  console.log('━'.repeat(80));
  console.log(`Total QPOP requests: ${stats.qpop.total}`);
  console.log(`Frequency: ${(stats.qpop.total / durationSeconds * 60).toFixed(2)} requests/minute`);
  
  console.log(`\nBy Worker:`);
  for (const [worker, count] of [...stats.qpop.byWorker.entries()].sort()) {
    const pct = (count / stats.qpop.total * 100).toFixed(1);
    console.log(`  Worker ${worker}: ${count} (${pct}%)`);
  }

  console.log(`\nTop 10 Queues:`);
  const sortedQueues = [...stats.qpop.byQueue.entries()].sort((a, b) => b[1] - a[1]);
  for (const [queue, count] of sortedQueues.slice(0, 10)) {
    const pct = (count / stats.qpop.total * 100).toFixed(1);
    console.log(`  ${queue}: ${count} (${pct}%)`);
  }

  console.log(`\nTop 10 Consumer Groups:`);
  const sortedConsumers = [...stats.qpop.byConsumerGroup.entries()].sort((a, b) => b[1] - a[1]);
  for (const [consumer, count] of sortedConsumers.slice(0, 10)) {
    const pct = (count / stats.qpop.total * 100).toFixed(1);
    console.log(`  ${consumer}: ${count} (${pct}%)`);
  }

  console.log(`\n${'━'.repeat(80)}`);
  console.log('POLL OUTCOMES');
  console.log('━'.repeat(80));
  console.log(`Registered poll intentions: ${stats.qpopRegistered.total}`);
  console.log(`Fulfilled (got messages): ${stats.popFulfilled.total}`);
  console.log(`Timed out (no messages): ${stats.popTimeout.total}`);
  console.log(`\nFulfillment rate: ${(stats.popFulfilled.total / stats.qpopRegistered.total * 100).toFixed(1)}%`);
  console.log(`Timeout rate: ${(stats.popTimeout.total / stats.qpopRegistered.total * 100).toFixed(1)}%`);
  console.log(`\nTotal messages delivered: ${stats.popFulfilled.totalMessages}`);
  console.log(`Avg messages per fulfilled poll: ${(stats.popFulfilled.totalMessages / stats.popFulfilled.total).toFixed(2)}`);

  console.log(`\nTop 10 Consumer Groups (by fulfillment):`);
  const sortedFulfilled = [...stats.popFulfilled.byConsumerGroup.entries()].sort((a, b) => b[1] - a[1]);
  for (const [consumer, count] of sortedFulfilled.slice(0, 10)) {
    const pct = (count / stats.popFulfilled.total * 100).toFixed(1);
    console.log(`  ${consumer}: ${count} fulfills (${pct}%)`);
  }

  console.log(`\n${'━'.repeat(80)}`);
  console.log('ACK OPERATIONS');
  console.log('━'.repeat(80));
  console.log(`Total ACKs: ${stats.ack.total}`);
  console.log(`Immediate ACKs: ${stats.ack.immediate} (${(stats.ack.immediate / stats.ack.total * 100).toFixed(1)}%)`);
  console.log(`Frequency: ${(stats.ack.total / durationSeconds * 60).toFixed(2)} ACKs/minute`);

  console.log(`\n${'━'.repeat(80)}`);
  console.log('PUSH OPERATIONS');
  console.log('━'.repeat(80));
  console.log(`Total PUSHes: ${stats.push.total}`);
  console.log(`Total items pushed: ${stats.push.totalItems}`);
  console.log(`Frequency: ${(stats.push.total / durationSeconds * 60).toFixed(2)} pushes/minute`);
  if (stats.push.durationMs.length > 0) {
    const avgDuration = stats.push.durationMs.reduce((a, b) => a + b, 0) / stats.push.durationMs.length;
    const maxDuration = Math.max(...stats.push.durationMs);
    console.log(`Avg push duration: ${avgDuration.toFixed(1)}ms`);
    console.log(`Max push duration: ${maxDuration}ms`);
  }

  console.log(`\n${'━'.repeat(80)}`);
  console.log('TRANSACTION OPERATIONS');
  console.log('━'.repeat(80));
  console.log(`Total TRANSACTIONs: ${stats.transaction.total}`);
  console.log(`Frequency: ${(stats.transaction.total / durationSeconds * 60).toFixed(2)} transactions/minute`);

  console.log(`\n${'━'.repeat(80)}`);
  console.log('BACKOFF BEHAVIOR');
  console.log('━'.repeat(80));
  console.log(`Total backoff activations: ${stats.backoff.total}`);
  console.log(`Unique groups with backoff: ${stats.backoff.byGroup.size}`);

  // Calculate backoff interval distribution
  const intervalCounts = new Map();
  for (const interval of stats.backoff.intervals) {
    const key = `${interval.from}ms → ${interval.to}ms`;
    intervalCounts.set(key, (intervalCounts.get(key) || 0) + 1);
  }

  console.log(`\nBackoff interval transitions (top 10):`);
  const sortedIntervals = [...intervalCounts.entries()].sort((a, b) => b[1] - a[1]);
  for (const [transition, count] of sortedIntervals.slice(0, 10)) {
    const pct = (count / stats.backoff.total * 100).toFixed(1);
    console.log(`  ${transition}: ${count} times (${pct}%)`);
  }

  console.log(`\nTop 10 Groups with most backoff activations:`);
  const sortedBackoff = [...stats.backoff.byGroup.entries()].sort((a, b) => b[1] - a[1]);
  for (const [group, count] of sortedBackoff.slice(0, 10)) {
    console.log(`  ${group}: ${count} backoffs`);
  }

  // Calculate average time between operations
  console.log(`\n${'━'.repeat(80)}`);
  console.log('TIMING ANALYSIS');
  console.log('━'.repeat(80));

  if (stats.qpopRegistered.timestamps.length > 1) {
    const intervals = [];
    for (let i = 1; i < stats.qpopRegistered.timestamps.length; i++) {
      intervals.push((stats.qpopRegistered.timestamps[i] - stats.qpopRegistered.timestamps[i - 1]) / 1000);
    }
    const avgInterval = intervals.reduce((a, b) => a + b, 0) / intervals.length;
    console.log(`Avg time between QPOP registrations: ${avgInterval.toFixed(3)}s`);
  }

  if (stats.popFulfilled.timestamps.length > 1) {
    const intervals = [];
    for (let i = 1; i < stats.popFulfilled.timestamps.length; i++) {
      intervals.push((stats.popFulfilled.timestamps[i] - stats.popFulfilled.timestamps[i - 1]) / 1000);
    }
    const avgInterval = intervals.reduce((a, b) => a + b, 0) / intervals.length;
    console.log(`Avg time between fulfillments: ${avgInterval.toFixed(3)}s`);
  }

  if (stats.ack.timestamps.length > 1) {
    const intervals = [];
    for (let i = 1; i < stats.ack.timestamps.length; i++) {
      intervals.push((stats.ack.timestamps[i] - stats.ack.timestamps[i - 1]) / 1000);
    }
    const avgInterval = intervals.reduce((a, b) => a + b, 0) / intervals.length;
    console.log(`Avg time between ACKs: ${avgInterval.toFixed(3)}s`);
  }

  if (stats.push.timestamps.length > 1) {
    const intervals = [];
    for (let i = 1; i < stats.push.timestamps.length; i++) {
      intervals.push((stats.push.timestamps[i] - stats.push.timestamps[i - 1]) / 1000);
    }
    const avgInterval = intervals.reduce((a, b) => a + b, 0) / intervals.length;
    console.log(`Avg time between PUSHes: ${avgInterval.toFixed(3)}s`);
  }

  // Calculate residence time (QPOP to fulfillment/timeout)
  console.log(`\n${'━'.repeat(80)}`);
  console.log('POLL INTENTION RESIDENCE TIME');
  console.log('━'.repeat(80));
  console.log(`Total poll intentions: ${stats.qpopRegistered.total}`);
  console.log(`Fulfilled: ${stats.popFulfilled.total} (${(stats.popFulfilled.total / stats.qpopRegistered.total * 100).toFixed(1)}%)`);
  console.log(`Timed out: ${stats.popTimeout.total} (${(stats.popTimeout.total / stats.qpopRegistered.total * 100).toFixed(1)}%)`);
  
  const avgResidenceTimeFulfilled = durationSeconds / stats.qpopRegistered.total;
  console.log(`Estimated avg residence time: ${avgResidenceTimeFulfilled.toFixed(2)}s`);

  return stats;
}

function compareStats(stats1, stats2, label1, label2) {
  console.log(`\n\n${'═'.repeat(80)}`);
  console.log('COMPARISON SUMMARY');
  console.log('═'.repeat(80));

  const comparison = [
    ['Metric', label1, label2, 'Ratio'],
    ['─'.repeat(40), '─'.repeat(12), '─'.repeat(12), '─'.repeat(12)],
    ['QPOP requests', stats1.qpop.total, stats2.qpop.total, (stats2.qpop.total / stats1.qpop.total).toFixed(2) + 'x'],
    ['Poll fulfilled', stats1.popFulfilled.total, stats2.popFulfilled.total, (stats2.popFulfilled.total / stats1.popFulfilled.total).toFixed(2) + 'x'],
    ['Poll timeouts', stats1.popTimeout.total, stats2.popTimeout.total, (stats2.popTimeout.total / stats1.popTimeout.total).toFixed(2) + 'x'],
    ['Messages delivered', stats1.popFulfilled.totalMessages, stats2.popFulfilled.totalMessages, (stats2.popFulfilled.totalMessages / stats1.popFulfilled.totalMessages).toFixed(2) + 'x'],
    ['ACK operations', stats1.ack.total, stats2.ack.total, (stats2.ack.total / stats1.ack.total).toFixed(2) + 'x'],
    ['PUSH operations', stats1.push.total, stats2.push.total, (stats2.push.total / stats1.push.total).toFixed(2) + 'x'],
    ['Items pushed', stats1.push.totalItems, stats2.push.totalItems, (stats2.push.totalItems / stats1.push.totalItems).toFixed(2) + 'x'],
    ['Backoff activations', stats1.backoff.total, stats2.backoff.total, (stats2.backoff.total / stats1.backoff.total).toFixed(2) + 'x'],
    ['Unique backoff groups', stats1.backoff.byGroup.size, stats2.backoff.byGroup.size, (stats2.backoff.byGroup.size / stats1.backoff.byGroup.size).toFixed(2) + 'x'],
  ];

  for (const row of comparison) {
    if (row[0].startsWith('─')) {
      console.log(row[0]);
    } else {
      console.log(`${row[0].padEnd(40)} ${String(row[1]).padStart(12)} ${String(row[2]).padStart(12)} ${String(row[3]).padStart(12)}`);
    }
  }

  console.log(`\n${'═'.repeat(80)}`);
  console.log('KEY INSIGHTS');
  console.log('═'.repeat(80));

  const fulfillmentRate1 = (stats1.popFulfilled.total / stats1.qpopRegistered.total * 100).toFixed(1);
  const fulfillmentRate2 = (stats2.popFulfilled.total / stats2.qpopRegistered.total * 100).toFixed(1);
  
  console.log(`\n${label1}:`);
  console.log(`  - Fulfillment rate: ${fulfillmentRate1}%`);
  console.log(`  - Timeout rate: ${(100 - fulfillmentRate1).toFixed(1)}%`);
  console.log(`  - Avg backoff activations per group: ${(stats1.backoff.total / stats1.backoff.byGroup.size).toFixed(1)}`);
  
  console.log(`\n${label2}:`);
  console.log(`  - Fulfillment rate: ${fulfillmentRate2}%`);
  console.log(`  - Timeout rate: ${(100 - fulfillmentRate2).toFixed(1)}%`);
  console.log(`  - Avg backoff activations per group: ${(stats2.backoff.total / stats2.backoff.byGroup.size).toFixed(1)}`);

  console.log(`\nInterpretation:`);
  if (stats1.popFulfilled.total > stats2.popFulfilled.total) {
    console.log(`  → ${label1} fulfills ${(stats1.popFulfilled.total / stats2.popFulfilled.total).toFixed(2)}x more polls`);
    console.log(`  → ${label1} is winning the database lock race more frequently`);
    console.log(`  → This keeps its backoff low, creating a self-reinforcing winner pattern`);
  } else {
    console.log(`  → ${label2} fulfills ${(stats2.popFulfilled.total / stats1.popFulfilled.total).toFixed(2)}x more polls`);
    console.log(`  → ${label2} is winning the database lock race more frequently`);
    console.log(`  → This keeps its backoff low, creating a self-reinforcing winner pattern`);
  }

  if (stats1.backoff.total > stats2.backoff.total) {
    console.log(`  → ${label1} has ${(stats1.backoff.total / stats2.backoff.total).toFixed(2)}x more backoff activations`);
    console.log(`  → More backoff = lower query frequency = lower CPU usage = fewer wins`);
  } else {
    console.log(`  → ${label2} has ${(stats2.backoff.total / stats1.backoff.total).toFixed(2)}x more backoff activations`);
    console.log(`  → More backoff = lower query frequency = lower CPU usage = fewer wins`);
  }
}

function main() {
  const args = process.argv.slice(2);

  if (args.length !== 2) {
    console.error('Usage: node analyze-logs.js <log-file-1> <log-file-2>');
    console.error('');
    console.error('Example:');
    console.error('  node analyze-logs.js mq-0.log mq-1.log');
    process.exit(1);
  }

  const [file1, file2] = args;

  // Check if files exist
  if (!fs.existsSync(file1)) {
    console.error(`Error: File not found: ${file1}`);
    process.exit(1);
  }

  if (!fs.existsSync(file2)) {
    console.error(`Error: File not found: ${file2}`);
    process.exit(1);
  }

  console.log('Queen MQ Log Analyzer');
  console.log('═'.repeat(80));

  const stats1 = analyzeLogFile(file1);
  const stats2 = analyzeLogFile(file2);

  compareStats(stats1, stats2, path.basename(file1), path.basename(file2));

  console.log('\n');
}

main();

