#!/usr/bin/env node

/**
 * Migration script to add priority column to queues table
 */

import pg from 'pg';

const pool = new pg.Pool({
  host: process.env.PG_HOST || 'localhost',
  port: process.env.PG_PORT || 5432,
  database: process.env.PG_DB || 'postgres',
  user: process.env.PG_USER || 'postgres',
  password: process.env.PG_PASSWORD || 'postgres'
});

async function migrate() {
  console.log('🔄 Migrating database to add queue priority...');
  
  try {
    // Check if priority column already exists
    const checkColumn = await pool.query(`
      SELECT column_name 
      FROM information_schema.columns 
      WHERE table_schema = 'queen' 
        AND table_name = 'queues' 
        AND column_name = 'priority'
    `);
    
    if (checkColumn.rows.length > 0) {
      console.log('✅ Priority column already exists in queues table');
    } else {
      // Add priority column to queues table
      await pool.query(`
        ALTER TABLE queen.queues 
        ADD COLUMN priority INTEGER DEFAULT 0
      `);
      console.log('✅ Added priority column to queues table');
    }
    
    // Add indexes for priority
    await pool.query(`
      CREATE INDEX IF NOT EXISTS idx_queues_priority ON queen.queues(priority DESC)
    `);
    
    await pool.query(`
      CREATE INDEX IF NOT EXISTS idx_queues_namespace_priority 
      ON queen.queues(namespace, priority DESC) 
      WHERE namespace IS NOT NULL
    `);
    
    await pool.query(`
      CREATE INDEX IF NOT EXISTS idx_queues_task_priority 
      ON queen.queues(task, priority DESC) 
      WHERE task IS NOT NULL
    `);
    
    console.log('✅ Added priority indexes');
    
    // Set some test priorities
    await pool.query(`
      UPDATE queen.queues 
      SET priority = CASE 
        WHEN name = 'test-high-priority' THEN 10
        WHEN name = 'test-medium-priority' THEN 5  
        WHEN name = 'test-low-priority' THEN 1
        ELSE 0
      END
      WHERE name IN ('test-high-priority', 'test-medium-priority', 'test-low-priority')
    `);
    
    await pool.query(`
      UPDATE queen.partitions 
      SET priority = CASE 
        WHEN name = 'high-priority' THEN 10
        WHEN name = 'low-priority' THEN 1
        ELSE 0
      END
      WHERE name IN ('high-priority', 'low-priority', 'default')
    `);
    
    console.log('✅ Set test priorities');
    
    // Verify the changes
    const result = await pool.query(`
      SELECT name, priority, namespace, task 
      FROM queen.queues 
      WHERE name LIKE 'test-%' 
      ORDER BY priority DESC
    `);
    
    console.log('\n📊 Current queue priorities:');
    result.rows.forEach(row => {
      console.log(`  ${row.name}: priority ${row.priority}`);
    });
    
    console.log('\n✅ Migration completed successfully!');
    
  } catch (error) {
    console.error('❌ Migration failed:', error.message);
    console.error('Stack:', error.stack);
  } finally {
    await pool.end();
  }
}

migrate();
