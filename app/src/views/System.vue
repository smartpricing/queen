<template>
  <div class="space-y-4 sm:space-y-6 animate-fade-in">
    <!-- Controls -->
    <div class="card p-3 sm:p-4">
      <div class="flex flex-col gap-3">
        <!-- Row 1: Data Source & View Mode -->
        <div class="flex flex-wrap items-center gap-2 sm:gap-4">
          <!-- Data Source Toggle -->
          <div class="flex items-center gap-1 sm:gap-2">
            <span class="text-[10px] sm:text-xs font-medium text-light-500">Source:</span>
            <div class="flex items-center gap-1">
              <button 
                @click="dataSource = 'worker'; fetchData()"
                class="px-3 py-1.5 text-xs font-medium rounded-lg transition-colors"
                :class="dataSource === 'worker' 
                  ? 'bg-queen-500 text-white' 
                  : 'bg-light-100 dark:bg-dark-300 text-light-700 dark:text-light-300 hover:bg-light-200 dark:hover:bg-dark-200'"
              >
                Queue Operations
              </button>
              <button 
                @click="dataSource = 'system'; fetchData()"
                class="px-3 py-1.5 text-xs font-medium rounded-lg transition-colors"
                :class="dataSource === 'system' 
                  ? 'bg-queen-500 text-white' 
                  : 'bg-light-100 dark:bg-dark-300 text-light-700 dark:text-light-300 hover:bg-light-200 dark:hover:bg-dark-200'"
              >
                System Resources
              </button>
              <button 
                @click="dataSource = 'postgres'; fetchData()"
                class="px-3 py-1.5 text-xs font-medium rounded-lg transition-colors"
                :class="dataSource === 'postgres' 
                  ? 'bg-queen-500 text-white' 
                  : 'bg-light-100 dark:bg-dark-300 text-light-700 dark:text-light-300 hover:bg-light-200 dark:hover:bg-dark-200'"
              >
                Postgres Stats
              </button>
            </div>
          </div>

          <!-- View Mode (only for System) -->
          <div v-if="dataSource === 'system'" class="flex items-center gap-2">
            <span class="text-xs font-medium text-light-500">View:</span>
            <div class="flex items-center gap-1">
              <button 
                @click="viewMode = 'individual'"
                class="px-3 py-1.5 text-xs font-medium rounded-lg transition-colors"
                :class="viewMode === 'individual' 
                  ? 'bg-indigo-500 text-white' 
                  : 'bg-light-100 dark:bg-dark-300 text-light-700 dark:text-light-300 hover:bg-light-200 dark:hover:bg-dark-200'"
              >
                Per Server
              </button>
              <button 
                @click="viewMode = 'aggregate'"
                class="px-3 py-1.5 text-xs font-medium rounded-lg transition-colors"
                :class="viewMode === 'aggregate' 
                  ? 'bg-indigo-500 text-white' 
                  : 'bg-light-100 dark:bg-dark-300 text-light-700 dark:text-light-300 hover:bg-light-200 dark:hover:bg-dark-200'"
              >
                Aggregate
              </button>
            </div>
          </div>

          <!-- Aggregation Type (only for System) -->
          <div v-if="dataSource === 'system'" class="flex items-center gap-2">
            <span class="text-xs font-medium text-light-500">Metric:</span>
            <div class="flex items-center gap-1">
              <button 
                v-for="agg in aggregationTypes"
                :key="agg.value"
                @click="aggregationType = agg.value"
                class="px-3 py-1.5 text-xs font-medium rounded-lg transition-colors"
                :class="aggregationType === agg.value 
                  ? 'bg-crown-500 text-white' 
                  : 'bg-light-100 dark:bg-dark-300 text-light-700 dark:text-light-300 hover:bg-light-200 dark:hover:bg-dark-200'"
              >
                {{ agg.label }}
              </button>
            </div>
          </div>
          
          <!-- Time Range (hide for postgres stats - point-in-time) -->
          <div v-if="dataSource !== 'postgres'" class="flex items-center gap-2 ml-auto">
            <span class="text-xs font-medium text-light-500">Range:</span>
            <div class="flex items-center gap-1">
              <button 
                v-for="range in timeRanges" 
                :key="range.value"
                @click="selectQuickRange(range.value)"
                class="px-3 py-1.5 text-xs font-medium rounded-lg transition-colors"
                :class="timeRange === range.value && !customMode
                  ? 'bg-cyber-500 text-white' 
                  : 'bg-light-100 dark:bg-dark-300 text-light-700 dark:text-light-300 hover:bg-light-200 dark:hover:bg-dark-200'"
              >
                {{ range.label }}
              </button>
              <button 
                @click="toggleCustomMode"
                class="px-3 py-1.5 text-xs font-medium rounded-lg transition-colors"
                :class="customMode
                  ? 'bg-cyber-500 text-white' 
                  : 'bg-light-100 dark:bg-dark-300 text-light-700 dark:text-light-300 hover:bg-light-200 dark:hover:bg-dark-200'"
              >
                Custom
              </button>
            </div>
          </div>
        </div>

        <!-- Custom Date/Time Range -->
        <div v-if="customMode" class="flex flex-wrap items-center gap-3 pt-3 border-t border-light-200 dark:border-dark-50">
          <div class="flex items-center gap-2">
            <label class="text-xs font-medium text-light-600 dark:text-light-400 whitespace-nowrap">From:</label>
            <input 
              type="datetime-local" 
              v-model="customFrom"
              class="input text-sm py-1.5 px-2 font-mono"
            />
          </div>
          <div class="flex items-center gap-2">
            <label class="text-xs font-medium text-light-600 dark:text-light-400 whitespace-nowrap">To:</label>
            <input 
              type="datetime-local" 
              v-model="customTo"
              class="input text-sm py-1.5 px-2 font-mono"
            />
          </div>
          <button 
            @click="applyCustomRange"
            class="btn btn-primary text-xs"
          >
            Apply
          </button>
        </div>
      </div>
    </div>

    <div v-if="loading" class="space-y-6">
      <div class="grid grid-cols-1 lg:grid-cols-2 gap-4 sm:gap-6">
        <div v-for="i in 4" :key="i" class="card p-6">
          <div class="skeleton h-48 w-full rounded-lg" />
        </div>
      </div>
    </div>

    <!-- Worker Metrics View -->
    <template v-else-if="dataSource === 'worker' && workerData">
      <!-- Throughput Chart -->
      <div class="card">
        <div class="card-header flex items-center justify-between">
          <h3 class="font-semibold text-light-900 dark:text-white">Message Throughput</h3>
          <span class="text-xs text-light-500">{{ workerData.pointCount || 0 }} data points</span>
        </div>
        <div class="card-body">
          <!-- Metric Toggles -->
          <div class="flex items-center gap-2 flex-wrap mb-4">
            <button
              v-for="metric in throughputMetrics"
              :key="metric.key"
              @click="toggleThroughputMetric(metric.key)"
              class="flex items-center gap-1.5 px-2 py-1 text-xs font-medium rounded transition-colors"
              :class="selectedThroughputMetrics[metric.key] ? metric.activeClass : 'bg-light-100 dark:bg-dark-300 text-light-600'"
            >
              <span class="w-2 h-2 rounded-full" :class="selectedThroughputMetrics[metric.key] ? metric.dotClass : 'bg-gray-400'" />
              {{ metric.label }}
            </button>
          </div>
          <BaseChart 
            v-if="throughputChartData.labels.length > 0"
            type="line" 
            :data="throughputChartData" 
            :options="throughputChartOptions" 
            height="280px"
          />
          <div v-else class="text-center py-12 text-light-500">
            No throughput data available
          </div>
        </div>
      </div>

      <!-- Message Latency -->
      <div class="card">
        <div class="card-header">
          <h3 class="font-semibold text-light-900 dark:text-white">Message Latency</h3>
          <p class="text-xs text-light-500 mt-0.5">Time from push to pop</p>
        </div>
        <div class="card-body">
          <BaseChart 
            v-if="latencyChartData.labels.length > 0"
            type="line" 
            :data="latencyChartData" 
            :options="lagChartOptions" 
            height="240px"
          />
          <div v-else class="text-center py-12 text-light-500">
            No latency data available
          </div>
        </div>
      </div>

      <!-- Event Loop & Connection Pool -->
      <div class="grid grid-cols-1 lg:grid-cols-2 gap-4 sm:gap-6">
        <div class="card">
          <div class="card-header">
            <h3 class="font-semibold text-light-900 dark:text-white">Event Loop Latency</h3>
          </div>
          <div class="card-body">
            <!-- Metric Toggles -->
            <div class="flex items-center gap-2 flex-wrap mb-4">
              <button
                v-for="metric in eventLoopMetrics"
                :key="metric.key"
                @click="toggleEventLoopMetric(metric.key)"
                class="flex items-center gap-1.5 px-2 py-1 text-xs font-medium rounded transition-colors"
                :class="selectedEventLoopMetrics[metric.key] ? metric.activeClass : 'bg-light-100 dark:bg-dark-300 text-light-600'"
              >
                <span class="w-2 h-2 rounded-full" :class="selectedEventLoopMetrics[metric.key] ? metric.dotClass : 'bg-gray-400'" />
                {{ metric.label }}
              </button>
            </div>
            <BaseChart 
              v-if="eventLoopChartData.labels.length > 0"
              type="line" 
              :data="eventLoopChartData" 
              :options="lagChartOptions"
              height="200px"
            />
            <div v-else class="text-center py-12 text-light-500">
              No event loop data available
            </div>
          </div>
        </div>
        
        <div class="card">
          <div class="card-header">
            <h3 class="font-semibold text-light-900 dark:text-white">Connection Pool</h3>
          </div>
          <div class="card-body">
            <BaseChart 
              v-if="connectionPoolChartData.labels.length > 0"
              type="line" 
              :data="connectionPoolChartData" 
              :options="poolChartOptions"
              height="200px"
            />
            <div v-else class="text-center py-12 text-light-500">
              No connection pool data available
            </div>
          </div>
        </div>
      </div>

      <!-- Errors Chart -->
      <div class="card">
        <div class="card-header flex items-center justify-between">
          <h3 class="font-semibold text-light-900 dark:text-white">Errors</h3>
          <span v-if="totalErrors > 0" class="badge badge-danger">{{ totalErrors }} in period</span>
        </div>
        <div class="card-body">
          <!-- Metric Toggles -->
          <div class="flex items-center gap-2 flex-wrap mb-4">
            <button
              v-for="metric in errorMetrics"
              :key="metric.key"
              @click="toggleErrorMetric(metric.key)"
              class="flex items-center gap-1.5 px-2 py-1 text-xs font-medium rounded transition-colors"
              :class="selectedErrorMetrics[metric.key] ? metric.activeClass : 'bg-light-100 dark:bg-dark-300 text-light-600'"
            >
              <span class="w-2 h-2 rounded-full" :class="selectedErrorMetrics[metric.key] ? metric.dotClass : 'bg-gray-400'" />
              {{ metric.label }}
            </button>
          </div>
          <BaseChart 
            v-if="errorsChartData.labels.length > 0"
            type="bar" 
            :data="errorsChartData" 
            :options="errorsChartOptions"
            height="200px"
          />
          <div v-else class="text-center py-12 text-light-500">
            No error data available
          </div>
        </div>
      </div>

      <!-- Workers Status Panel -->
      <div v-if="workerData?.workers?.length" class="card">
        <div class="card-header flex items-center justify-between">
          <h3 class="font-semibold text-light-900 dark:text-white">Workers Status</h3>
          <span class="text-xs text-light-500">{{ workerData.workers.length }} workers</span>
        </div>
        <div class="card-body">
          <div class="overflow-x-auto">
            <table class="w-full text-sm">
              <thead>
                <tr class="border-b border-light-200 dark:border-dark-50">
                  <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Worker ID</th>
                  <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Hostname</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Avg EL</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Max EL</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Free Slots</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">DB Conn</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Job Queue</th>
                  <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Last Seen</th>
                </tr>
              </thead>
              <tbody>
                <tr 
                  v-for="worker in workerData.workers" 
                  :key="worker.workerId"
                  class="border-b border-light-100 dark:border-dark-100"
                >
                  <td class="px-3 py-2 font-medium">{{ worker.workerId }}</td>
                  <td class="px-3 py-2 text-light-600 dark:text-light-400">{{ worker.hostname }}</td>
                  <td class="px-3 py-2 text-right">
                    <span :class="worker.avgEventLoopLagMs > 100 ? 'text-amber-600 dark:text-amber-400' : ''">
                      {{ worker.avgEventLoopLagMs }}ms
                    </span>
                  </td>
                  <td class="px-3 py-2 text-right">
                    <span :class="worker.maxEventLoopLagMs > 500 ? 'text-rose-600 dark:text-rose-400' : ''">
                      {{ worker.maxEventLoopLagMs }}ms
                    </span>
                  </td>
                  <td class="px-3 py-2 text-right text-cyber-600 dark:text-cyber-400">{{ worker.freeSlots }}</td>
                  <td class="px-3 py-2 text-right">{{ worker.dbConnections }}</td>
                  <td class="px-3 py-2 text-right">{{ worker.jobQueueSize }}</td>
                  <td class="px-3 py-2 text-xs text-light-500">{{ formatTime(worker.lastSeen) }}</td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>
    </template>

    <!-- System Metrics View -->
    <template v-else-if="dataSource === 'system' && systemData">
      <!-- CPU & Memory Charts -->
      <div class="grid grid-cols-1 lg:grid-cols-2 gap-4 sm:gap-6">
        <div class="card">
          <div class="card-header flex items-center justify-between">
            <h3 class="font-semibold text-light-900 dark:text-white">CPU Usage</h3>
            <span class="text-xs text-light-500">{{ systemData.replicaCount || 0 }} replicas</span>
          </div>
          <div class="card-body">
            <BaseChart 
              v-if="cpuChartData.labels.length > 0"
              type="line" 
              :data="cpuChartData" 
              :options="cpuChartOptions"
              height="240px"
            />
            <div v-else class="text-center py-12 text-light-500">
              No CPU data available
            </div>
          </div>
        </div>
        
        <div class="card">
          <div class="card-header">
            <h3 class="font-semibold text-light-900 dark:text-white">Memory Usage</h3>
          </div>
          <div class="card-body">
            <BaseChart 
              v-if="memoryChartData.labels.length > 0"
              type="line" 
              :data="memoryChartData" 
              :options="memoryChartOptions"
              height="240px"
            />
            <div v-else class="text-center py-12 text-light-500">
              No memory data available
            </div>
          </div>
        </div>
      </div>

      <!-- Database & Thread Pool -->
      <div class="grid grid-cols-1 lg:grid-cols-2 gap-4 sm:gap-6">
        <div class="card">
          <div class="card-header">
            <h3 class="font-semibold text-light-900 dark:text-white">Database Pool</h3>
          </div>
          <div class="card-body">
            <BaseChart 
              v-if="databaseChartData.labels.length > 0"
              type="line" 
              :data="databaseChartData" 
              :options="poolChartOptions"
              height="200px"
            />
            <div v-else class="text-center py-12 text-light-500">
              No database pool data available
            </div>
          </div>
        </div>
        
        <div class="card">
          <div class="card-header">
            <h3 class="font-semibold text-light-900 dark:text-white">Thread Pool</h3>
          </div>
          <div class="card-body">
            <BaseChart 
              v-if="threadPoolChartData.labels.length > 0"
              type="line" 
              :data="threadPoolChartData" 
              :options="queueChartOptions"
              height="200px"
            />
            <div v-else class="text-center py-12 text-light-500">
              No thread pool data available
            </div>
          </div>
        </div>
      </div>

      <!-- Stats Summary -->
      <div class="card">
        <div class="card-header">
          <h3 class="font-semibold text-light-900 dark:text-white">System Summary</h3>
        </div>
        <div class="card-body">
          <div class="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-6 gap-2 sm:gap-4">
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg">
              <p class="text-xs text-light-500 uppercase tracking-wide">Replicas</p>
              <p class="text-2xl font-bold text-light-900 dark:text-light-100 mt-1">
                {{ systemData.replicaCount || 0 }}
              </p>
            </div>
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg">
              <p class="text-xs text-light-500 uppercase tracking-wide">Data Points</p>
              <p class="text-2xl font-bold text-light-900 dark:text-light-100 mt-1">
                {{ systemData.pointCount || 0 }}
              </p>
            </div>
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg">
              <p class="text-xs text-light-500 uppercase tracking-wide">Bucket Size</p>
              <p class="text-2xl font-bold text-light-900 dark:text-light-100 mt-1">
                {{ formatBucketSize(systemData.bucketMinutes) }}
              </p>
            </div>
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg">
              <p class="text-xs text-light-500 uppercase tracking-wide">Latest CPU</p>
              <p class="text-2xl font-bold text-rose-600 dark:text-rose-400 mt-1">
                {{ formatCPU(lastMetrics?.cpu?.user_us?.last) }}
              </p>
            </div>
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg">
              <p class="text-xs text-light-500 uppercase tracking-wide">Latest Memory</p>
              <p class="text-2xl font-bold text-purple-600 dark:text-purple-400 mt-1">
                {{ formatMemory(lastMetrics?.memory?.rss_bytes?.last) }}
              </p>
            </div>
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg">
              <p class="text-xs text-light-500 uppercase tracking-wide">DB Active</p>
              <p class="text-2xl font-bold text-light-900 dark:text-light-100 mt-1">
                {{ lastMetrics?.database?.pool_active?.last || 0 }}
              </p>
            </div>
          </div>
        </div>
      </div>

      <!-- Per-Server Stats (when in individual mode) -->
      <div v-if="viewMode === 'individual' && systemData?.replicas?.length > 1" class="card">
        <div class="card-header">
          <h3 class="font-semibold text-light-900 dark:text-white">Server Details</h3>
        </div>
        <div class="card-body">
          <div class="overflow-x-auto">
            <table class="w-full text-sm">
              <thead>
                <tr class="border-b border-light-200 dark:border-dark-50">
                  <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Hostname</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Port</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">CPU (User)</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">CPU (Sys)</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Memory</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">DB Pool</th>
                </tr>
              </thead>
              <tbody>
                <tr 
                  v-for="replica in systemData.replicas" 
                  :key="`${replica.hostname}:${replica.port}`"
                  class="border-b border-light-100 dark:border-dark-100"
                >
                  <td class="px-3 py-2 font-medium">{{ replica.hostname }}</td>
                  <td class="px-3 py-2 text-right text-light-600 dark:text-light-400">{{ replica.port }}</td>
                  <td class="px-3 py-2 text-right text-rose-600 dark:text-rose-400">
                    {{ formatCPU(getLastMetricForReplica(replica)?.cpu?.user_us?.last) }}
                  </td>
                  <td class="px-3 py-2 text-right text-amber-600 dark:text-amber-400">
                    {{ formatCPU(getLastMetricForReplica(replica)?.cpu?.system_us?.last) }}
                  </td>
                  <td class="px-3 py-2 text-right text-purple-600 dark:text-purple-400">
                    {{ formatMemory(getLastMetricForReplica(replica)?.memory?.rss_bytes?.last) }}
                  </td>
                  <td class="px-3 py-2 text-right">
                    {{ getLastMetricForReplica(replica)?.database?.pool_active?.last || 0 }}/{{ getLastMetricForReplica(replica)?.database?.pool_size?.last || 0 }}
                  </td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>
    </template>

    <!-- Postgres Stats View -->
    <template v-else-if="dataSource === 'postgres' && postgresData">
      <!-- Cache Hit Ratios Summary -->
      <div class="card">
        <div class="card-header flex items-center justify-between">
          <h3 class="font-semibold text-light-900 dark:text-white">Cache Performance</h3>
          <span class="text-xs text-light-500">{{ postgresData.database }}</span>
        </div>
        <div class="card-body">
          <div class="grid grid-cols-2 sm:grid-cols-4 gap-4">
            <!-- Database-level cache -->
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg">
              <p class="text-xs text-light-500 uppercase tracking-wide">Database Hit Ratio</p>
              <p class="text-2xl font-bold mt-1" :class="getCacheRatioClass(postgresData.databaseCache?.cacheHitRatio)">
                {{ postgresData.databaseCache?.cacheHitRatio || 0 }}%
              </p>
            </div>
            <!-- Table cache -->
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg">
              <p class="text-xs text-light-500 uppercase tracking-wide">Table Hit Ratio</p>
              <p class="text-2xl font-bold mt-1" :class="getCacheRatioClass(postgresData.cacheSummary?.tables?.hitRatio)">
                {{ postgresData.cacheSummary?.tables?.hitRatio || 0 }}%
              </p>
            </div>
            <!-- Index cache -->
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg">
              <p class="text-xs text-light-500 uppercase tracking-wide">Index Hit Ratio</p>
              <p class="text-2xl font-bold mt-1" :class="getCacheRatioClass(postgresData.cacheSummary?.indexes?.hitRatio)">
                {{ postgresData.cacheSummary?.indexes?.hitRatio || 0 }}%
              </p>
            </div>
            <!-- Shared buffers -->
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg">
              <p class="text-xs text-light-500 uppercase tracking-wide">Shared Buffers</p>
              <p class="text-2xl font-bold text-light-900 dark:text-light-100 mt-1">
                {{ postgresData.bufferConfig?.sharedBuffersSize || 'N/A' }}
              </p>
            </div>
          </div>
        </div>
      </div>

      <!-- Table Cache Details -->
      <div class="card">
        <div class="card-header">
          <h3 class="font-semibold text-light-900 dark:text-white">Table Cache Stats</h3>
          <p class="text-xs text-light-500 mt-0.5">Cache hit ratios per table in queen schema</p>
        </div>
        <div class="card-body">
          <div class="overflow-x-auto">
            <table class="w-full text-sm">
              <thead>
                <tr class="border-b border-light-200 dark:border-dark-50">
                  <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Table</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Disk Reads</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Cache Hits</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Hit Ratio</th>
                </tr>
              </thead>
              <tbody>
                <tr 
                  v-for="table in postgresData.tableCache" 
                  :key="table.table"
                  class="border-b border-light-100 dark:border-dark-100"
                >
                  <td class="px-3 py-2 font-medium font-mono text-sm">{{ table.table }}</td>
                  <td class="px-3 py-2 text-right text-light-600 dark:text-light-400">{{ formatNumber(table.diskReads) }}</td>
                  <td class="px-3 py-2 text-right text-light-600 dark:text-light-400">{{ formatNumber(table.cacheHits) }}</td>
                  <td class="px-3 py-2 text-right">
                    <span :class="getCacheRatioClass(table.cacheHitRatio)">
                      {{ table.cacheHitRatio || 0 }}%
                    </span>
                  </td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>

      <!-- Index Cache Details -->
      <div class="card">
        <div class="card-header">
          <h3 class="font-semibold text-light-900 dark:text-white">Index Cache Stats</h3>
          <p class="text-xs text-light-500 mt-0.5">Cache hit ratios per index (top 20 by disk reads)</p>
        </div>
        <div class="card-body">
          <div class="overflow-x-auto">
            <table class="w-full text-sm">
              <thead>
                <tr class="border-b border-light-200 dark:border-dark-50">
                  <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Index</th>
                  <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Table</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Disk Reads</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Cache Hits</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Hit Ratio</th>
                </tr>
              </thead>
              <tbody>
                <tr 
                  v-for="idx in postgresData.indexCache" 
                  :key="idx.index"
                  class="border-b border-light-100 dark:border-dark-100"
                >
                  <td class="px-3 py-2 font-mono text-xs">{{ idx.index }}</td>
                  <td class="px-3 py-2 text-light-600 dark:text-light-400">{{ idx.table }}</td>
                  <td class="px-3 py-2 text-right text-light-600 dark:text-light-400">{{ formatNumber(idx.diskReads) }}</td>
                  <td class="px-3 py-2 text-right text-light-600 dark:text-light-400">{{ formatNumber(idx.cacheHits) }}</td>
                  <td class="px-3 py-2 text-right">
                    <span :class="getCacheRatioClass(idx.cacheHitRatio)">
                      {{ idx.cacheHitRatio || 0 }}%
                    </span>
                  </td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>

      <!-- Buffer Usage -->
      <div v-if="postgresData.bufferUsage?.length" class="card">
        <div class="card-header">
          <h3 class="font-semibold text-light-900 dark:text-white">Buffer Cache Contents</h3>
          <p class="text-xs text-light-500 mt-0.5">What's currently cached in shared_buffers</p>
        </div>
        <div class="card-body">
          <div class="overflow-x-auto">
            <table class="w-full text-sm">
              <thead>
                <tr class="border-b border-light-200 dark:border-dark-50">
                  <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Object</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Buffered Size</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">% of Cache</th>
                </tr>
              </thead>
              <tbody>
                <tr 
                  v-for="buf in postgresData.bufferUsage" 
                  :key="buf.object"
                  class="border-b border-light-100 dark:border-dark-100"
                >
                  <td class="px-3 py-2 font-mono text-sm">{{ buf.object }}</td>
                  <td class="px-3 py-2 text-right text-light-600 dark:text-light-400">{{ buf.bufferedSize }}</td>
                  <td class="px-3 py-2 text-right">
                    <div class="flex items-center justify-end gap-2">
                      <div class="w-16 h-2 bg-light-200 dark:bg-dark-200 rounded-full overflow-hidden">
                        <div 
                          class="h-full bg-indigo-500 rounded-full" 
                          :style="{ width: `${Math.min(buf.percentOfCache, 100)}%` }"
                        />
                      </div>
                      <span class="text-xs text-light-600 dark:text-light-400 w-12 text-right">
                        {{ buf.percentOfCache }}%
                      </span>
                    </div>
                  </td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>

      <!-- Table Sizes -->
      <div class="card">
        <div class="card-header">
          <h3 class="font-semibold text-light-900 dark:text-white">Table Sizes</h3>
          <p class="text-xs text-light-500 mt-0.5">Storage usage per table</p>
        </div>
        <div class="card-body">
          <div class="overflow-x-auto">
            <table class="w-full text-sm">
              <thead>
                <tr class="border-b border-light-200 dark:border-dark-50">
                  <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Table</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Total Size</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Table Size</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Index Size</th>
                </tr>
              </thead>
              <tbody>
                <tr 
                  v-for="tbl in postgresData.tableSizes" 
                  :key="tbl.table"
                  class="border-b border-light-100 dark:border-dark-100"
                >
                  <td class="px-3 py-2 font-mono text-sm font-medium">{{ tbl.table }}</td>
                  <td class="px-3 py-2 text-right font-medium text-light-900 dark:text-light-100">{{ tbl.totalSize }}</td>
                  <td class="px-3 py-2 text-right text-light-600 dark:text-light-400">{{ tbl.tableSize }}</td>
                  <td class="px-3 py-2 text-right text-indigo-600 dark:text-indigo-400">{{ tbl.indexSize }}</td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>

      <!-- Dead Tuples & HOT Updates (side by side) -->
      <div class="grid grid-cols-1 lg:grid-cols-2 gap-4 sm:gap-6">
        <!-- Dead Tuples -->
        <div class="card">
          <div class="card-header flex items-center justify-between">
            <div>
              <h3 class="font-semibold text-light-900 dark:text-white">Dead Tuples</h3>
              <p class="text-xs text-light-500 mt-0.5">Tables needing vacuum</p>
            </div>
            <span v-if="postgresData.deadTuples?.length" class="badge badge-warning">
              {{ postgresData.deadTuples.length }} tables
            </span>
          </div>
          <div class="card-body">
            <div v-if="postgresData.deadTuples?.length" class="overflow-x-auto">
              <table class="w-full text-sm">
                <thead>
                  <tr class="border-b border-light-200 dark:border-dark-50">
                    <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Table</th>
                    <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Dead</th>
                    <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Dead %</th>
                    <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Last Vacuum</th>
                  </tr>
                </thead>
                <tbody>
                  <tr 
                    v-for="tbl in postgresData.deadTuples" 
                    :key="tbl.table"
                    class="border-b border-light-100 dark:border-dark-100"
                  >
                    <td class="px-3 py-2 font-mono text-xs">{{ tbl.table }}</td>
                    <td class="px-3 py-2 text-right text-amber-600 dark:text-amber-400">{{ formatNumber(tbl.deadTuples) }}</td>
                    <td class="px-3 py-2 text-right">
                      <span :class="tbl.deadPercentage > 10 ? 'text-rose-600 dark:text-rose-400' : 'text-light-600 dark:text-light-400'">
                        {{ tbl.deadPercentage || 0 }}%
                      </span>
                    </td>
                    <td class="px-3 py-2 text-xs text-light-500">
                      {{ formatTimestamp(tbl.lastAutovacuum || tbl.lastVacuum) }}
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
            <div v-else class="text-center py-8 text-light-500">
              No dead tuples - tables are clean! ✓
            </div>
          </div>
        </div>

        <!-- HOT Updates -->
        <div class="card">
          <div class="card-header">
            <h3 class="font-semibold text-light-900 dark:text-white">HOT Update Efficiency</h3>
            <p class="text-xs text-light-500 mt-0.5">Heap-Only Tuple updates (higher is better)</p>
          </div>
          <div class="card-body">
            <div v-if="postgresData.hotUpdates?.length" class="overflow-x-auto">
              <table class="w-full text-sm">
                <thead>
                  <tr class="border-b border-light-200 dark:border-dark-50">
                    <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Table</th>
                    <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Updates</th>
                    <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">HOT %</th>
                  </tr>
                </thead>
                <tbody>
                  <tr 
                    v-for="tbl in postgresData.hotUpdates" 
                    :key="tbl.table"
                    class="border-b border-light-100 dark:border-dark-100"
                  >
                    <td class="px-3 py-2 font-mono text-xs">{{ tbl.table }}</td>
                    <td class="px-3 py-2 text-right text-light-600 dark:text-light-400">{{ formatNumber(tbl.totalUpdates) }}</td>
                    <td class="px-3 py-2 text-right">
                      <span :class="getHotRatioClass(tbl.hotUpdatePercentage)">
                        {{ tbl.hotUpdatePercentage || 0 }}%
                      </span>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
            <div v-else class="text-center py-8 text-light-500">
              No updates recorded yet
            </div>
          </div>
        </div>
      </div>

      <!-- Active Queries -->
      <div class="card">
        <div class="card-header flex items-center justify-between">
          <div>
            <h3 class="font-semibold text-light-900 dark:text-white">Active Queries</h3>
            <p class="text-xs text-light-500 mt-0.5">Queries running longer than 1 second</p>
          </div>
          <span v-if="postgresData.activeQueries?.length" class="badge badge-danger">
            {{ postgresData.activeQueries.length }} slow
          </span>
        </div>
        <div class="card-body">
          <div v-if="postgresData.activeQueries?.length" class="space-y-3">
            <div 
              v-for="query in postgresData.activeQueries" 
              :key="query.pid"
              class="p-3 bg-light-100 dark:bg-dark-300 rounded-lg"
            >
              <div class="flex items-center justify-between mb-2">
                <span class="text-xs font-medium text-light-600 dark:text-light-400">
                  PID: {{ query.pid }} · {{ query.state }}
                </span>
                <span class="text-xs font-mono" :class="query.duration > 10 ? 'text-rose-600 dark:text-rose-400' : 'text-amber-600 dark:text-amber-400'">
                  {{ formatDurationSeconds(query.duration) }}
                </span>
              </div>
              <code class="text-xs text-light-700 dark:text-light-300 block break-all">
                {{ query.query }}
              </code>
              <div v-if="query.waitEventType" class="mt-2 text-xs text-light-500">
                Wait: {{ query.waitEventType }} / {{ query.waitEvent }}
              </div>
            </div>
          </div>
          <div v-else class="text-center py-8 text-emerald-600 dark:text-emerald-400">
            No slow queries running ✓
          </div>
        </div>
      </div>

      <!-- Autovacuum Status -->
      <div v-if="postgresData.autovacuumStatus?.length" class="card">
        <div class="card-header flex items-center justify-between">
          <div>
            <h3 class="font-semibold text-light-900 dark:text-white">Autovacuum Status</h3>
            <p class="text-xs text-light-500 mt-0.5">Tables with >1000 dead tuples</p>
          </div>
          <span class="badge badge-warning">{{ postgresData.autovacuumStatus.length }} pending</span>
        </div>
        <div class="card-body">
          <div class="overflow-x-auto">
            <table class="w-full text-sm">
              <thead>
                <tr class="border-b border-light-200 dark:border-dark-50">
                  <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Table</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Dead Tuples</th>
                  <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Vacuum Count</th>
                  <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Last Autovacuum</th>
                </tr>
              </thead>
              <tbody>
                <tr 
                  v-for="tbl in postgresData.autovacuumStatus" 
                  :key="tbl.table"
                  class="border-b border-light-100 dark:border-dark-100"
                >
                  <td class="px-3 py-2 font-mono text-sm">{{ tbl.table }}</td>
                  <td class="px-3 py-2 text-right text-amber-600 dark:text-amber-400">{{ formatNumber(tbl.deadTuples) }}</td>
                  <td class="px-3 py-2 text-right text-light-600 dark:text-light-400">{{ tbl.autovacuumCount }}</td>
                  <td class="px-3 py-2 text-xs text-light-500">{{ formatTimestamp(tbl.lastAutovacuum) }}</td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>
    </template>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted } from 'vue'
import { system } from '@/api'
import { useRefresh } from '@/composables/useRefresh'
import BaseChart from '@/components/BaseChart.vue'

// State
const loading = ref(true)
const dataSource = ref('worker')
const viewMode = ref('aggregate')
const aggregationType = ref('avg')
const timeRange = ref(60)
const customMode = ref(false)
const customFrom = ref('')
const customTo = ref('')
const workerData = ref(null)
const systemData = ref(null)
const postgresData = ref(null)

const timeRanges = [
  { label: '15m', value: 15 },
  { label: '1h', value: 60 },
  { label: '6h', value: 360 },
  { label: '24h', value: 1440 }
]

// Format date to datetime-local input format
const formatDateTimeLocal = (date) => {
  const year = date.getFullYear()
  const month = String(date.getMonth() + 1).padStart(2, '0')
  const day = String(date.getDate()).padStart(2, '0')
  const hours = String(date.getHours()).padStart(2, '0')
  const minutes = String(date.getMinutes()).padStart(2, '0')
  return `${year}-${month}-${day}T${hours}:${minutes}`
}

// Format milliseconds to appropriate unit
const formatDuration = (ms) => {
  if (ms === undefined || ms === null) return '0ms'
  if (ms < 1000) return `${Math.round(ms)}ms`
  if (ms < 60000) return `${(ms / 1000).toFixed(1)}s`
  return `${(ms / 60000).toFixed(1)}m`
}

const aggregationTypes = [
  { label: 'Average', value: 'avg' },
  { label: 'Maximum', value: 'max' },
  { label: 'Minimum', value: 'min' }
]

// Metric toggles for throughput
const throughputMetrics = [
  { key: 'push', label: 'Push', activeClass: 'bg-orange-100 dark:bg-orange-900/30 text-orange-700 dark:text-orange-300', dotClass: 'bg-orange-500' },
  { key: 'pop', label: 'Pop', activeClass: 'bg-indigo-100 dark:bg-indigo-900/30 text-indigo-700 dark:text-indigo-300', dotClass: 'bg-indigo-500' },
  { key: 'ack', label: 'Ack', activeClass: 'bg-emerald-100 dark:bg-emerald-900/30 text-emerald-700 dark:text-emerald-300', dotClass: 'bg-emerald-500' }
]
const selectedThroughputMetrics = reactive({ push: true, pop: true, ack: true })

// Metric toggles for event loop
const eventLoopMetrics = [
  { key: 'avg', label: 'Avg Event Loop', activeClass: 'bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-300', dotClass: 'bg-blue-500' },
  { key: 'max', label: 'Max Event Loop', activeClass: 'bg-rose-100 dark:bg-rose-900/30 text-rose-700 dark:text-rose-300', dotClass: 'bg-rose-500' }
]
const selectedEventLoopMetrics = reactive({ avg: true, max: true })

// Metric toggles for errors
const errorMetrics = [
  { key: 'dbErrors', label: 'DB Errors', activeClass: 'bg-rose-100 dark:bg-rose-900/30 text-rose-700 dark:text-rose-300', dotClass: 'bg-rose-500' },
  { key: 'ackFailed', label: 'Ack Failed', activeClass: 'bg-orange-100 dark:bg-orange-900/30 text-orange-700 dark:text-orange-300', dotClass: 'bg-orange-500' },
  { key: 'dlq', label: 'DLQ', activeClass: 'bg-amber-100 dark:bg-amber-900/30 text-amber-700 dark:text-amber-300', dotClass: 'bg-amber-500' }
]
const selectedErrorMetrics = reactive({ dbErrors: true, ackFailed: true, dlq: true })

const chartOptions = {
  plugins: {
    legend: {
      display: false
    }
  }
}

// Chart options for latency charts with proper duration formatting
const lagChartOptions = {
  plugins: {
    legend: { display: false },
    tooltip: {
      callbacks: {
        label: (ctx) => ` ${ctx.dataset.label}: ${formatDuration(ctx.parsed.y)}`
      }
    }
  },
  scales: {
    y: {
      beginAtZero: true,
      min: 0,
      title: {
        display: true,
        text: 'Latency',
        font: { size: 11 }
      },
      ticks: {
        callback: (value) => formatDuration(value)
      }
    }
  }
}

// Chart options with Y-axis titles
const throughputChartOptions = {
  plugins: { legend: { display: false } },
  scales: {
    y: {
      title: { display: true, text: 'Operations/s', font: { size: 11 } }
    }
  }
}

const cpuChartOptions = {
  plugins: { legend: { display: false } },
  scales: {
    y: {
      title: { display: true, text: 'CPU %', font: { size: 11 } },
      ticks: {
        callback: (value) => value.toFixed(1) + '%'
      }
    }
  }
}

const memoryChartOptions = {
  plugins: { legend: { display: false } },
  scales: {
    y: {
      title: { display: true, text: 'Memory (MB)', font: { size: 11 } }
    }
  }
}

const poolChartOptions = {
  plugins: { legend: { display: false } },
  scales: {
    y: {
      title: { display: true, text: 'Connections', font: { size: 11 } }
    }
  }
}

const queueChartOptions = {
  plugins: { legend: { display: false } },
  scales: {
    y: {
      title: { display: true, text: 'Queue Size', font: { size: 11 } }
    }
  }
}

const errorsChartOptions = {
  plugins: { legend: { display: false } },
  scales: {
    x: { stacked: true },
    y: {
      stacked: true,
      beginAtZero: true,
      min: 0,
      title: { display: true, text: 'Errors', font: { size: 11 } }
    }
  }
}

// Toggle functions
const toggleThroughputMetric = (key) => { selectedThroughputMetrics[key] = !selectedThroughputMetrics[key] }
const toggleEventLoopMetric = (key) => { selectedEventLoopMetrics[key] = !selectedEventLoopMetrics[key] }
const toggleErrorMetric = (key) => { selectedErrorMetrics[key] = !selectedErrorMetrics[key] }

// Time range functions
const selectQuickRange = (value) => {
  customMode.value = false
  timeRange.value = value
  fetchData()
}

const toggleCustomMode = () => {
  customMode.value = !customMode.value
  if (customMode.value) {
    // Initialize with current range when entering custom mode
    const now = new Date()
    const from = new Date(now.getTime() - timeRange.value * 60 * 1000)
    customTo.value = formatDateTimeLocal(now)
    customFrom.value = formatDateTimeLocal(from)
  }
}

const applyCustomRange = () => {
  if (!customFrom.value || !customTo.value) return
  const fromDate = new Date(customFrom.value)
  const toDate = new Date(customTo.value)
  if (fromDate >= toDate) return
  fetchData()
}

// Format chart label based on time span
const formatChartLabel = (date, isMultiDay) => {
  if (isMultiDay) {
    return date.toLocaleString('en-US', { 
      month: 'short', 
      day: 'numeric', 
      hour: '2-digit', 
      minute: '2-digit' 
    })
  }
  return date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' })
}

// Check if time series spans multiple days
const isMultiDay = (timeSeries) => {
  if (!timeSeries || timeSeries.length < 2) return false
  const firstDate = new Date(timeSeries[0].timestamp)
  const lastDate = new Date(timeSeries[timeSeries.length - 1].timestamp)
  return firstDate.toDateString() !== lastDate.toDateString()
}

// Computed
const lastMetrics = computed(() => {
  if (!systemData.value?.replicas?.length) return null
  const firstReplica = systemData.value.replicas[0]
  if (!firstReplica?.timeSeries?.length) return null
  return firstReplica.timeSeries[firstReplica.timeSeries.length - 1]?.metrics
})

const totalErrors = computed(() => {
  if (!workerData.value?.timeSeries?.length) return 0
  return workerData.value.timeSeries.reduce((sum, t) => {
    return sum + (t.dbErrors || 0) + (t.ackFailed || 0) + (t.dlqCount || 0)
  }, 0)
})

// Worker Chart Data
const throughputChartData = computed(() => {
  if (!workerData.value?.timeSeries?.length) return { labels: [], datasets: [] }
  
  const ts = [...workerData.value.timeSeries].reverse()
  const multiDay = isMultiDay(ts)
  const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multiDay))
  
  const datasets = []
  
  if (selectedThroughputMetrics.push) {
    datasets.push({
      label: 'Push/s',
      data: ts.map(t => t.pushPerSecond || 0),
      borderColor: 'rgba(255, 107, 0, 0.9)',
      backgroundColor: 'rgba(255, 107, 0, 0.1)',
      fill: true,
      tension: 0
    })
  }
  
  if (selectedThroughputMetrics.pop) {
    datasets.push({
      label: 'Pop/s',
      data: ts.map(t => t.popPerSecond || 0),
      borderColor: 'rgba(99, 102, 241, 0.9)',
      backgroundColor: 'rgba(99, 102, 241, 0.1)',
      fill: true,
      tension: 0
    })
  }
  
  if (selectedThroughputMetrics.ack) {
    datasets.push({
      label: 'Ack/s',
      data: ts.map(t => t.ackPerSecond || 0),
      borderColor: 'rgba(16, 185, 129, 0.9)',
      backgroundColor: 'rgba(16, 185, 129, 0.1)',
      fill: true,
      tension: 0
    })
  }
  
  return { labels, datasets }
})

const latencyChartData = computed(() => {
  if (!workerData.value?.timeSeries?.length) return { labels: [], datasets: [] }
  
  const ts = [...workerData.value.timeSeries].reverse()
  const multiDay = isMultiDay(ts)
  const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multiDay))
  
  return {
    labels,
    datasets: [
      { 
        label: 'Avg Lag (ms)', 
        data: ts.map(t => t.avgLagMs || 0), 
        borderColor: 'rgba(139, 92, 246, 0.9)',
        fill: false,
        tension: 0
      },
      { 
        label: 'Max Lag (ms)', 
        data: ts.map(t => t.maxLagMs || 0), 
        borderColor: 'rgba(244, 63, 94, 0.9)',
        fill: false,
        tension: 0
      }
    ]
  }
})

const eventLoopChartData = computed(() => {
  if (!workerData.value?.timeSeries?.length) return { labels: [], datasets: [] }
  
  const ts = [...workerData.value.timeSeries].reverse()
  const multiDay = isMultiDay(ts)
  const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multiDay))
  
  const datasets = []
  
  if (selectedEventLoopMetrics.avg) {
    datasets.push({
      label: 'Avg Event Loop (ms)',
      data: ts.map(t => t.avgEventLoopLagMs || 0),
      borderColor: 'rgba(59, 130, 246, 0.9)',
      backgroundColor: 'rgba(59, 130, 246, 0.1)',
      fill: true,
      tension: 0
    })
  }
  
  if (selectedEventLoopMetrics.max) {
    datasets.push({
      label: 'Max Event Loop (ms)',
      data: ts.map(t => t.maxEventLoopLagMs || 0),
      borderColor: 'rgba(239, 68, 68, 0.9)',
      backgroundColor: 'rgba(239, 68, 68, 0.05)',
      fill: true,
      tension: 0
    })
  }
  
  return { labels, datasets }
})

const connectionPoolChartData = computed(() => {
  if (!workerData.value?.timeSeries?.length) return { labels: [], datasets: [] }
  
  const ts = [...workerData.value.timeSeries].reverse()
  const multiDay = isMultiDay(ts)
  const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multiDay))
  
  return {
    labels,
    datasets: [
      { 
        label: 'Free Slots', 
        data: ts.map(t => t.avgFreeSlots || 0), 
        borderColor: 'rgba(6, 182, 212, 0.9)',
        backgroundColor: 'rgba(6, 182, 212, 0.1)',
        fill: true,
        tension: 0
      },
      { 
        label: 'DB Connections', 
        data: ts.map(t => t.dbConnections || 0), 
        borderColor: 'rgba(245, 158, 11, 0.9)',
        fill: false,
        tension: 0
      }
    ]
  }
})

const errorsChartData = computed(() => {
  if (!workerData.value?.timeSeries?.length) return { labels: [], datasets: [] }
  
  const ts = [...workerData.value.timeSeries].reverse()
  const multiDay = isMultiDay(ts)
  const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multiDay))
  
  const datasets = []
  
  if (selectedErrorMetrics.dbErrors) {
    datasets.push({
      label: 'DB Errors',
      data: ts.map(t => t.dbErrors || 0),
      backgroundColor: 'rgba(239, 68, 68, 0.7)',
      borderColor: 'rgba(239, 68, 68, 1)',
      borderWidth: 1
    })
  }
  
  if (selectedErrorMetrics.ackFailed) {
    datasets.push({
      label: 'Ack Failed',
      data: ts.map(t => t.ackFailed || 0),
      backgroundColor: 'rgba(255, 107, 0, 0.7)',
      borderColor: 'rgba(255, 107, 0, 1)',
      borderWidth: 1
    })
  }
  
  if (selectedErrorMetrics.dlq) {
    datasets.push({
      label: 'DLQ',
      data: ts.map(t => t.dlqCount || 0),
      backgroundColor: 'rgba(245, 158, 11, 0.7)',
      borderColor: 'rgba(245, 158, 11, 1)',
      borderWidth: 1
    })
  }
  
  return { labels, datasets }
})

// System Chart Data
const cpuChartData = computed(() => {
  if (!systemData.value?.replicas?.length) return { labels: [], datasets: [] }
  
  const agg = aggregationType.value
  
  if (viewMode.value === 'individual') {
    // Show each server as a separate line
    const datasets = []
    const firstReplica = systemData.value.replicas[0]
    const ts = firstReplica?.timeSeries || []
    const multiDay = isMultiDay(ts)
    const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multiDay))
    
    const colors = ['#f43f5e', '#3b82f6', '#10b981', '#f59e0b', '#8b5cf6']
    
    systemData.value.replicas.forEach((replica, i) => {
      const color = colors[i % colors.length]
      datasets.push({
        label: `${replica.hostname} (User)`,
        data: (replica.timeSeries || []).map(t => (t.metrics?.cpu?.user_us?.[agg] || 0) / 100),
        borderColor: color,
        fill: false,
        tension: 0
      })
    })
    
    return { labels, datasets }
  } else {
    // Aggregate all servers
    const ts = systemData.value.replicas[0]?.timeSeries || []
    const multiDay = isMultiDay(ts)
    const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multiDay))
    
    return {
      labels,
      datasets: [
        { 
          label: 'User CPU (%)', 
          data: ts.map(t => (t.metrics?.cpu?.user_us?.[agg] || 0) / 100), 
          borderColor: 'rgba(244, 63, 94, 0.9)',
          backgroundColor: 'rgba(244, 63, 94, 0.1)',
          fill: true,
          tension: 0
        },
        { 
          label: 'System CPU (%)', 
          data: ts.map(t => (t.metrics?.cpu?.system_us?.[agg] || 0) / 100), 
          borderColor: 'rgba(245, 158, 11, 0.9)',
          backgroundColor: 'rgba(245, 158, 11, 0.1)',
          fill: true,
          tension: 0
        }
      ]
    }
  }
})

const memoryChartData = computed(() => {
  if (!systemData.value?.replicas?.length) return { labels: [], datasets: [] }
  
  const agg = aggregationType.value
  
  if (viewMode.value === 'individual') {
    const datasets = []
    const firstReplica = systemData.value.replicas[0]
    const ts = firstReplica?.timeSeries || []
    const multiDay = isMultiDay(ts)
    const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multiDay))
    
    const colors = ['#8b5cf6', '#3b82f6', '#10b981', '#f59e0b', '#f43f5e']
    
    systemData.value.replicas.forEach((replica, i) => {
      const color = colors[i % colors.length]
      datasets.push({
        label: replica.hostname,
        data: (replica.timeSeries || []).map(t => Math.round((t.metrics?.memory?.rss_bytes?.[agg] || 0) / 1024 / 1024)),
        borderColor: color,
        fill: false,
        tension: 0
      })
    })
    
    return { labels, datasets }
  } else {
    const ts = systemData.value.replicas[0]?.timeSeries || []
    const multiDay = isMultiDay(ts)
    const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multiDay))
    
    return {
      labels,
      datasets: [
        { 
          label: 'RSS (MB)', 
          data: ts.map(t => Math.round((t.metrics?.memory?.rss_bytes?.[agg] || 0) / 1024 / 1024)), 
          borderColor: 'rgba(139, 92, 246, 0.9)',
          backgroundColor: 'rgba(139, 92, 246, 0.1)',
          fill: true,
          tension: 0
        }
      ]
    }
  }
})

const databaseChartData = computed(() => {
  if (!systemData.value?.replicas?.length) return { labels: [], datasets: [] }
  
  const agg = aggregationType.value
  const ts = systemData.value.replicas[0]?.timeSeries || []
  const multiDay = isMultiDay(ts)
  const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multiDay))
  
  return {
    labels,
    datasets: [
      { 
        label: 'Active', 
        data: ts.map(t => t.metrics?.database?.pool_active?.[agg] || 0), 
        borderColor: 'rgba(6, 182, 212, 0.9)',
        backgroundColor: 'rgba(6, 182, 212, 0.1)',
        fill: true,
        tension: 0
      },
      { 
        label: 'Idle', 
        data: ts.map(t => t.metrics?.database?.pool_idle?.[agg] || 0), 
        borderColor: 'rgba(16, 185, 129, 0.9)',
        backgroundColor: 'rgba(16, 185, 129, 0.1)',
        fill: true,
        tension: 0
      }
    ]
  }
})

const threadPoolChartData = computed(() => {
  if (!systemData.value?.replicas?.length) return { labels: [], datasets: [] }
  
  const agg = aggregationType.value
  const ts = systemData.value.replicas[0]?.timeSeries || []
  const multiDay = isMultiDay(ts)
  const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multiDay))
  
  return {
    labels,
    datasets: [
      { 
        label: 'DB Queue', 
        data: ts.map(t => t.metrics?.threadpool?.db?.queue_size?.[agg] || 0), 
        borderColor: 'rgba(99, 102, 241, 0.9)',
        backgroundColor: 'rgba(99, 102, 241, 0.1)',
        fill: true,
        tension: 0
      },
      { 
        label: 'System Queue', 
        data: ts.map(t => t.metrics?.threadpool?.system?.queue_size?.[agg] || 0), 
        borderColor: 'rgba(245, 158, 11, 0.9)',
        backgroundColor: 'rgba(245, 158, 11, 0.1)',
        fill: true,
        tension: 0
      }
    ]
  }
})

// Methods
const formatCPU = (value) => {
  if (value === undefined || value === null) return '0%'
  return ((value / 100).toFixed(1)) + '%'
}

const formatMemory = (value) => {
  if (value === undefined || value === null) return '0 MB'
  return Math.round(value / 1024 / 1024) + ' MB'
}

const formatBucketSize = (minutes) => {
  if (!minutes) return '1 min'
  if (minutes === 1) return '1 min'
  if (minutes < 60) return `${minutes} min`
  const hours = Math.floor(minutes / 60)
  const remainingMinutes = minutes % 60
  if (remainingMinutes === 0) return `${hours}h`
  return `${hours}h ${remainingMinutes}m`
}

const formatTime = (timestamp) => {
  if (!timestamp) return '-'
  return new Date(timestamp).toLocaleTimeString('en-US', { 
    hour: '2-digit', 
    minute: '2-digit', 
    second: '2-digit' 
  })
}

// Postgres stats helpers
const formatNumber = (num) => {
  if (num === undefined || num === null) return '0'
  if (num >= 1e12) return (num / 1e12).toFixed(2) + 'T'
  if (num >= 1e9) return (num / 1e9).toFixed(2) + 'B'
  if (num >= 1e6) return (num / 1e6).toFixed(2) + 'M'
  if (num >= 1e3) return (num / 1e3).toFixed(1) + 'K'
  return num.toString()
}

const formatTimestamp = (ts) => {
  if (!ts) return 'Never'
  const date = new Date(ts)
  const now = new Date()
  const diffMs = now - date
  const diffMins = Math.floor(diffMs / 60000)
  const diffHours = Math.floor(diffMs / 3600000)
  const diffDays = Math.floor(diffMs / 86400000)
  
  if (diffMins < 1) return 'Just now'
  if (diffMins < 60) return `${diffMins}m ago`
  if (diffHours < 24) return `${diffHours}h ago`
  if (diffDays < 7) return `${diffDays}d ago`
  return date.toLocaleDateString()
}

const formatDurationSeconds = (seconds) => {
  if (seconds < 60) return `${seconds.toFixed(1)}s`
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m ${Math.floor(seconds % 60)}s`
  return `${Math.floor(seconds / 3600)}h ${Math.floor((seconds % 3600) / 60)}m`
}

const getCacheRatioClass = (ratio) => {
  if (ratio === undefined || ratio === null) return 'text-light-500'
  if (ratio >= 99) return 'text-emerald-600 dark:text-emerald-400'
  if (ratio >= 95) return 'text-cyber-600 dark:text-cyber-400'
  if (ratio >= 90) return 'text-amber-600 dark:text-amber-400'
  return 'text-rose-600 dark:text-rose-400'
}

const getHotRatioClass = (ratio) => {
  if (ratio === undefined || ratio === null) return 'text-light-500'
  if (ratio >= 95) return 'text-emerald-600 dark:text-emerald-400'
  if (ratio >= 80) return 'text-cyber-600 dark:text-cyber-400'
  if (ratio >= 50) return 'text-amber-600 dark:text-amber-400'
  return 'text-rose-600 dark:text-rose-400'
}

const getLastMetricForReplica = (replica) => {
  if (!replica?.timeSeries?.length) return null
  return replica.timeSeries[replica.timeSeries.length - 1]?.metrics
}

const fetchData = async () => {
  loading.value = true
  
  try {
    // For postgres stats, no time range needed
    if (dataSource.value === 'postgres') {
      const res = await system.getPostgresStats()
      postgresData.value = res.data
    } else {
      // Worker and system metrics need time range
      let from, to
      
      if (customMode.value && customFrom.value && customTo.value) {
        // Use custom range
        from = new Date(customFrom.value)
        to = new Date(customTo.value)
      } else {
        // Use quick range
        const now = new Date()
        from = new Date(now.getTime() - timeRange.value * 60 * 1000)
        to = now
      }
      
      const params = {
        from: from.toISOString(),
        to: to.toISOString()
      }
      
      const [workerRes, systemRes] = await Promise.all([
        system.getWorkerMetrics(params),
        system.getSystemMetrics(params)
      ])
      
      workerData.value = workerRes.data
      systemData.value = systemRes.data
    }
  } catch (err) {
    console.error('Failed to fetch system metrics:', err)
  } finally {
    loading.value = false
  }
}

// Register for global refresh
useRefresh(fetchData)

onMounted(fetchData)
</script>
