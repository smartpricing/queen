#!/usr/bin/env node
import pg from 'pg';

const { Pool } = pg;

const pool = new Pool({
  user: 'postgres',
  host: 'localhost',
  database: 'postgres',
  password: 'postgres',
  port: 5432,
});

async function checkLeaseState() {
  const result = await pool.query(`
    SELECT 
      pl.partition_id,
      p.name as partition_name,
      q.name as queue_name,
      pl.consumer_group,
      pl.lease_expires_at,
      pl.released_at,
      pl.batch_size,
      pl.acked_count,
      pl.message_batch,
      (pl.released_at IS NULL) as is_active,
      (pl.lease_expires_at > NOW()) as is_not_expired
    FROM queen.partition_leases pl
    JOIN queen.partitions p ON pl.partition_id = p.id
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE q.name = 'test-concurrent-debug'
    ORDER BY pl.created_at DESC
    LIMIT 10
  `);
  
  console.log('\n🔍 Partition Lease State:');
  console.log('='.repeat(120));
  
  if (result.rows.length === 0) {
    console.log('No leases found for queue test-concurrent-debug');
  } else {
    result.rows.forEach((row, idx) => {
      console.log(`\nLease ${idx + 1}:`);
      console.log(`  Queue: ${row.queue_name} | Partition: ${row.partition_name}`);
      console.log(`  Consumer Group: ${row.consumer_group}`);
      console.log(`  Batch Size: ${row.batch_size} | ACKed Count: ${row.acked_count}`);
      console.log(`  Active: ${row.is_active} | Not Expired: ${row.is_not_expired}`);
      console.log(`  Released At: ${row.released_at || 'NULL (lease held)'}`);
      console.log(`  Expires At: ${row.lease_expires_at}`);
      console.log(`  Message Batch: ${row.message_batch ? JSON.stringify(row.message_batch) : 'NULL'}`);
    });
  }
  
  await pool.end();
}

checkLeaseState().catch(console.error);

