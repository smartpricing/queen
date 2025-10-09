import pg from 'pg';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const { Pool } = pg;
const __dirname = path.dirname(fileURLToPath(import.meta.url));

// Create connection pool
export const createPool = () => {
  return new Pool({
    user: process.env.PG_USER || 'postgres',
    host: process.env.PG_HOST || 'localhost',
    database: process.env.PG_DB || 'postgres',
    password: process.env.PG_PASSWORD || 'postgres',
    port: process.env.PG_PORT || 5432,
    max: process.env.DB_POOL_SIZE || 20,
    idleTimeoutMillis: 30000,
    connectionTimeoutMillis: 2000,
  });
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

// Helper for transactions
export const withTransaction = async (pool, callback) => {
  const client = await pool.connect();
  try {
    await client.query('BEGIN');
    const result = await callback(client);
    await client.query('COMMIT');
    return result;
  } catch (error) {
    await client.query('ROLLBACK');
    throw error;
  } finally {
    client.release();
  }
};
