// Benchmark Configuration
// All systems use the same parameters for fair comparison

export const config = {
  // Number of partitions/topics to create
  // Note: Queen handles 10k, but Kafka/Pulsar struggle, so using 5k for comparison
  partitionCount: 10000,
  
  // Messages per partition during warmup
  warmupMessagesPerPartition: 10,
  
  // Messages per partition during benchmark
  benchmarkMessagesPerPartition: 100,
  
  // Message payload size in bytes
  messageSize: 256,
  
  // Producer settings
  producer: {
    // linger.ms = 0: No client-side batching (send immediately)
    lingerMs: 0,
    // Number of concurrent producers
    concurrency: 10,
    // Batch size for Queen (messages per API call)
    queenBatchSize: 1,  // 1 = no batching, fair comparison
  },
  
  // Consumer settings
  consumer: {
    // Number of concurrent consumers
    concurrency: 10,
    // Batch size for consuming (100 for faster tests)
    batchSize: 100,
    // Auto-commit/auto-ack
    autoAck: false
  },
  
  // Durability settings (must be equivalent across systems)
  durability: {
    // Kafka: acks=all
    kafkaAcks: -1,  // -1 = all
    // Pulsar: sync writes
    pulsarSyncWrites: true,
    // Queen/PostgreSQL: synchronous_commit=on (configured in docker-compose)
  },
  
  // Test scenarios
  scenarios: [
    {
      name: 'latency-focused',
      description: 'Single message, measure p50/p95/p99 latency',
      messagesPerSecond: 100,
      duration: 60,  // seconds
    },
    {
      name: 'throughput-focused',
      description: 'Maximum throughput, unbatched',
      messagesPerSecond: null,  // unlimited
      duration: 60,
    },
    {
      name: 'partition-fanout',
      description: 'Publish to 10000 different partitions',
      messagesPerSecond: 1000,
      duration: 60,
    },
  ],
  
  // Endpoints
  endpoints: {
    queen: 'http://localhost:6632',
    kafka: 'localhost:9092',
    pulsar: 'pulsar://localhost:6650',
    pulsarAdmin: 'http://localhost:8080',
  },
  
  // Metrics collection
  metrics: {
    // HDR Histogram settings
    histogramLowestValue: 1,        // 1 microsecond
    histogramHighestValue: 60000000, // 60 seconds in microseconds
    histogramSignificantDigits: 3,
    // Percentiles to report
    percentiles: [50, 75, 90, 95, 99, 99.9],
  },
};

export function generatePayload(size = config.messageSize) {
  return {
    timestamp: Date.now(),
    data: 'x'.repeat(size - 50), // Account for timestamp and JSON overhead
  };
}

export function getPartitionName(index) {
  return `partition-${String(index).padStart(5, '0')}`;
}
