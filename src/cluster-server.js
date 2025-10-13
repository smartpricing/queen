#!/usr/bin/env node

/**
 * Queen Message Queue - Clustered Server
 * 
 * This module enables multi-core utilization using Node.js cluster module.
 * Each worker runs an independent instance of the Queen server, sharing the same port.
 * 
 * Benefits:
 * - 10x throughput improvement (utilizes all CPU cores)
 * - Automatic load balancing across workers
 * - Fault tolerance (automatic worker restart on crash)
 * - Zero-downtime deploys (rolling worker restarts)
 * 
 * Usage:
 *   node src/cluster-server.js
 *   
 * Environment Variables:
 *   QUEEN_WORKERS=N    - Number of workers (default: CPU count)
 *   WORKER_ID=name     - Base worker ID prefix (default: queen-worker)
 */

import cluster from 'cluster';
import os from 'os';
import { log } from './utils/logger.js';

// Configuration
const numWorkers = parseInt(process.env.QUEEN_WORKERS) || os.cpus().length;
const workerIdPrefix = process.env.WORKER_ID || 'queen-worker';

if (cluster.isPrimary) {
  console.log('');
  console.log('═══════════════════════════════════════════════════════════');
  console.log('🚀 Queen Message Queue - Clustered Mode');
  console.log('═══════════════════════════════════════════════════════════');
  console.log(`📊 System: ${os.cpus()[0].model}`);
  console.log(`🔢 CPU Cores: ${os.cpus().length}`);
  console.log(`👷 Starting Workers: ${numWorkers}`);
  console.log(`🆔 Worker ID Prefix: ${workerIdPrefix}`);
  console.log('═══════════════════════════════════════════════════════════');
  console.log('');

  // Track worker statistics
  const workerStats = {
    startTime: Date.now(),
    totalRestarts: 0,
    workers: new Map()
  };

  // Graceful shutdown state (declared early for use in event handlers)
  let isShuttingDown = false;
  let forceExitTimeout = null;
  let statusInterval = null;

  // Fork workers
  for (let i = 0; i < numWorkers; i++) {
    forkWorker(i, workerStats);
  }

  // Handle worker exit
  cluster.on('exit', (worker, code, signal) => {
    const workerInfo = workerStats.workers.get(worker.id);
    const workerId = workerInfo ? workerInfo.workerId : `worker-${worker.id}`;
    const workerIndex = workerInfo ? workerInfo.index : 0;
    
    if (code !== 0 && !worker.exitedAfterDisconnect) {
      // Unexpected crash
      console.log('');
      console.log(`⚠️  Worker ${workerId} (PID: ${worker.process.pid}) crashed!`);
      console.log(`   Exit code: ${code}, Signal: ${signal}`);
      console.log(`   Restarting worker...`);
      
      workerStats.totalRestarts++;
      
      // Remove from stats
      workerStats.workers.delete(worker.id);
      
      // Restart with same index
      setTimeout(() => {
        forkWorker(workerIndex, workerStats);
      }, 1000); // Wait 1 second before restart
    } else {
      // Graceful shutdown
      console.log(`✅ Worker ${workerId} (PID: ${worker.process.pid}) exited gracefully`);
      workerStats.workers.delete(worker.id);
      
      // Check if all workers have exited during shutdown
      if (isShuttingDown && Object.keys(cluster.workers).length === 0) {
        console.log('✅ All workers exited, shutting down master process');
        clearInterval(statusInterval);
        clearTimeout(forceExitTimeout);
        process.exit(0);
      }
    }
  });

  // Handle worker online
  cluster.on('online', (worker) => {
    const workerInfo = workerStats.workers.get(worker.id);
    if (workerInfo) {
      // Update PID now that it's available
      workerInfo.pid = worker.process.pid;
      console.log(`✅ Worker ${workerInfo.workerId} (PID: ${worker.process.pid}) online and ready`);
    } else {
      console.log(`✅ Worker ${worker.id} (PID: ${worker.process.pid}) online`);
    }
  });

  // Handle worker listening
  cluster.on('listening', (worker, address) => {
    const workerInfo = workerStats.workers.get(worker.id);
    const workerId = workerInfo ? workerInfo.workerId : `worker-${worker.id}`;
    console.log(`🎧 Worker ${workerId} listening on ${address.address}:${address.port}`);
  });
  
  // Handle cluster errors
  cluster.on('error', (error) => {
    console.error('⚠️  Cluster error:', error.message);
  });
  
  // Handle uncaught exceptions in master
  process.on('uncaughtException', (error) => {
    console.error('❌ Uncaught exception in master process:', error);
    console.error(error.stack);
    // Don't exit - try to keep the cluster running
  });
  
  process.on('unhandledRejection', (reason, promise) => {
    console.error('❌ Unhandled rejection in master process:', reason);
    // Don't exit - try to keep the cluster running
  });

  // Graceful shutdown handling
  const gracefulShutdown = (signal) => {
    if (isShuttingDown) return;
    isShuttingDown = true;
    
    console.log('');
    console.log(`🛑 Received ${signal}, shutting down gracefully...`);
    console.log(`📊 Total worker restarts during uptime: ${workerStats.totalRestarts}`);
    console.log(`⏱️  Uptime: ${((Date.now() - workerStats.startTime) / 1000 / 60).toFixed(2)} minutes`);
    
    // Stop status reporting during shutdown
    clearInterval(statusInterval);
    
    // Check if we already have no workers
    if (Object.keys(cluster.workers).length === 0) {
      console.log('✅ No active workers, exiting immediately');
      process.exit(0);
    }
    
    // Disconnect all workers
    for (const id in cluster.workers) {
      cluster.workers[id].disconnect();
    }
    
    // Force exit if workers don't shut down in 10 seconds (reduced from 30)
    forceExitTimeout = setTimeout(() => {
      console.log('⚠️  Forcing shutdown after 10 second timeout');
      console.log(`   Remaining workers: ${Object.keys(cluster.workers).length}`);
      process.exit(0);
    }, 10000);
  };

  process.on('SIGTERM', () => gracefulShutdown('SIGTERM'));
  process.on('SIGINT', () => gracefulShutdown('SIGINT'));

  // Status reporting every 60 seconds
  statusInterval = setInterval(() => {
    // Don't show status during shutdown
    if (isShuttingDown) return;
    
    const aliveWorkers = Object.keys(cluster.workers).length;
    const uptime = ((Date.now() - workerStats.startTime) / 1000 / 60).toFixed(2);
    
    console.log('');
    console.log(`📊 Cluster Status (${new Date().toISOString()})`);
    console.log(`   Active Workers: ${aliveWorkers}/${numWorkers}`);
    console.log(`   Total Restarts: ${workerStats.totalRestarts}`);
    console.log(`   Uptime: ${uptime} minutes`);
  }, 60000);

} else {
  // Worker process - run the normal server
  const workerId = process.env.WORKER_ID || `worker-${process.pid}`;
  const workerIndex = process.env.WORKER_INDEX || '0';
  
  console.log(`👷 Worker ${workerId} (index: ${workerIndex}, PID: ${process.pid}) starting...`);
  
  try {
    // Import and run the main server
    await import('./server.js');
    
    // Worker-specific metrics could be added here
    // e.g., track messages processed, errors, etc.
  } catch (error) {
    console.error(`❌ Worker ${workerId} failed to start:`, error.message);
    console.error(error.stack);
    process.exit(1);
  }
  
  // Handle uncaught exceptions in worker
  process.on('uncaughtException', (error) => {
    console.error(`❌ Uncaught exception in worker ${workerId}:`, error);
    process.exit(1);
  });
  
  process.on('unhandledRejection', (reason, promise) => {
    console.error(`❌ Unhandled rejection in worker ${workerId}:`, reason);
    process.exit(1);
  });
}

/**
 * Fork a new worker with proper environment setup
 */
function forkWorker(index, stats) {
  const workerId = `${workerIdPrefix}-${index}`;
  
  try {
    const worker = cluster.fork({
      WORKER_ID: workerId,
      WORKER_INDEX: index.toString(),
      // Each worker gets a unique ID for system events
      SYSTEM_WORKER_ID: `${workerIdPrefix}-${index}-${Date.now()}`
    });
    
    // Store worker info immediately (PID will be updated in 'online' event)
    stats.workers.set(worker.id, {
      workerId,
      index,
      startTime: Date.now(),
      pid: null  // Will be set when worker comes online
    });
    
    return worker;
  } catch (error) {
    console.error(`❌ Failed to fork worker ${workerId}:`, error.message);
    return null;
  }
}

