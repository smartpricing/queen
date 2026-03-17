// Docker container metrics collector
// Monitors CPU and memory usage during benchmarks

import { exec } from 'child_process';
import { promisify } from 'util';

const execAsync = promisify(exec);

export class DockerMetrics {
  constructor(containerNames) {
    this.containerNames = Array.isArray(containerNames) ? containerNames : [containerNames];
    this.samples = [];
    this.intervalId = null;
    this.sampleIntervalMs = 1000;  // Sample every second
  }
  
  async collectSample() {
    try {
      // Get stats for all containers in one call
      const containerList = this.containerNames.join(' ');
      const { stdout } = await execAsync(
        `docker stats ${containerList} --no-stream --format "{{.Name}},{{.CPUPerc}},{{.MemUsage}},{{.MemPerc}}"`
      );
      
      const timestamp = Date.now();
      const lines = stdout.trim().split('\n').filter(l => l);
      
      for (const line of lines) {
        const [name, cpuPerc, memUsage, memPerc] = line.split(',');
        
        // Parse CPU percentage (e.g., "45.32%" -> 45.32)
        const cpu = parseFloat(cpuPerc.replace('%', '')) || 0;
        
        // Parse memory percentage
        const mem = parseFloat(memPerc.replace('%', '')) || 0;
        
        // Parse memory usage (e.g., "1.5GiB / 8GiB" -> bytes)
        const memBytes = this.parseMemoryUsage(memUsage);
        
        this.samples.push({
          timestamp,
          container: name,
          cpu,
          memPercent: mem,
          memBytes,
        });
      }
    } catch (e) {
      // Container might not be running yet, ignore
    }
  }
  
  parseMemoryUsage(memStr) {
    // Parse "1.5GiB / 8GiB" or "500MiB / 8GiB"
    const match = memStr.match(/([\d.]+)([A-Za-z]+)/);
    if (!match) return 0;
    
    const value = parseFloat(match[1]);
    const unit = match[2].toLowerCase();
    
    const multipliers = {
      'b': 1,
      'kib': 1024,
      'kb': 1000,
      'mib': 1024 * 1024,
      'mb': 1000 * 1000,
      'gib': 1024 * 1024 * 1024,
      'gb': 1000 * 1000 * 1000,
    };
    
    return value * (multipliers[unit] || 1);
  }
  
  start() {
    this.samples = [];
    this.collectSample();  // Initial sample
    this.intervalId = setInterval(() => this.collectSample(), this.sampleIntervalMs);
    return this;
  }
  
  stop() {
    if (this.intervalId) {
      clearInterval(this.intervalId);
      this.intervalId = null;
    }
    return this;
  }
  
  getStats() {
    if (this.samples.length === 0) {
      return {
        containers: {},
        combined: { cpu: {}, memory: {} },
      };
    }
    
    // Group by container
    const byContainer = {};
    for (const sample of this.samples) {
      if (!byContainer[sample.container]) {
        byContainer[sample.container] = { cpu: [], mem: [], memBytes: [] };
      }
      byContainer[sample.container].cpu.push(sample.cpu);
      byContainer[sample.container].mem.push(sample.memPercent);
      byContainer[sample.container].memBytes.push(sample.memBytes);
    }
    
    const containerStats = {};
    let totalCpuSamples = [];
    let totalMemSamples = [];
    
    for (const [name, data] of Object.entries(byContainer)) {
      containerStats[name] = {
        cpu: this.computeStats(data.cpu),
        memoryPercent: this.computeStats(data.mem),
        memoryMB: this.computeStats(data.memBytes.map(b => b / (1024 * 1024))),
      };
      
      // Aggregate for combined stats
      totalCpuSamples.push(...data.cpu);
      totalMemSamples.push(...data.memBytes);
    }
    
    // Combined stats (sum of all containers at each timestamp)
    const timestamps = [...new Set(this.samples.map(s => s.timestamp))];
    const combinedCpu = [];
    const combinedMem = [];
    
    for (const ts of timestamps) {
      const atTime = this.samples.filter(s => s.timestamp === ts);
      combinedCpu.push(atTime.reduce((sum, s) => sum + s.cpu, 0));
      combinedMem.push(atTime.reduce((sum, s) => sum + s.memBytes, 0));
    }
    
    return {
      containers: containerStats,
      combined: {
        cpu: this.computeStats(combinedCpu),
        memoryMB: this.computeStats(combinedMem.map(b => b / (1024 * 1024))),
      },
      sampleCount: this.samples.length,
      durationMs: timestamps.length > 1 ? timestamps[timestamps.length - 1] - timestamps[0] : 0,
    };
  }
  
  computeStats(values) {
    if (values.length === 0) return { min: 0, max: 0, avg: 0, p95: 0 };
    
    const sorted = [...values].sort((a, b) => a - b);
    const sum = values.reduce((a, b) => a + b, 0);
    
    return {
      min: Number(sorted[0].toFixed(2)),
      max: Number(sorted[sorted.length - 1].toFixed(2)),
      avg: Number((sum / values.length).toFixed(2)),
      p95: Number(sorted[Math.floor(sorted.length * 0.95)].toFixed(2)),
    };
  }
  
  printReport() {
    const stats = this.getStats();
    
    console.log(`\n${'─'.repeat(60)}`);
    console.log('  Docker Resource Usage');
    console.log(`${'─'.repeat(60)}`);
    
    for (const [name, data] of Object.entries(stats.containers)) {
      console.log(`\n  Container: ${name}`);
      console.log(`    CPU:    avg=${data.cpu.avg}%  max=${data.cpu.max}%  p95=${data.cpu.p95}%`);
      console.log(`    Memory: avg=${data.memoryMB.avg}MB  max=${data.memoryMB.max}MB  p95=${data.memoryMB.p95}MB`);
    }
    
    if (Object.keys(stats.containers).length > 1) {
      console.log(`\n  Combined (all containers):`);
      console.log(`    CPU:    avg=${stats.combined.cpu.avg}%  max=${stats.combined.cpu.max}%`);
      console.log(`    Memory: avg=${stats.combined.memoryMB.avg}MB  max=${stats.combined.memoryMB.max}MB`);
    }
    
    console.log(`${'─'.repeat(60)}`);
    
    return stats;
  }
  
  toJSON() {
    return this.getStats();
  }
}

// Helper to get container names for each system
export const CONTAINER_NAMES = {
  queen: ['queen-postgres', 'queen-server'],
  kafka: ['kafka-broker'],
  pulsar: ['pulsar-broker'],
};
