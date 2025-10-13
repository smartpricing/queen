import pg from 'pg';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import config from '../config.js';

const { Pool } = pg;
const __dirname = path.dirname(fileURLToPath(import.meta.url));

/**
 * Create a PostgreSQL connection pool with proper configuration
 * This is the single source of truth for pool creation across the application
 */
export const createPool = () => {
  const poolConfig = {
    user: config.DATABASE.USER,
    host: config.DATABASE.HOST,
    database: config.DATABASE.DATABASE,
    password: config.DATABASE.PASSWORD,
    port: config.DATABASE.PORT,
    max: config.DATABASE.POOL_SIZE,
    idleTimeoutMillis: config.DATABASE.IDLE_TIMEOUT,
    connectionTimeoutMillis: config.DATABASE.CONNECTION_TIMEOUT,
    // Note: statement_timeout and query_timeout cannot be set as startup parameters
    // They should be set per-query or per-transaction using SET commands
    application_name: config.SERVER.APPLICATION_NAME,
    // PERFORMANCE NOTE: TCP_NODELAY for latency reduction
    // The node-postgres driver enables TCP_NODELAY by default on sockets
    // For server-side: Edit postgresql.conf and add: tcp_nodelay = on
    // Reference: https://stackoverflow.com/questions/60634455/how-does-one-configure-tcp-nodelay-for-libpq-and-postgres-server
    keepAlive: true,
    keepAliveInitialDelayMillis: 10000
  };

  // Add SSL configuration if enabled
  if (config.DATABASE.USE_SSL) {
    poolConfig.ssl = {
      rejectUnauthorized: config.DATABASE.SSL_REJECT_UNAUTHORIZED
    };
  }

  return new Pool(poolConfig);
};

// Initialize database schema
export const initDatabase = async (pool) => {
  try {
    // Check if the queen schema already exists
    const schemaCheck = await pool.query(`
      SELECT schema_name 
      FROM information_schema.schemata 
      WHERE schema_name = 'queen'
    `);
    
    if (schemaCheck.rows.length > 0) {
      console.log('Database schema already exists, skipping initialization');
      return;
    }
    
    // Schema doesn't exist, create it
    const schemaPath = path.join(__dirname, 'schema-v2.sql');
    const schema = fs.readFileSync(schemaPath, 'utf8');
    
    await pool.query(schema);
    console.log('Database schema V2 initialized');
  } catch (error) {
    console.error('Failed to initialize database schema:', error);
    throw error;
  }
};

// Valid transaction isolation levels
const VALID_ISOLATION_LEVELS = [
  'READ COMMITTED',
  'REPEATABLE READ', 
  'SERIALIZABLE'
];

// Helper for transactions with retry logic
export const withTransaction = async (pool, callback, isolationLevel = 'READ COMMITTED') => {
  // Validate isolation level
  if (isolationLevel && !VALID_ISOLATION_LEVELS.includes(isolationLevel)) {
    throw new Error(`Invalid isolation level: ${isolationLevel}`);
  }
  
  const maxAttempts = 3;
  let attempt = 1;
  
  while (attempt <= maxAttempts) {
    const client = await pool.connect();
    try {
      await client.query('BEGIN');
      
      if (isolationLevel) {
        await client.query(`SET TRANSACTION ISOLATION LEVEL ${isolationLevel}`);
      }
      
      // Set timeouts to prevent long blocks
      await client.query(`SET LOCAL statement_timeout = ${config.DATABASE.STATEMENT_TIMEOUT}`);
      await client.query(`SET LOCAL lock_timeout = ${config.DATABASE.LOCK_TIMEOUT}`);
      
      const result = await callback(client);
      await client.query('COMMIT');
      return result;
    } catch (error) {
      await client.query('ROLLBACK');
      
      // Check for retriable errors:
      // 40001: serialization_failure
      // 40P01: deadlock_detected
      // 55P03: lock_not_available (lock timeout)
      // 25P02: in_failed_sql_transaction (transaction already aborted - retry whole transaction)
      if ((error.code === '40001' || error.code === '40P01' || error.code === '55P03' || error.code === '25P02') && attempt < maxAttempts) {
        // Exponential backoff: 100ms, 200ms, 400ms
        const delay = Math.min(100 * Math.pow(2, attempt - 1), 1000);
        await new Promise(resolve => setTimeout(resolve, delay));
        attempt++;
        console.log(`Transaction retry attempt ${attempt}/${maxAttempts} after ${error.code} (${error.message})`);
        continue;
      }
      
      throw error;
    } finally {
      client.release();
    }
  }
  
  throw new Error('Max transaction retry attempts exceeded');
};
