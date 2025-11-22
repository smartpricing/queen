#!/usr/bin/env node

/**
 * Optimize Affinity Hash Ring (Weighted Version)
 * 
 * Analyzes your consumer groups with load weights and determines the optimal 
 * affinityHashRing value for uniform WORKLOAD distribution (not just count).
 * 
 * Usage: node examples/optimize-affinity-hash-ring-weighted.js
 */

// FNV-1a hash function (same as LoadBalancer)
function hashString(str) {
  let hash = 2166136261
  for (let i = 0; i < str.length; i++) {
    hash ^= str.charCodeAt(i)
    hash += (hash << 1) + (hash << 4) + (hash << 7) + (hash << 8) + (hash << 24)
  }
  return hash >>> 0
}

// Virtual node consistent hashing
class VirtualNodeHasher {
  constructor(servers, replicasPerServer) {
    this.servers = servers
    this.vnodes = []
    
    for (const server of servers) {
      for (let i = 0; i < replicasPerServer; i++) {
        const vnodeKey = `${server}#vnode${i}`
        const hash = hashString(vnodeKey)
        this.vnodes.push({ hash, server })
      }
    }
    
    this.vnodes.sort((a, b) => a.hash - b.hash)
  }
  
  getServer(key) {
    const keyHash = hashString(key)
    let left = 0
    let right = this.vnodes.length - 1
    let result = 0
    
    while (left <= right) {
      const mid = Math.floor((left + right) / 2)
      if (this.vnodes[mid].hash >= keyHash) {
        result = mid
        right = mid - 1
      } else {
        left = mid + 1
      }
    }
    
    return this.vnodes[result].server
  }
}

// Your production consumer groups WITH WEIGHTS
// Weight represents relative load (messages/sec, CPU usage, etc.)
// Adjust these based on your actual metrics!
const consumerGroups = [
  // High-traffic queues (smartchat.router.outgoing)
  { queue: 'smartchat.router.outgoing', group: 'email-outbox', weight: 100 },
  { queue: 'smartchat.router.outgoing', group: 'integration-meta-wa', weight: 100 },
  { queue: 'smartchat.router.outgoing', group: 'push-notification-sender', weight: 100 },
  { queue: 'smartchat.router.outgoing', group: 'smartchat-agent-js-admin', weight: 100 },
  { queue: 'smartchat.router.outgoing', group: 'smartchat-agent-js-chat-augmentation', weight: 100 },
  { queue: 'smartchat.router.outgoing', group: 'wa-lite-393518289182', weight: 100 },
  { queue: 'smartchat.router.outgoing', group: 'wa-lite-393755324931', weight: 100 },
  { queue: 'smartchat.router.outgoing', group: 'wa-lite-393762141506', weight: 100 },
  { queue: 'smartchat.router.outgoing', group: 'ws-app-server-smartchat-ws-0', weight: 100 },
  { queue: 'smartchat.router.outgoing', group: 'ws-app-server-smartchat-ws-1', weight: 100 },
  { queue: 'smartchat.router.outgoing', group: 'ws-app-server-smartchat-ws-2', weight: 100 },
  
  // Medium-traffic queues
  { queue: 'smartchat.agent.incoming', group: '__QUEUE_MODE__', weight: 50 },
  { queue: 'smartchat.router.incoming', group: '__QUEUE_MODE__', weight: 50 },
  { queue: 'smartchat.router.history', group: '__QUEUE_MODE__', weight: 50 },
  
  // WebSocket events (lower traffic but important)
  { queue: 'smartchat.events', group: 'ws-app-server-smartchat-ws-0', weight: 30 },
  { queue: 'smartchat.events', group: 'ws-app-server-smartchat-ws-1', weight: 30 },
  { queue: 'smartchat.events', group: 'ws-app-server-smartchat-ws-2', weight: 30 },
  
  // Low-traffic queues
  { queue: 'smartchat.translations', group: '__QUEUE_MODE__', weight: 10 },
  { queue: 'smartchat.agent.learner', group: '__QUEUE_MODE__', weight: 10 },
  { queue: 'smartchat.agent.suggestion-eval', group: '__QUEUE_MODE__', weight: 10 },
  { queue: 'smartchat.agent.document-to-process', group: '__QUEUE_MODE__', weight: 10 },
]

const affinityKeys = consumerGroups.map(cg => ({
  key: `${cg.queue}:*:${cg.group}`,
  weight: cg.weight,
  queue: cg.queue,
  group: cg.group
}))

const totalWeight = consumerGroups.reduce((sum, cg) => sum + cg.weight, 0)

// Test configurations
const serverCounts = [3, 4, 5]
const hashRingValues = [50, 100, 128, 150, 200, 300, 500, 750, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000]

console.log('='.repeat(80))
console.log('WEIGHTED AFFINITY HASH RING OPTIMIZATION')
console.log('='.repeat(80))
console.log()
console.log(`Analyzing ${consumerGroups.length} consumer groups with load weights`)
console.log(`Total Weight: ${totalWeight} (relative load units)`)
console.log()
console.log('Consumer Groups (sorted by weight):')
const sortedGroups = [...affinityKeys].sort((a, b) => b.weight - a.weight)
sortedGroups.forEach((item, i) => {
  const percentage = ((item.weight / totalWeight) * 100).toFixed(1)
  console.log(`  ${(i + 1).toString().padStart(2)}. [Weight: ${item.weight.toString().padStart(3)}] ${percentage.padStart(5)}%  ${item.key}`)
})
console.log()
console.log('💡 Adjust weights in the script based on your actual metrics:')
console.log('   • Messages/second per consumer group')
console.log('   • CPU usage per consumer group')
console.log('   • Average processing time')
console.log('   • Database query load')
console.log()

const allResults = []

for (const serverCount of serverCounts) {
  console.log('='.repeat(80))
  console.log(`TESTING WITH ${serverCount} SERVERS`)
  console.log('='.repeat(80))
  console.log()
  
  const servers = Array.from({ length: serverCount }, (_, i) => `queen-server-${i + 1}`)
  const idealLoadPerServer = totalWeight / serverCount
  
  console.log(`Target Load per Server: ${idealLoadPerServer.toFixed(1)} (${(100/serverCount).toFixed(1)}% of total)`)
  console.log()
  
  for (const hashRing of hashRingValues) {
    const hasher = new VirtualNodeHasher(servers, hashRing)
    
    // Distribute consumer groups
    const distribution = {}
    servers.forEach(s => {
      distribution[s] = { groups: [], count: 0, totalWeight: 0 }
    })
    
    affinityKeys.forEach(item => {
      const server = hasher.getServer(item.key)
      distribution[server].groups.push(item)
      distribution[server].count++
      distribution[server].totalWeight += item.weight
    })
    
    // Calculate WEIGHTED statistics
    const weights = servers.map(s => distribution[s].totalWeight)
    const avgWeight = weights.reduce((a, b) => a + b, 0) / serverCount
    const minWeight = Math.min(...weights)
    const maxWeight = Math.max(...weights)
    
    // Weighted standard deviation
    const variance = weights.reduce((sum, weight) => sum + Math.pow(weight - avgWeight, 2), 0) / serverCount
    const stdDev = Math.sqrt(variance)
    
    // Balance score (lower is better)
    const balanceScore = ((stdDev / avgWeight) * 100).toFixed(1)
    
    // Weight range
    const weightRange = maxWeight - minWeight
    const weightRangePercent = ((weightRange / totalWeight) * 100).toFixed(1)
    
    allResults.push({
      serverCount,
      hashRing,
      balanceScore: parseFloat(balanceScore),
      stdDev,
      minWeight,
      maxWeight,
      weightRange,
      weightRangePercent: parseFloat(weightRangePercent),
      distribution
    })
    
    console.log(`affinityHashRing: ${hashRing.toString().padStart(4)}`)
    console.log(`  Weight Distribution: Min=${minWeight}, Max=${maxWeight}, Avg=${avgWeight.toFixed(1)}, Range=${weightRange} (${weightRangePercent}%)`)
    console.log(`  Weighted Std Dev: ${stdDev.toFixed(2)}`)
    console.log(`  Balance Score: ${balanceScore}% (lower is better)`)
    console.log()
  }
}

// Find best configuration for each server count
console.log('='.repeat(80))
console.log('RECOMMENDATIONS')
console.log('='.repeat(80))
console.log()

for (const serverCount of serverCounts) {
  const resultsForServerCount = allResults.filter(r => r.serverCount === serverCount)
  resultsForServerCount.sort((a, b) => a.balanceScore - b.balanceScore)
  
  const best = resultsForServerCount[0]
  const worst = resultsForServerCount[resultsForServerCount.length - 1]
  
  console.log(`For ${serverCount} servers:`)
  console.log(`  ✅ BEST:  affinityHashRing = ${best.hashRing}`)
  console.log(`     Balance Score: ${best.balanceScore}%`)
  console.log(`     Weight Range: ${best.minWeight}-${best.maxWeight} (${best.weightRangePercent}% imbalance)`)
  console.log()
  console.log(`  ❌ WORST: affinityHashRing = ${worst.hashRing}`)
  console.log(`     Balance Score: ${worst.balanceScore}%`)
  console.log(`     Weight Range: ${worst.minWeight}-${worst.maxWeight} (${worst.weightRangePercent}% imbalance)`)
  console.log()
}

// Overall best recommendation
console.log('='.repeat(80))
console.log('OVERALL RECOMMENDATION')
console.log('='.repeat(80))
console.log()

const resultsFor3Servers = allResults.filter(r => r.serverCount === 3)
resultsFor3Servers.sort((a, b) => a.balanceScore - b.balanceScore)
const overallBest = resultsFor3Servers[0]

const idealLoad = totalWeight / 3

console.log(`📊 With your ${consumerGroups.length} consumer groups and 3 servers (HA):`)
console.log()
console.log(`   ✅ Use: affinityHashRing = ${overallBest.hashRing}`)
console.log()
console.log(`   Results (WEIGHTED by actual load):`)
console.log(`     • Balance Score: ${overallBest.balanceScore}% (perfect = 0%)`)
console.log(`     • Load per server: ${overallBest.minWeight} to ${overallBest.maxWeight} (ideal: ${idealLoad.toFixed(1)})`)
console.log(`     • Load imbalance: ${overallBest.weightRangePercent}% of total`)
console.log()
console.log('   Server Distribution (by workload):')
const servers = Object.keys(overallBest.distribution).sort()
servers.forEach(server => {
  const load = overallBest.distribution[server].totalWeight
  const count = overallBest.distribution[server].count
  const percentage = ((load / totalWeight) * 100).toFixed(1)
  const bar = '█'.repeat(Math.round((load / overallBest.maxWeight) * 40))
  console.log(`     ${server}: ${bar} ${load} load (${percentage}%, ${count} groups)`)
})
console.log()

console.log('   Configuration:')
console.log('   ```javascript')
console.log('   const queen = new Queen({')
console.log('     urls: [')
console.log('       "http://queen-server-1:6632",')
console.log('       "http://queen-server-2:6632",')
console.log('       "http://queen-server-3:6632"')
console.log('     ],')
console.log('     loadBalancingStrategy: "affinity",')
console.log(`     affinityHashRing: ${overallBest.hashRing}  // 👈 Optimal for weighted workload`)
console.log('   })')
console.log('   ```')
console.log()

console.log('💡 Important Notes:')
console.log('   • This uses WEIGHTED distribution (not just group count)')
console.log('   • Adjust weights in script based on YOUR actual metrics')
console.log('   • Monitor CPU/memory usage per server in production')
console.log('   • Re-run analysis when adding new consumer groups')
console.log(`   • Memory usage: ${overallBest.hashRing} vnodes × 3 servers × 12 bytes = ${(overallBest.hashRing * 3 * 12 / 1024).toFixed(1)} KB`)
console.log()

// Show detailed distribution
console.log('='.repeat(80))
console.log('DETAILED DISTRIBUTION (BEST CONFIGURATION)')
console.log('='.repeat(80))
console.log()
console.log(`affinityHashRing: ${overallBest.hashRing} with 3 servers`)
console.log()

servers.forEach(server => {
  const groups = overallBest.distribution[server].groups
  const totalLoad = overallBest.distribution[server].totalWeight
  const percentage = ((totalLoad / totalWeight) * 100).toFixed(1)
  
  console.log(`${server} (${groups.length} groups, ${totalLoad} load, ${percentage}%):`)
  
  // Sort groups by weight (descending)
  const sortedGroups = [...groups].sort((a, b) => b.weight - a.weight)
  sortedGroups.forEach(item => {
    const groupPercent = ((item.weight / totalLoad) * 100).toFixed(1)
    console.log(`  • [${item.weight.toString().padStart(3)}] ${groupPercent.padStart(5)}% of server  ${item.key}`)
  })
  console.log()
})

console.log('='.repeat(80))
console.log('HOW TO GET ACCURATE WEIGHTS')
console.log('='.repeat(80))
console.log()
console.log('Option 1: Use message throughput (messages/second)')
console.log('  • Monitor Queen metrics for each consumer group')
console.log('  • Use average messages/sec as weight')
console.log()
console.log('Option 2: Use CPU/memory usage')
console.log('  • Profile your application')
console.log('  • Measure CPU time per consumer group')
console.log()
console.log('Option 3: Use backlog size')
console.log('  • Consumer groups with large backlogs = higher weight')
console.log('  • Groups with small/empty queues = lower weight')
console.log()
console.log('Option 4: Combination')
console.log('  • Weight = (messages/sec × avg_processing_time) + backlog_penalty')
console.log()
console.log('='.repeat(80))
console.log('ANALYSIS COMPLETE')
console.log('='.repeat(80))

