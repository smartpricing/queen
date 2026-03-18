// Benchmark Configuration
// All systems use the same parameters for fair comparison

export const config = {
  // Number of partitions to create
  partitionCount: 100,
  
  // Message payload size in bytes
  messageSize: 256,
  
  // Test duration in seconds
  duration: 60,
  
  // Producer settings
  producer: {
    concurrency: 1,
  },
  
  // Consumer settings
  consumer: {
    concurrency: 1,
    batchSize: 100,
  },
  
  // Endpoints
  endpoints: {
    queen: 'http://localhost:6632',
    kafka: 'localhost:9092',
    pulsar: 'pulsar://localhost:6650',
    pulsarAdmin: 'http://localhost:8080',
  },
};
