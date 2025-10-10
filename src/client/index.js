// Legacy exports (for backward compatibility)
export { createQueenClient, createConsumer, createProducer } from './queenClient.js';
export { LoadBalancingStrategy } from './utils/loadBalancer.js';

// New minimalist interface
export { Queen, default as default } from './client.js';