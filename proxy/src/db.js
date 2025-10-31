import pg from 'pg';
const { Pool } = pg;

const pool = new Pool({
  host: process.env.PG_HOST || 'localhost',
  port: process.env.PG_PORT || 5432,
  database: process.env.PG_DB || 'postgres',
  user: process.env.PG_USER || 'postgres',
  password: process.env.PG_PASSWORD || 'postgres',
  ssl: process.env.PG_USE_SSL ? { rejectUnauthorized: process.env.PG_SSL_REJECT_UNAUTHORIZED === 'true' } : false,
  max: 2,
  idleTimeoutMillis: 30000,
  connectionTimeoutMillis: 2000,
});

export async function query(text, params) {
  return await pool.query(text, params);
}

export async function initDatabase() {
  await query('CREATE SCHEMA IF NOT EXISTS queen_proxy');
  
  await query(`
    CREATE TABLE IF NOT EXISTS queen_proxy.users (
      id SERIAL PRIMARY KEY,
      username VARCHAR(255) UNIQUE NOT NULL,
      password_hash VARCHAR(255) NOT NULL,
      role VARCHAR(50) NOT NULL CHECK (role IN ('admin', 'read-write', 'read-only')),
      created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    )
  `);
  
  await query('CREATE INDEX IF NOT EXISTS idx_users_username ON queen_proxy.users(username)');
  
  await query(`
    CREATE TABLE IF NOT EXISTS queen_proxy.sessions (
      id SERIAL PRIMARY KEY,
      user_id INTEGER REFERENCES queen_proxy.users(id) ON DELETE CASCADE,
      token_jti VARCHAR(255) UNIQUE NOT NULL,
      expires_at TIMESTAMP NOT NULL,
      created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    )
  `);
  
  await query('CREATE INDEX IF NOT EXISTS idx_sessions_token_jti ON queen_proxy.sessions(token_jti)');
  await query('CREATE INDEX IF NOT EXISTS idx_sessions_user_id ON queen_proxy.sessions(user_id)');
}

export async function getUserByUsername(username) {
  const result = await query(
    'SELECT id, username, password_hash, role FROM queen_proxy.users WHERE username = $1',
    [username]
  );
  return result.rows[0];
}

export async function createUser(username, passwordHash, role) {
  const result = await query(
    'INSERT INTO queen_proxy.users (username, password_hash, role) VALUES ($1, $2, $3) RETURNING id, username, role',
    [username, passwordHash, role]
  );
  return result.rows[0];
}

export default pool;
