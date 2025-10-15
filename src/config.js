/**
 * Centralized Configuration for Queen Message Queue System
 * 
 * All configuration values can be overridden using environment variables.
 * This file provides default values and documentation for all settings.
 */
import os from 'os';

// Server Configuration
export const SERVER = {
  PORT: parseInt(process.env.PORT) || 6632,
  HOST: process.env.HOST || '0.0.0.0',
  WORKER_ID: process.env.WORKER_ID || `worker-${os.hostname()}-${process.pid}`,
  APPLICATION_NAME: process.env.APP_NAME || 'queen-mq'
};

// Database Configuration
export const DATABASE = {
  // Connection settings
  USER: process.env.PG_USER || 'postgres',
  HOST: process.env.PG_HOST || 'localhost',
  DATABASE: process.env.PG_DB || 'postgres',
  PASSWORD: process.env.PG_PASSWORD || 'postgres',
  PORT: parseInt(process.env.PG_PORT) || 5432,
  
  // SSL configuration
  USE_SSL: process.env.PG_USE_SSL === 'true',
  SSL_REJECT_UNAUTHORIZED: process.env.PG_SSL_REJECT_UNAUTHORIZED !== 'false',
  
  // Pool configuration
  POOL_SIZE: parseInt(process.env.DB_POOL_SIZE) || 150,
  IDLE_TIMEOUT: parseInt(process.env.DB_IDLE_TIMEOUT) || 30000, // 30 seconds
  CONNECTION_TIMEOUT: parseInt(process.env.DB_CONNECTION_TIMEOUT) || 2000, // 2 seconds
  STATEMENT_TIMEOUT: parseInt(process.env.DB_STATEMENT_TIMEOUT) || 30000, // 30 seconds
  QUERY_TIMEOUT: parseInt(process.env.DB_QUERY_TIMEOUT) || 30000, // 30 seconds
  LOCK_TIMEOUT: parseInt(process.env.DB_LOCK_TIMEOUT) || 10000, // 10 seconds
  
  // Pool manager settings
  MAX_RETRIES: parseInt(process.env.DB_MAX_RETRIES) || 3
};

// Queue Processing Configuration
export const QUEUE = {
  // Pop operation defaults
  DEFAULT_TIMEOUT: parseInt(process.env.DEFAULT_TIMEOUT) || 30000, // 30 seconds
  MAX_TIMEOUT: parseInt(process.env.MAX_TIMEOUT) || 60000, // 60 seconds
  DEFAULT_BATCH_SIZE: parseInt(process.env.DEFAULT_BATCH_SIZE) || 1,
  BATCH_INSERT_SIZE: parseInt(process.env.BATCH_INSERT_SIZE) || 1000,
  
  // Long polling
  POLL_INTERVAL: parseInt(process.env.QUEUE_POLL_INTERVAL) || 100, // 100ms - initial poll interval
  POLL_INTERVAL_FILTERED: parseInt(process.env.QUEUE_POLL_INTERVAL_FILTERED) || 50, // 50ms for better distribution across consumers
  MAX_POLL_INTERVAL: parseInt(process.env.QUEUE_MAX_POLL_INTERVAL) || 2000, // 2000ms - max poll interval after backoff
  BACKOFF_THRESHOLD: parseInt(process.env.QUEUE_BACKOFF_THRESHOLD) || 5, // Number of empty polls before backoff starts
  BACKOFF_MULTIPLIER: parseFloat(process.env.QUEUE_BACKOFF_MULTIPLIER) || 2, // Exponential backoff multiplier
  
  // Partition selection for filtered pops (namespace/task)
  MAX_PARTITION_CANDIDATES: parseInt(process.env.MAX_PARTITION_CANDIDATES) || 100, // Number of candidate partitions to fetch for lease acquisition
  
  // Queue defaults
  DEFAULT_LEASE_TIME: parseInt(process.env.DEFAULT_LEASE_TIME) || 300, // 5 minutes
  DEFAULT_RETRY_LIMIT: parseInt(process.env.DEFAULT_RETRY_LIMIT) || 3,
  DEFAULT_RETRY_DELAY: parseInt(process.env.DEFAULT_RETRY_DELAY) || 1000, // 1 second
  DEFAULT_MAX_SIZE: parseInt(process.env.DEFAULT_MAX_SIZE) || 10000,
  DEFAULT_TTL: parseInt(process.env.DEFAULT_TTL) || 3600, // 1 hour
  DEFAULT_PRIORITY: parseInt(process.env.DEFAULT_PRIORITY) || 0,
  DEFAULT_DELAYED_PROCESSING: parseInt(process.env.DEFAULT_DELAYED_PROCESSING) || 0,
  DEFAULT_WINDOW_BUFFER: parseInt(process.env.DEFAULT_WINDOW_BUFFER) || 0,
  
  // Dead Letter Queue
  DEFAULT_DLQ_ENABLED: process.env.DEFAULT_DLQ_ENABLED === 'true' || false,
  DEFAULT_DLQ_AFTER_MAX_RETRIES: process.env.DEFAULT_DLQ_AFTER_MAX_RETRIES === 'true' || false,
  
  // Retention defaults
  DEFAULT_RETENTION_SECONDS: parseInt(process.env.DEFAULT_RETENTION_SECONDS) || 0,
  DEFAULT_COMPLETED_RETENTION_SECONDS: parseInt(process.env.DEFAULT_COMPLETED_RETENTION_SECONDS) || 0,
  DEFAULT_RETENTION_ENABLED: process.env.DEFAULT_RETENTION_ENABLED === 'true' || false,
  
  // Eviction
  DEFAULT_MAX_WAIT_TIME_SECONDS: parseInt(process.env.DEFAULT_MAX_WAIT_TIME_SECONDS) || 0
};

// System Events Configuration
export const SYSTEM_EVENTS = {
  // Enable/disable system event propagation
  ENABLED: process.env.QUEEN_SYSTEM_EVENTS_ENABLED === 'true' || false,
  
  // Batching window for event publishing (milliseconds)
  BATCH_MS: parseInt(process.env.QUEEN_SYSTEM_EVENTS_BATCH_MS) || 10,
  
  // Timeout for startup synchronization (milliseconds)
  SYNC_TIMEOUT: parseInt(process.env.QUEEN_SYSTEM_EVENTS_SYNC_TIMEOUT) || 30000
};

// Background Jobs Configuration
export const JOBS = {
  // Lease reclamation
  LEASE_RECLAIM_INTERVAL: parseInt(process.env.LEASE_RECLAIM_INTERVAL) || 5000, // 5 seconds
  
  // Retention service
  RETENTION_INTERVAL: parseInt(process.env.RETENTION_INTERVAL) || 300000, // 5 minutes
  RETENTION_BATCH_SIZE: parseInt(process.env.RETENTION_BATCH_SIZE) || 1000,
  PARTITION_CLEANUP_DAYS: parseInt(process.env.PARTITION_CLEANUP_DAYS) || 7,
  
  // Metrics retention (messages_consumed table)
  METRICS_RETENTION_DAYS: parseInt(process.env.METRICS_RETENTION_DAYS) || 90, // Keep 90 days of metrics by default
  
  // Eviction service
  EVICTION_INTERVAL: parseInt(process.env.EVICTION_INTERVAL) || 60000, // 1 minute
  EVICTION_BATCH_SIZE: parseInt(process.env.EVICTION_BATCH_SIZE) || 1000,
  
  // WebSocket updates
  QUEUE_DEPTH_UPDATE_INTERVAL: parseInt(process.env.QUEUE_DEPTH_UPDATE_INTERVAL) || 5000, // 5 seconds
  SYSTEM_STATS_UPDATE_INTERVAL: parseInt(process.env.SYSTEM_STATS_UPDATE_INTERVAL) || 10000 // 10 seconds
};

// WebSocket Configuration
export const WEBSOCKET = {
  COMPRESSION: parseInt(process.env.WS_COMPRESSION) || 0,
  MAX_PAYLOAD_LENGTH: parseInt(process.env.WS_MAX_PAYLOAD_LENGTH) || 16384, // 16KB
  IDLE_TIMEOUT: parseInt(process.env.WS_IDLE_TIMEOUT) || 60, // 60 seconds
  MAX_CONNECTIONS: parseInt(process.env.WS_MAX_CONNECTIONS) || 1000,
  HEARTBEAT_INTERVAL: parseInt(process.env.WS_HEARTBEAT_INTERVAL) || 30000 // 30 seconds
};

// Encryption Configuration
export const ENCRYPTION = {
  KEY_ENV_VAR: 'QUEEN_ENCRYPTION_KEY',
  ALGORITHM: 'aes-256-gcm',
  KEY_LENGTH: 32, // bytes
  IV_LENGTH: 16 // bytes
};

// Client SDK Configuration
export const CLIENT = {
  DEFAULT_BASE_URL: process.env.QUEEN_BASE_URL || 'http://localhost:6632',
  DEFAULT_RETRY_ATTEMPTS: parseInt(process.env.CLIENT_RETRY_ATTEMPTS) || 3,
  DEFAULT_RETRY_DELAY: parseInt(process.env.CLIENT_RETRY_DELAY) || 1000, // 1 second
  DEFAULT_RETRY_BACKOFF: parseFloat(process.env.CLIENT_RETRY_BACKOFF) || 2,
  CONNECTION_POOL_SIZE: parseInt(process.env.CLIENT_POOL_SIZE) || 10,
  REQUEST_TIMEOUT: parseInt(process.env.CLIENT_REQUEST_TIMEOUT) || 30000 // 30 seconds
};

// API Configuration
export const API = {
  MAX_BODY_SIZE: parseInt(process.env.MAX_BODY_SIZE) || 100 * 1024 * 1024, // 100MB max body size
  // Pagination
  DEFAULT_LIMIT: parseInt(process.env.API_DEFAULT_LIMIT) || 100,
  MAX_LIMIT: parseInt(process.env.API_MAX_LIMIT) || 1000,
  DEFAULT_OFFSET: parseInt(process.env.API_DEFAULT_OFFSET) || 0,
  
  // CORS
  CORS_MAX_AGE: parseInt(process.env.CORS_MAX_AGE) || 86400, // 24 hours
  CORS_ALLOWED_ORIGINS: process.env.CORS_ALLOWED_ORIGINS || '*',
  CORS_ALLOWED_METHODS: process.env.CORS_ALLOWED_METHODS || 'GET, POST, PUT, DELETE, OPTIONS',
  CORS_ALLOWED_HEADERS: process.env.CORS_ALLOWED_HEADERS || 'Content-Type, Authorization'
};

// Analytics Configuration
export const ANALYTICS = {
  // Processing time calculations
  RECENT_COMPLETION_HOURS: parseInt(process.env.ANALYTICS_RECENT_HOURS) || 24,
  MIN_COMPLETED_FOR_STATS: parseInt(process.env.ANALYTICS_MIN_COMPLETED) || 5,
  
  // Time windows
  RECENT_MESSAGE_WINDOW: parseInt(process.env.RECENT_MESSAGE_WINDOW) || 60, // 1 minute in seconds
  RELATED_MESSAGE_WINDOW: parseInt(process.env.RELATED_MESSAGE_WINDOW) || 3600, // 1 hour in seconds
  MAX_RELATED_MESSAGES: parseInt(process.env.MAX_RELATED_MESSAGES) || 10
};

// Performance Monitoring
export const MONITORING = {
  ENABLE_REQUEST_COUNTING: process.env.ENABLE_REQUEST_COUNTING !== 'false',
  ENABLE_MESSAGE_COUNTING: process.env.ENABLE_MESSAGE_COUNTING !== 'false',
  METRICS_ENDPOINT_ENABLED: process.env.METRICS_ENDPOINT_ENABLED !== 'false',
  HEALTH_CHECK_ENABLED: process.env.HEALTH_CHECK_ENABLED !== 'false'
};

// Logging Configuration
export const LOGGING = {
  ENABLE_LOGGING: process.env.ENABLE_LOGGING !== 'false',
  LOG_LEVEL: process.env.LOG_LEVEL || 'info',
  LOG_FORMAT: process.env.LOG_FORMAT || 'json',
  LOG_TIMESTAMP: process.env.LOG_TIMESTAMP !== 'false'
};

// HTTP Response Status Codes
export const HTTP_STATUS = {
  OK: 200,
  CREATED: 201,
  NO_CONTENT: 204,
  BAD_REQUEST: 400,
  NOT_FOUND: 404,
  INTERNAL_SERVER_ERROR: 500,
  SERVICE_UNAVAILABLE: 503
};

// Export a function to get the complete configuration
export const getConfig = () => ({
  server: SERVER,
  database: DATABASE,
  queue: QUEUE,
  jobs: JOBS,
  websocket: WEBSOCKET,
  encryption: ENCRYPTION,
  client: CLIENT,
  api: API,
  analytics: ANALYTICS,
  monitoring: MONITORING,
  logging: LOGGING,
  httpStatus: HTTP_STATUS
});

// Export default configuration object
export default {
  SERVER,
  DATABASE,
  QUEUE,
  SYSTEM_EVENTS,
  JOBS,
  WEBSOCKET,
  ENCRYPTION,
  CLIENT,
  API,
  ANALYTICS,
  MONITORING,
  LOGGING,
  HTTP_STATUS
};
