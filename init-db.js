#!/usr/bin/env node
import { createPool, initDatabase } from './src/database/connection.js';

const init = async () => {
  console.log('Initializing Queen database schema...');
  
  const pool = createPool();
  
  try {
    await initDatabase(pool);
    console.log('✅ Database schema initialized successfully');
  } catch (error) {
    console.error('❌ Failed to initialize database:', error.message);
    process.exit(1);
  } finally {
    await pool.end();
  }
};

init();
