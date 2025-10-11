/**
 * Shared test utilities for Queen test suite
 */

import pg from 'pg';

// Test configuration
export const TEST_CONFIG = {
  baseUrls: ['http://localhost:6632'],
  dbConfig: {
    host: process.env.PG_HOST || 'localhost',
    port: process.env.PG_PORT || 5432,
    database: process.env.PG_DB || 'postgres',
    user: process.env.PG_USER || 'postgres',
    password: process.env.PG_PASSWORD || 'postgres'
  }
};

// Global test state
export let dbPool;
export let testResults = [];
export let currentTest = '';

// Initialize database pool
export async function initDb() {
  dbPool = new pg.Pool(TEST_CONFIG.dbConfig);
  await dbPool.query('SELECT 1');
  return dbPool;
}

// Close database pool
export async function closeDb() {
  if (dbPool) {
    await dbPool.end();
  }
}

// Test logging utilities
export const log = (message, type = 'info') => {
  const timestamp = new Date().toISOString().substring(11, 23);
  const prefix = {
    info: '📝',
    success: '✅',
    error: '❌',
    warning: '⚠️',
    test: '🧪',
    enterprise: '🏢',
    edge: '🔍',
    pattern: '🎯',
    workflow: '🔄',
    priority: '⚡'
  }[type] || '📝';
  
  console.log(`[${timestamp}] ${prefix} ${message}`);
};

export const startTest = (testName, category = 'test') => {
  currentTest = testName;
  log(`Starting: ${testName}`, category);
};

export const passTest = (message = '') => {
  const result = { test: currentTest, status: 'PASS', message };
  testResults.push(result);
  log(`PASS: ${currentTest} ${message}`, 'success');
};

export const failTest = (error) => {
  const result = { test: currentTest, status: 'FAIL', error: error.message };
  testResults.push(result);
  log(`FAIL: ${currentTest} - ${error.message}`, 'error');
};

export const sleep = (ms) => new Promise(resolve => setTimeout(resolve, ms));

// Database utilities
export const cleanupTestData = async () => {
  try {
    await dbPool.query(`
      DELETE FROM queen.messages 
      WHERE partition_id IN (
        SELECT p.id FROM queen.partitions p
        JOIN queen.queues q ON p.queue_id = q.id
        WHERE q.name LIKE 'test-%' OR q.name LIKE 'edge-%' OR q.name LIKE 'pattern-%' OR q.name LIKE 'workflow-%'
      )
    `);
    
    await dbPool.query(`
      DELETE FROM queen.partitions 
      WHERE queue_id IN (
        SELECT id FROM queen.queues WHERE name LIKE 'test-%' OR name LIKE 'edge-%' OR name LIKE 'pattern-%' OR name LIKE 'workflow-%'
      )
    `);
    
    await dbPool.query(`DELETE FROM queen.queues WHERE name LIKE 'test-%' OR name LIKE 'edge-%' OR name LIKE 'pattern-%' OR name LIKE 'workflow-%'`);
    
    await dbPool.query(`
      DELETE FROM queen.retention_history
      WHERE partition_id NOT IN (SELECT id FROM queen.partitions)
    `);
    
    log('Test data cleaned up');
  } catch (error) {
    log(`Cleanup error: ${error.message}`, 'warning');
  }
};

export const getMessageCount = async (queueName, partitionName = null) => {
  const query = partitionName 
    ? `SELECT COUNT(*) as count FROM queen.messages m
       JOIN queen.partitions p ON m.partition_id = p.id
       JOIN queen.queues q ON p.queue_id = q.id
       WHERE q.name = $1 AND p.name = $2`
    : `SELECT COUNT(*) as count FROM queen.messages m
       JOIN queen.partitions p ON m.partition_id = p.id
       JOIN queen.queues q ON p.queue_id = q.id
       WHERE q.name = $1`;
       
  const params = partitionName ? [queueName, partitionName] : [queueName];
  const result = await dbPool.query(query, params);
  return parseInt(result.rows[0].count);
};

// Print test results summary
export function printResults() {
  console.log('\n' + '='.repeat(80));
  console.log('📊 TEST RESULTS SUMMARY');
  console.log('='.repeat(80));
  
  const passed = testResults.filter(r => r.status === 'PASS').length;
  const failed = testResults.filter(r => r.status === 'FAIL').length;
  const total = testResults.length;
  
  console.log(`\n📈 Overall Results: ${passed}/${total} tests passed`);
  
  if (failed > 0) {
    console.log('\n❌ Failed Tests:');
    testResults
      .filter(r => r.status === 'FAIL')
      .forEach(r => {
        console.log(`   • ${r.test}: ${r.error}`);
      });
  }
  
  if (passed > 0) {
    console.log('\n✅ Passed Tests:');
    testResults
      .filter(r => r.status === 'PASS')
      .forEach(r => {
        console.log(`   • ${r.test}${r.message ? ': ' + r.message : ''}`);
      });
  }
  
  console.log('\n' + '='.repeat(80));
  
  if (failed === 0) {
    console.log('🎉 ALL TESTS PASSED! Queen Message Queue System is working correctly.');
    console.log('   All features are operational.');
  } else {
    console.log(`⚠️  ${failed} test(s) failed. Please review the failures above.`);
    process.exit(1);
  }
}

// Reset test results (useful for running tests in batches)
export function resetResults() {
  testResults = [];
}

