#!/usr/bin/env node

/**
 * Optimize Affinity Hash Ring
 * 
 * Analyzes your consumer groups and determines the optimal affinityHashRing
 * value for uniform load distribution across Queen server replicas.
 * 
 * Usage: node examples/optimize-affinity-hash-ring.js
 */

// FNV-1a hash function (same as LoadBalancer)
function hashString(str) {
  let hash = 2166136261 // FNV offset basis
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
    
    // Build virtual node ring
    for (const server of servers) {
      for (let i = 0; i < replicasPerServer; i++) {
        const vnodeKey = `${server}#vnode${i}`
        const hash = hashString(vnodeKey)
        this.vnodes.push({ hash, server })
      }
    }
    
    // Sort by hash
    this.vnodes.sort((a, b) => a.hash - b.hash)
  }
  
  getServer(key) {
    const keyHash = hashString(key)
    
    // Binary search for first vnode >= keyHash
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

// Your production consumer groups (extracted from the data)
const consumerGroups = [
  // __QUEUE_MODE__ groups (these ALSO use affinity routing!)
  { queue: 'smartchat.agent.document-to-process', group: '__QUEUE_MODE__' },
  { queue: 'smartchat.agent.incoming', group: '__QUEUE_MODE__' },
  { queue: 'smartchat.agent.learner', group: '__QUEUE_MODE__' },
  { queue: 'smartchat.agent.suggestion-eval', group: '__QUEUE_MODE__' },
  { queue: 'smartchat.router.history', group: '__QUEUE_MODE__' },
  { queue: 'smartchat.router.incoming', group: '__QUEUE_MODE__' },
  { queue: 'smartchat.translations', group: '__QUEUE_MODE__' },
  
  // Named consumer groups
  { queue: 'smartchat.router.outgoing', group: 'email-outbox' },
  { queue: 'smartchat.router.outgoing', group: 'integration-meta-wa' },
  { queue: 'smartchat.router.outgoing', group: 'push-notification-sender' },
  { queue: 'smartchat.router.outgoing', group: 'smartchat-agent-js-admin' },
  { queue: 'smartchat.router.outgoing', group: 'smartchat-agent-js-chat-augmentation' },
  { queue: 'smartchat.router.outgoing', group: 'wa-lite-393518289182' },
  { queue: 'smartchat.router.outgoing', group: 'wa-lite-393755324931' },
  { queue: 'smartchat.router.outgoing', group: 'wa-lite-393762141506' },
  { queue: 'smartchat.events', group: 'ws-app-server-smartchat-ws-0' },
  { queue: 'smartchat.router.outgoing', group: 'ws-app-server-smartchat-ws-0' },
  { queue: 'smartchat.events', group: 'ws-app-server-smartchat-ws-1' },
  { queue: 'smartchat.router.outgoing', group: 'ws-app-server-smartchat-ws-1' },
  { queue: 'smartchat.events', group: 'ws-app-server-smartchat-ws-2' },
  { queue: 'smartchat.router.outgoing', group: 'ws-app-server-smartchat-ws-2' },
]

// Generate affinity keys (queue:partition:consumerGroup)
const affinityKeys = consumerGroups.map(cg => `${cg.queue}:*:${cg.group}`)

// Test configurations
const serverCounts = [3, 4, 5]  // Test with 3, 4, 5 servers
const hashRingValues = [50, 100, 128, 150, 200, 300, 500, 750, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000]

console.log('='.repeat(80))
console.log('AFFINITY HASH RING OPTIMIZATION')
console.log('='.repeat(80))
console.log()
console.log(`Analyzing ${consumerGroups.length} consumer groups`)
console.log()
console.log('Consumer Groups (sorted by affinity key):')
affinityKeys.forEach((key, i) => {
  console.log(`  ${(i + 1).toString().padStart(2)}. ${key}`)
})
console.log()
console.log('Note: ALL groups use affinity routing (including __QUEUE_MODE__)')
console.log('      Each unique queue:partition:group combination gets a consistent server')
console.log()

// Store results for comparison
const allResults = []

for (const serverCount of serverCounts) {
  console.log('='.repeat(80))
  console.log(`TESTING WITH ${serverCount} SERVERS`)
  console.log('='.repeat(80))
  console.log()
  
  const servers = Array.from({ length: serverCount }, (_, i) => `queen-server-${i + 1}`)
  
  for (const hashRing of hashRingValues) {
    const hasher = new VirtualNodeHasher(servers, hashRing)
    
    // Distribute consumer groups
    const distribution = {}
    servers.forEach(s => {
      distribution[s] = { groups: [], count: 0 }
    })
    
    affinityKeys.forEach(key => {
      const server = hasher.getServer(key)
      distribution[server].groups.push(key)
      distribution[server].count++
    })
    
    // Calculate statistics
    const counts = servers.map(s => distribution[s].count)
    const avgCount = counts.reduce((a, b) => a + b, 0) / serverCount
    const minCount = Math.min(...counts)
    const maxCount = Math.max(...counts)
    
    // Standard deviation
    const variance = counts.reduce((sum, count) => sum + Math.pow(count - avgCount, 2), 0) / serverCount
    const stdDev = Math.sqrt(variance)
    
    // Balance score (lower is better, 0 = perfect balance)
    const balanceScore = ((stdDev / avgCount) * 100).toFixed(1)
    
    // Distribution range
    const range = maxCount - minCount
    
    allResults.push({
      serverCount,
      hashRing,
      balanceScore: parseFloat(balanceScore),
      stdDev,
      minCount,
      maxCount,
      range,
      distribution
    })
    
    console.log(`affinityHashRing: ${hashRing.toString().padStart(4)}`)
    console.log(`  Distribution: Min=${minCount}, Max=${maxCount}, Avg=${avgCount.toFixed(2)}, Range=${range}`)
    console.log(`  Std Dev: ${stdDev.toFixed(2)}`)
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
  
  // Sort by balance score (lower is better)
  resultsForServerCount.sort((a, b) => a.balanceScore - b.balanceScore)
  
  const best = resultsForServerCount[0]
  const worst = resultsForServerCount[resultsForServerCount.length - 1]
  
  console.log(`For ${serverCount} servers:`)
  console.log(`  ✅ BEST:  affinityHashRing = ${best.hashRing}`)
  console.log(`     Balance Score: ${best.balanceScore}%`)
  console.log(`     Distribution: ${best.minCount}-${best.maxCount} groups per server (range: ${best.range})`)
  console.log()
  console.log(`  ❌ WORST: affinityHashRing = ${worst.hashRing}`)
  console.log(`     Balance Score: ${worst.balanceScore}%`)
  console.log(`     Distribution: ${worst.minCount}-${worst.maxCount} groups per server (range: ${worst.range})`)
  console.log()
}

// Overall best recommendation
console.log('='.repeat(80))
console.log('OVERALL RECOMMENDATION')
console.log('='.repeat(80))
console.log()

// Find best for 3 servers (most common HA setup)
const resultsFor3Servers = allResults.filter(r => r.serverCount === 3)
resultsFor3Servers.sort((a, b) => a.balanceScore - b.balanceScore)
const overallBest = resultsFor3Servers[0]

console.log(`📊 With your ${consumerGroups.length} consumer groups and 3 servers (HA):`)
console.log()
console.log(`   ✅ Use: affinityHashRing = ${overallBest.hashRing}`)
console.log()
console.log(`   Results:`)
console.log(`     • Balance Score: ${overallBest.balanceScore}% (perfect = 0%)`)
console.log(`     • Groups per server: ${overallBest.minCount} to ${overallBest.maxCount}`)
console.log(`     • Distribution range: ${overallBest.range} groups`)
console.log()
console.log('   Server Distribution:')
const servers = Object.keys(overallBest.distribution).sort()
servers.forEach(server => {
  const count = overallBest.distribution[server].count
  const bar = '█'.repeat(Math.round((count / overallBest.maxCount) * 30))
  console.log(`     ${server}: ${bar} ${count} groups`)
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
console.log(`     affinityHashRing: ${overallBest.hashRing}  // 👈 Optimal for your workload`)
console.log('   })')
console.log('   ```')
console.log()

console.log('💡 Notes:')
console.log('   • This analysis is based on your current consumer groups')
console.log('   • As you add more groups, distribution will improve')
console.log(`   • With ${consumerGroups.length} groups, small imbalances are normal (law of large numbers)`)
console.log('   • Higher hashRing values use more memory (~12 bytes per vnode)')
console.log(`   • Memory usage: ${overallBest.hashRing} vnodes × 3 servers × 12 bytes = ${(overallBest.hashRing * 3 * 12 / 1024).toFixed(1)} KB`)
console.log()
console.log('⚠️  Important:')
console.log('   • __QUEUE_MODE__ groups ALSO use affinity routing')
console.log('   • Each queue without a consumer group gets its own affinity key')
console.log('   • This means all workers polling the same queue (without group) hit same server')
console.log()

// Show detailed distribution for best configuration
console.log('='.repeat(80))
console.log('DETAILED DISTRIBUTION (BEST CONFIGURATION)')
console.log('='.repeat(80))
console.log()
console.log(`affinityHashRing: ${overallBest.hashRing} with 3 servers`)
console.log()

servers.forEach(server => {
  const groups = overallBest.distribution[server].groups
  console.log(`${server} (${groups.length} groups):`)
  groups.forEach(group => {
    console.log(`  • ${group}`)
  })
  console.log()
})

console.log('='.repeat(80))
console.log('ANALYSIS COMPLETE')
console.log('='.repeat(80))

