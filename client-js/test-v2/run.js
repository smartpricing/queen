import pg from 'pg';
import { Queen } from '../client-v2/index.js'
import * as queueTests from './queue.js'
import * as pushTests from './push.js'
import * as popTests from './pop.js'
import * as consumerTests from './consume.js'
import * as loadTests from './load.js'
import * as dlqTests from './dlq.js'
import * as completeTests from './complete.js'
import * as transactionTests from './transaction.js'
import * as subscriptionTests from './subscription.js'
import * as maintenanceTests from './maintenance.js'
import * as retentionTests from './retention.js'
import * as errorHandlingTests from './ai_error_handling.js'
import * as leaseRenewalTests from './ai_lease_renewal.js'
import * as resourcesTests from './ai_resources.js'
import * as bufferingTests from './ai_buffering.js'
import * as priorityTests from './ai_priority.js'
import * as ttlRetentionTests from './ai_ttl_retention.js'
import * as mixedScenariosTests from './ai_mixed_scenarios.js'


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

function log (success, ...args) {
    console.log(new Date().toISOString(), success ? '✅' : '❌', ...args)
}

const testResults = []
function addRestResult (success, testName, message) {
    testResults.push({ success, testName, message })
}

function printResults() {
    console.log('='.repeat(80))
    console.log('Results:')
    console.log(testResults.map(x => `${x.success ? '✅' : '❌'} ${x.testName}: ${x.message}`).join('\n'))

    const passed = testResults.filter(x => x.success).length
    const failed = testResults.filter(x => !x.success).length
    const total = testResults.length
    console.log('='.repeat(80))
    console.log(`Overall Results: ${passed}/${total} tests passed, ${failed}/${total} tests failed`)
    console.log('='.repeat(80))
}

export const cleanupTestData = async () => {
    try {
      await dbPool.query(`DELETE FROM queen.queues WHERE name LIKE 'test-%' OR name LIKE 'edge-%' OR name LIKE 'pattern-%' OR name LIKE 'workflow-%'`);
      
      log('Test data cleaned up');
    } catch (error) {
      log(`Cleanup error: ${error.message}`, 'warning');
    }
  };

async function main() {
    const client = new Queen(TEST_CONFIG.baseUrls)
    await initDb()

    // Separate human and AI tests
    const humanTests = [
        queueTests,
        pushTests,
        popTests,
        consumerTests,
        loadTests,
        dlqTests,
        completeTests,
        transactionTests,
        subscriptionTests,
        retentionTests,
        maintenanceTests
    ]
    
    const aiTests = [
        errorHandlingTests,
        leaseRenewalTests,
        resourcesTests,
        bufferingTests,
        priorityTests,
        ttlRetentionTests,
        mixedScenariosTests
    ]

    const allTests = [...humanTests, ...aiTests]
    const allTestFunctions = allTests.map(x => Object.values(x)).flat()
    const humanTestFunctions = humanTests.map(x => Object.values(x)).flat()
    const aiTestFunctions = aiTests.map(x => Object.values(x)).flat()

    // Check command line arguments
    const firstArg = process.argv[2]
    
    let testsToRun = allTestFunctions
    let mode = 'all'

    // Check if filtering by test origin
    if (firstArg === 'ai') {
        testsToRun = aiTestFunctions
        mode = 'ai'
        log(true, `Running AI-generated tests only (${aiTestFunctions.length} tests)...`)
    } else if (firstArg === 'human') {
        testsToRun = humanTestFunctions
        mode = 'human'
        log(true, `Running human-written tests only (${humanTestFunctions.length} tests)...`)
    } else if (firstArg && firstArg !== 'all') {
        // Check if it's a specific test name
        const testFunc = allTestFunctions.find(t => t.name === firstArg)
        if (!testFunc) {
            console.log(`❌ Test '${firstArg}' not found`)
            console.log('\nUsage:')
            console.log('  node run.js              # Run all tests')
            console.log('  node run.js ai           # Run only AI-generated tests')
            console.log('  node run.js human        # Run only human-written tests')
            console.log('  node run.js <testName>   # Run specific test')
            console.log('\nAvailable tests:')
            console.log('\n🤖 AI-generated tests:')
            aiTestFunctions.forEach(t => console.log(`  - ${t.name}`))
            console.log('\n👤 Human-written tests:')
            humanTestFunctions.forEach(t => console.log(`  - ${t.name}`))
            await closeDb()
            process.exit(1)
        }
        testsToRun = [testFunc]
        mode = 'single'
        log(true, `Running single test: ${firstArg}`)
    } else {
        log(true, `Running all tests (${allTestFunctions.length} tests)...`)
    }

    // Cleanup test data
    await cleanupTestData()
    
    for (const test of testsToRun) {
        try {
            console.log('Running test:', test.name)
            const result = await test(client)
            const message = result.message || 'Test completed successfully'
            addRestResult(result.success, test.name, message)
            log(result.success, test.name, message)
        } catch (error) {
            addRestResult(false, test.name, `Test threw error: ${error.message}`)
            log(false, test.name, 'Test failed:', error.message)
        }
    }
    
    printResults()
    
    // Show summary based on mode
    if (mode === 'ai') {
        console.log('\n💡 Tip: Run "node run.js human" to test human-written tests')
        console.log('💡 Tip: Run "node run.js" to test all tests')
    } else if (mode === 'human') {
        console.log('\n💡 Tip: Run "node run.js ai" to test AI-generated tests')
        console.log('💡 Tip: Run "node run.js" to test all tests')
    }
    
    //await cleanupTestData()
    await closeDb()
}

try {
    await main()
} catch (error) {
    log(false, 'Main error:', error.message)
}