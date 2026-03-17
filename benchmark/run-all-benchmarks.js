// Run all benchmarks and generate comparison report
// Usage: node run-all-benchmarks.js [queen|kafka|pulsar|all]

import { spawn } from 'child_process';
import { readFileSync, writeFileSync, readdirSync } from 'fs';
import Table from 'cli-table3';
import { config } from './config.js';

const SYSTEMS = ['queen', 'kafka', 'pulsar'];

function runCommand(command, args, options = {}) {
  return new Promise((resolve, reject) => {
    console.log(`\n> ${command} ${args.join(' ')}\n`);
    
    const proc = spawn(command, args, {
      stdio: 'inherit',
      shell: true,
      ...options,
    });
    
    proc.on('close', (code) => {
      if (code === 0) {
        resolve();
      } else {
        reject(new Error(`Command failed with code ${code}`));
      }
    });
    
    proc.on('error', reject);
  });
}

async function waitForService(name, checkFn, maxAttempts = 60) {
  console.log(`Waiting for ${name} to be ready...`);
  
  for (let i = 0; i < maxAttempts; i++) {
    try {
      await checkFn();
      console.log(`${name} is ready!`);
      return;
    } catch (e) {
      process.stdout.write('.');
      await new Promise(r => setTimeout(r, 2000));
    }
  }
  
  throw new Error(`${name} failed to start after ${maxAttempts * 2} seconds`);
}

async function runBenchmarkForSystem(system) {
  console.log(`\n${'#'.repeat(60)}`);
  console.log(`#  Starting ${system.toUpperCase()} benchmark`);
  console.log(`${'#'.repeat(60)}`);
  
  // Start the system
  await runCommand('docker', ['compose', '-f', `docker-compose.${system}.yml`, 'up', '-d']);
  
  // Wait for service to be ready
  const healthChecks = {
    queen: async () => {
      const res = await fetch('http://localhost:6632/api/v1/health');
      if (!res.ok) throw new Error('Not ready');
    },
    kafka: async () => {
      // KafkaJS will throw if not connected
      const { Kafka } = await import('kafkajs');
      const kafka = new Kafka({ clientId: 'health', brokers: ['localhost:9092'] });
      const admin = kafka.admin();
      await admin.connect();
      await admin.disconnect();
    },
    pulsar: async () => {
      const res = await fetch('http://localhost:8080/admin/v2/brokers/healthcheck');
      if (!res.ok) throw new Error('Not ready');
    },
  };
  
  await waitForService(system, healthChecks[system]);
  
  // Run setup
  console.log(`\nRunning ${system} setup...`);
  await runCommand('node', [`scripts/setup-${system}.js`]);
  
  // Run benchmark
  console.log(`\nRunning ${system} benchmark...`);
  await runCommand('node', [`benchmark-${system}.js`]);
  
  // Stop the system
  console.log(`\nStopping ${system}...`);
  await runCommand('docker', ['compose', '-f', `docker-compose.${system}.yml`, 'down', '-v']);
}

function findLatestResults(system) {
  const files = readdirSync('.').filter(f => f.startsWith(`results-${system}-`) && f.endsWith('.json'));
  if (files.length === 0) return null;
  
  files.sort().reverse();
  return JSON.parse(readFileSync(files[0], 'utf8'));
}

function generateComparisonReport() {
  console.log('\n\n');
  console.log('╔════════════════════════════════════════════════════════════════════════════╗');
  console.log('║                    BENCHMARK COMPARISON REPORT                             ║');
  console.log('╚════════════════════════════════════════════════════════════════════════════╝');
  
  const results = {};
  for (const system of SYSTEMS) {
    results[system] = findLatestResults(system);
  }
  
  const availableSystems = SYSTEMS.filter(s => results[s]);
  
  if (availableSystems.length === 0) {
    console.log('\nNo results found. Run benchmarks first.');
    return;
  }
  
  // Configuration
  console.log('\n📋 Configuration:');
  console.log(`   Partitions: ${config.partitionCount}`);
  console.log(`   Message size: ${config.messageSize} bytes`);
  console.log(`   Linger/Batching: DISABLED (fair comparison)`);
  console.log(`   Resource limits: 4 CPU cores, 8GB RAM`);
  
  const benchmarkTypes = ['latency', 'throughput', 'partitionFanout'];
  
  for (const benchType of benchmarkTypes) {
    console.log(`\n\n📊 ${benchType.toUpperCase()} BENCHMARK`);
    console.log('─'.repeat(76));
    
    // Throughput table
    const throughputTable = new Table({
      head: ['System', 'Messages/sec', 'MB/sec', 'Errors'],
      colWidths: [20, 18, 12, 10],
    });
    
    for (const system of availableSystems) {
      const bench = results[system]?.benchmarks?.[benchType];
      if (bench) {
        throughputTable.push([
          system.toUpperCase(),
          bench.throughput?.messagesPerSecond?.toLocaleString() || 'N/A',
          bench.throughput?.mbPerSecond || 'N/A',
          bench.messages?.errors || 0,
        ]);
      }
    }
    
    console.log('\nThroughput:');
    console.log(throughputTable.toString());
    
    // Latency table
    const latencyTable = new Table({
      head: ['System', 'p50', 'p95', 'p99', 'p99.9', 'Max'],
      colWidths: [20, 12, 12, 12, 12, 12],
    });
    
    for (const system of availableSystems) {
      const bench = results[system]?.benchmarks?.[benchType];
      if (bench?.latency) {
        latencyTable.push([
          system.toUpperCase(),
          `${bench.latency.p50}ms`,
          `${bench.latency.p95}ms`,
          `${bench.latency.p99}ms`,
          `${bench.latency.p999}ms`,
          `${bench.latency.max}ms`,
        ]);
      }
    }
    
    console.log('\nLatency (lower is better):');
    console.log(latencyTable.toString());
  }
  
  // Docker Resource Usage
  console.log(`\n\n🐳 DOCKER RESOURCE USAGE`);
  console.log('─'.repeat(76));
  
  const resourceTable = new Table({
    head: ['System', 'Avg CPU %', 'Max CPU %', 'Avg Memory MB', 'Max Memory MB'],
    colWidths: [20, 14, 14, 16, 16],
  });
  
  for (const system of availableSystems) {
    const docker = results[system]?.dockerMetrics;
    if (docker?.combined) {
      resourceTable.push([
        system.toUpperCase(),
        docker.combined.cpu?.avg || 'N/A',
        docker.combined.cpu?.max || 'N/A',
        docker.combined.memoryMB?.avg || 'N/A',
        docker.combined.memoryMB?.max || 'N/A',
      ]);
    } else {
      resourceTable.push([system.toUpperCase(), 'N/A', 'N/A', 'N/A', 'N/A']);
    }
  }
  
  console.log('\nCombined container resource usage during benchmark:');
  console.log(resourceTable.toString());
  
  // Per-container breakdown if available
  for (const system of availableSystems) {
    const docker = results[system]?.dockerMetrics?.containers;
    if (docker && Object.keys(docker).length > 1) {
      console.log(`\n  ${system.toUpperCase()} breakdown:`);
      for (const [container, stats] of Object.entries(docker)) {
        console.log(`    ${container}: CPU avg=${stats.cpu?.avg}% max=${stats.cpu?.max}%  Mem avg=${stats.memoryMB?.avg}MB max=${stats.memoryMB?.max}MB`);
      }
    }
  }
  
  // Summary
  console.log('\n\n📝 SUMMARY');
  console.log('─'.repeat(76));
  console.log(`
This benchmark tests ${config.partitionCount} partitions with NO client-side batching.
This represents a workload where:
  - Individual message latency matters
  - Messages must be persisted immediately (no buffering)
  - High partition/topic count is required

Key observations:
  - Queen (PostgreSQL) handles many partitions efficiently via SQL
  - Kafka's strength (batching) is intentionally disabled
  - Pulsar's separation of storage/compute is tested at small scale

NOTE: This is NOT a general-purpose benchmark. It specifically tests the
use case where Queen excels: low-latency, high-partition-count workloads
without batching optimizations.
`);
  
  // Save comparison report
  const reportData = {
    timestamp: new Date().toISOString(),
    config: {
      partitions: config.partitionCount,
      messageSize: config.messageSize,
      lingerMs: 0,
      resources: '4 CPU, 8GB RAM',
    },
    results,
  };
  
  writeFileSync('comparison-report.json', JSON.stringify(reportData, null, 2));
  console.log('\nFull report saved to comparison-report.json');
}

async function main() {
  const args = process.argv.slice(2);
  const target = args[0] || 'all';
  
  console.log('╔════════════════════════════════════════════════════════════╗');
  console.log('║     Queen vs Kafka vs Pulsar Benchmark Suite               ║');
  console.log('╚════════════════════════════════════════════════════════════╝');
  
  if (target === 'all') {
    for (const system of SYSTEMS) {
      try {
        await runBenchmarkForSystem(system);
      } catch (e) {
        console.error(`\n❌ ${system} benchmark failed:`, e.message);
      }
    }
    generateComparisonReport();
  } else if (target === 'report') {
    generateComparisonReport();
  } else if (SYSTEMS.includes(target)) {
    await runBenchmarkForSystem(target);
  } else {
    console.log(`\nUsage: node run-all-benchmarks.js [queen|kafka|pulsar|all|report]`);
    console.log(`\n  queen  - Run Queen MQ benchmark only`);
    console.log(`  kafka  - Run Kafka benchmark only`);
    console.log(`  pulsar - Run Pulsar benchmark only`);
    console.log(`  all    - Run all benchmarks (default)`);
    console.log(`  report - Generate comparison report from existing results`);
  }
}

main().catch(console.error);
