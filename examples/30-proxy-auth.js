import { Queen } from '../client-js/client-v2/index.js';

const queen = new Queen({
  url: 'https://qschat-stage.smartness.com',
  bearerToken: 'YOUR_BEARER_TOKEN_HERE'
});

const dlqMessages = await queen.queue('smartchat.router.outgoing').dlq().get();
console.log('DLQ Messages:', dlqMessages);

// ===========================
// Admin API Tests
// ===========================

// Helper to test each endpoint safely
async function test(name, fn) {
  try {
    const result = await fn();
    console.log(`✅ ${name}:`, result);
    return result;
  } catch (err) {
    console.log(`❌ ${name}: ${err.message}`);
    return null;
  }
}

console.log('\n=== RESOURCES API ===');
await test('getOverview', () => queen.admin.getOverview());
await test('getNamespaces', () => queen.admin.getNamespaces());
await test('getTasks', () => queen.admin.getTasks());

console.log('\n=== QUEUES API ===');
await test('listQueues', () => queen.admin.listQueues({ limit: 10 }));
await test('getPartitions', () => queen.admin.getPartitions({ limit: 10 }));
// await test('getQueue', () => queen.admin.getQueue('smartchat.router.outgoing'));
// await test('clearQueue', () => queen.admin.clearQueue('test-queue')); // DESTRUCTIVE!

console.log('\n=== MESSAGES API ===');
await test('listMessages', () => queen.admin.listMessages({ limit: 10 }));
// await test('getMessage', () => queen.admin.getMessage('partition-id', 'transaction-id'));
// await test('retryMessage', () => queen.admin.retryMessage('partition-id', 'transaction-id'));
// await test('moveMessageToDLQ', () => queen.admin.moveMessageToDLQ('partition-id', 'transaction-id'));
// await test('deleteMessage', () => queen.admin.deleteMessage('partition-id', 'transaction-id')); // DESTRUCTIVE!

console.log('\n=== TRACES API ===');
await test('getTraceNames', () => queen.admin.getTraceNames({ limit: 10 }));
// await test('getTracesByName', () => queen.admin.getTracesByName('my-trace-name', { limit: 10 }));
// await test('getTracesForMessage', () => queen.admin.getTracesForMessage('partition-id', 'transaction-id'));

console.log('\n=== ANALYTICS/STATUS API ===');
await test('getStatus', () => queen.admin.getStatus());
await test('getQueueStats', () => queen.admin.getQueueStats({ limit: 10 }));
await test('getAnalytics', () => queen.admin.getAnalytics());
// await test('getQueueDetail', () => queen.admin.getQueueDetail('smartchat.router.outgoing'));

console.log('\n=== CONSUMER GROUPS API ===');
await test('listConsumerGroups', () => queen.admin.listConsumerGroups());
await test('getLaggingConsumers', () => queen.admin.getLaggingConsumers(60));
await test('refreshConsumerStats', () => queen.admin.refreshConsumerStats());
// await test('getConsumerGroup', () => queen.admin.getConsumerGroup('my-consumer-group'));
// await test('seekConsumerGroup', () => queen.admin.seekConsumerGroup('my-group', 'my-queue', { timestamp: '2024-01-01T00:00:00Z' }));
// await test('deleteConsumerGroupForQueue', () => queen.admin.deleteConsumerGroupForQueue('my-group', 'my-queue')); // DESTRUCTIVE!

console.log('\n=== SYSTEM API ===');
await test('health', () => queen.admin.health());
// await test('metrics', () => queen.admin.metrics()); // Returns raw text (verbose)
await test('getMaintenanceMode', () => queen.admin.getMaintenanceMode());
await test('getPopMaintenanceMode', () => queen.admin.getPopMaintenanceMode());
await test('getSystemMetrics', () => queen.admin.getSystemMetrics());
await test('getWorkerMetrics', () => queen.admin.getWorkerMetrics());
await test('getPostgresStats', () => queen.admin.getPostgresStats());
// await test('setMaintenanceMode', () => queen.admin.setMaintenanceMode(false)); // CAREFUL!
// await test('setPopMaintenanceMode', () => queen.admin.setPopMaintenanceMode(false)); // CAREFUL!

console.log('\n=== ALL TESTS COMPLETE ===');

await queen.close();
