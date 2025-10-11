import pg from 'pg';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const { Pool } = pg;
const __dirname = path.dirname(fileURLToPath(import.meta.url));

// Create connection pool
export const createPool = () => {
  const poolConfig = {
    user: process.env.PG_USER || 'postgres',
    host: process.env.PG_HOST || 'localhost',
    database: process.env.PG_DB || 'postgres',
    password: process.env.PG_PASSWORD || 'postgres',
    port: process.env.PG_PORT || 5432,
    max: process.env.DB_POOL_SIZE || 20,
    idleTimeoutMillis: 30000,
    connectionTimeoutMillis: 2000,
  };

  // Add SSL configuration if enabled
  if (process.env.PG_USE_SSL === 'true') {
    poolConfig.ssl = {
      rejectUnauthorized: process.env.PG_SSL_REJECT_UNAUTHORIZED !== 'false'
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
      await client.query('SET LOCAL statement_timeout = 30000'); // 30 seconds
      await client.query('SET LOCAL lock_timeout = 5000'); // 5 seconds
      
      const result = await callback(client);
      await client.query('COMMIT');
      return result;
    } catch (error) {
      await client.query('ROLLBACK');
      
      // Check for serialization failure or deadlock
      if ((error.code === '40001' || error.code === '40P01') && attempt < maxAttempts) {
        // Exponential backoff: 100ms, 200ms, 400ms
        const delay = Math.min(100 * Math.pow(2, attempt - 1), 1000);
        await new Promise(resolve => setTimeout(resolve, delay));
        attempt++;
        console.log(`Transaction retry attempt ${attempt}/${maxAttempts} after ${error.code}`);
        continue;
      }
      
      throw error;
    } finally {
      client.release();
    }
  }
  
  throw new Error('Max transaction retry attempts exceeded');
};
