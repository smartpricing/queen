<template>
  <div class="view-container">

    <!-- Page head -->
    <!-- Controls -->
    <div class="card" style="padding:12px 16px; margin-bottom:20px;">
      <!-- Primary row: Source (left) and Range (right) -->
      <div style="display:flex; flex-wrap:wrap; align-items:center; gap:12px 20px;">
        <div style="display:flex; align-items:center; gap:8px;">
          <span style="font-size:11px; font-weight:500; color:var(--text-low);">Source</span>
          <div class="seg">
            <button
              @click="dataSource = 'worker'; fetchData()"
              :class="{ on: dataSource === 'worker' }"
            >Queue Operations</button>
            <button
              @click="dataSource = 'system'; fetchData()"
              :class="{ on: dataSource === 'system' }"
            >System Resources</button>
            <button
              @click="dataSource = 'postgres'; fetchData()"
              :class="{ on: dataSource === 'postgres' }"
            >Postgres Stats</button>
          </div>
        </div>

        <div v-if="dataSource !== 'postgres'" style="display:flex; align-items:center; gap:8px; margin-left:auto;">
          <span style="font-size:11px; font-weight:500; color:var(--text-low);">Range</span>
          <div class="seg">
            <button
              v-for="range in timeRanges"
              :key="range.value"
              @click="selectQuickRange(range.value)"
              :class="{ on: timeRange === range.value && !customMode }"
            >{{ range.label }}</button>
            <button
              @click="toggleCustomMode"
              :class="{ on: customMode }"
            >Custom</button>
          </div>
        </div>
      </div>

      <!-- Secondary row: System-only contextual controls -->
      <div
        v-if="dataSource === 'system'"
        style="display:flex; flex-wrap:wrap; align-items:center; gap:12px 20px; padding-top:10px; margin-top:10px; border-top:1px solid var(--bd);"
      >
        <div style="display:flex; align-items:center; gap:8px;">
          <span style="font-size:11px; font-weight:500; color:var(--text-low);">View</span>
          <div class="seg">
            <button
              @click="viewMode = 'individual'"
              :class="{ on: viewMode === 'individual' }"
            >Per Server</button>
            <button
              @click="viewMode = 'aggregate'"
              :class="{ on: viewMode === 'aggregate' }"
            >Aggregate</button>
          </div>
        </div>

        <div style="display:flex; align-items:center; gap:8px;">
          <span style="font-size:11px; font-weight:500; color:var(--text-low);">Metric</span>
          <div class="seg">
            <button
              v-for="agg in aggregationTypes"
              :key="agg.value"
              @click="aggregationType = agg.value"
              :class="{ on: aggregationType === agg.value }"
            >{{ agg.label }}</button>
          </div>
        </div>
      </div>

      <!-- Custom Date/Time Range -->
      <div v-if="customMode" style="display:flex; flex-wrap:wrap; align-items:center; gap:12px; padding-top:12px; margin-top:12px; border-top:1px solid var(--bd);">
        <div style="display:flex; align-items:center; gap:8px;">
          <label style="font-size:12px; font-weight:500; color:var(--text-low); white-space:nowrap;">From:</label>
          <input
            type="datetime-local"
            v-model="customFrom"
            class="input font-mono" style="font-size:13px; padding:6px 10px; width:auto;"
          />
        </div>
        <div style="display:flex; align-items:center; gap:8px;">
          <label style="font-size:12px; font-weight:500; color:var(--text-low); white-space:nowrap;">To:</label>
          <input
            type="datetime-local"
            v-model="customTo"
            class="input font-mono" style="font-size:13px; padding:6px 10px; width:auto;"
          />
        </div>
        <button
          @click="applyCustomRange"
          class="btn btn-primary" style="font-size:12px;"
        >Apply</button>
      </div>
    </div>

    <!-- Loading -->
    <div v-if="loading">
      <div style="display:grid; grid-template-columns:repeat(2,1fr); gap:16px;">
        <div v-for="i in 4" :key="i" class="card" style="padding:24px;">
          <div class="skeleton" style="height:192px; width:100%; border-radius:8px;" />
        </div>
      </div>
    </div>

    <!-- Worker Metrics View -->
    <template v-else-if="dataSource === 'worker' && workerData">
      <!-- Throughput Chart -->
      <div class="card" style="margin-bottom:16px;">
        <div class="card-header">
          <h3>Message Throughput</h3>
          <span class="muted">{{ workerData.pointCount || 0 }} data points</span>
        </div>
        <div class="card-body">
          <div style="display:flex; align-items:center; gap:8px; flex-wrap:wrap; margin-bottom:14px;">
            <button
              v-for="metric in throughputMetrics"
              :key="metric.key"
              @click="toggleThroughputMetric(metric.key)"
              style="display:inline-flex; align-items:center; gap:6px; padding:3px 10px; border-radius:999px; font-size:11px; font-weight:500; cursor:pointer; border:1px solid var(--bd-hi); transition:.15s;"
              :class="selectedThroughputMetrics[metric.key] ? metric.activeClass : 'opacity-50'"
            >
              <span style="width:6px; height:6px; border-radius:99px;" :style="{ background: selectedThroughputMetrics[metric.key] ? metric.activeDot : 'var(--text-faint)' }" />
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
          <div v-else style="text-align:center; padding:48px 0; color:var(--text-low);">
            No throughput data available
          </div>
        </div>
      </div>

      <!-- Message Latency -->
      <div class="card" style="margin-bottom:16px;">
        <div class="card-header">
          <h3>Message Latency</h3>
          <span class="muted">time from push to pop</span>
        </div>
        <div class="card-body">
          <BaseChart
            v-if="latencyChartData.labels.length > 0"
            type="line"
            :data="latencyChartData"
            :options="lagChartOptions"
            height="240px"
          />
          <div v-else style="text-align:center; padding:48px 0; color:var(--text-low);">
            No latency data available
          </div>
        </div>
      </div>

      <!-- Event Loop & Connection Pool -->
      <div style="display:grid; grid-template-columns:1fr 1fr; gap:16px; margin-bottom:16px;">
        <div class="card">
          <div class="card-header">
            <h3>Event Loop Latency</h3>
          </div>
          <div class="card-body">
            <div style="display:flex; align-items:center; gap:8px; flex-wrap:wrap; margin-bottom:14px;">
              <button
                v-for="metric in eventLoopMetrics"
                :key="metric.key"
                @click="toggleEventLoopMetric(metric.key)"
                style="display:inline-flex; align-items:center; gap:6px; padding:3px 10px; border-radius:999px; font-size:11px; font-weight:500; cursor:pointer; border:1px solid var(--bd-hi); transition:.15s;"
                :class="selectedEventLoopMetrics[metric.key] ? metric.activeClass : 'opacity-50'"
              >
                <span style="width:6px; height:6px; border-radius:99px;" :style="{ background: selectedEventLoopMetrics[metric.key] ? metric.activeDot : 'var(--text-faint)' }" />
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
            <div v-else style="text-align:center; padding:48px 0; color:var(--text-low);">
              No event loop data available
            </div>
          </div>
        </div>

        <div class="card">
          <div class="card-header">
            <h3>Connection Pool</h3>
          </div>
          <div class="card-body">
            <BaseChart
              v-if="connectionPoolChartData.labels.length > 0"
              type="line"
              :data="connectionPoolChartData"
              :options="poolChartOptions"
              height="200px"
            />
            <div v-else style="text-align:center; padding:48px 0; color:var(--text-low);">
              No connection pool data available
            </div>
          </div>
        </div>
      </div>

      <!-- Errors Chart -->
      <div class="card" style="margin-bottom:16px;">
        <div class="card-header">
          <h3>Errors</h3>
          <span v-if="totalErrors > 0" class="chip chip-bad" style="margin-left:auto;">{{ totalErrors }} in period</span>
        </div>
        <div class="card-body">
          <div style="display:flex; align-items:center; gap:8px; flex-wrap:wrap; margin-bottom:14px;">
            <button
              v-for="metric in errorMetrics"
              :key="metric.key"
              @click="toggleErrorMetric(metric.key)"
              style="display:inline-flex; align-items:center; gap:6px; padding:3px 10px; border-radius:999px; font-size:11px; font-weight:500; cursor:pointer; border:1px solid var(--bd-hi); transition:.15s;"
              :class="selectedErrorMetrics[metric.key] ? metric.activeClass : 'opacity-50'"
            >
              <span style="width:6px; height:6px; border-radius:99px;" :style="{ background: selectedErrorMetrics[metric.key] ? metric.activeDot : 'var(--text-faint)' }" />
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
          <div v-else style="text-align:center; padding:48px 0; color:var(--text-low);">
            No error data available
          </div>
        </div>
      </div>

      <!-- Per-Queue Metrics -->
      <div class="card" style="margin-bottom:16px;">
        <div class="card-header">
          <h3>Per-Queue Metrics</h3>
        </div>
        <div class="card-body">
          <template v-if="availableQueues.length > 0">
            <div style="display:flex; align-items:center; gap:8px; margin-bottom:16px;">
              <span style="font-size:12px; color:var(--text-low); white-space:nowrap;">Filter queues:</span>
              <MultiSelect
                v-model="selectedQueues"
                :options="availableQueues"
                placeholder="All queues"
                search-placeholder="Search queues…"
              />
              <span v-if="selectedQueues.length > 0" style="font-size:12px; color:var(--text-low);">
                {{ selectedQueues.length }} of {{ availableQueues.length }}
              </span>
            </div>
            <div>
              <h4 class="label-xs" style="margin-bottom:10px;">Pop Throughput by Queue</h4>
              <BaseChart
                v-if="queuePopChartData.labels.length > 0"
                type="line"
                :data="queuePopChartData"
                :options="perQueueThroughputOptions"
                height="340px"
              />
              <div v-else style="text-align:center; padding:48px 0; color:var(--text-low);">No per-queue data available</div>
            </div>
            <div style="margin-top:20px;">
              <h4 class="label-xs" style="margin-bottom:10px;">Avg Latency by Queue</h4>
              <BaseChart
                v-if="queueLagChartData.labels.length > 0"
                type="line"
                :data="queueLagChartData"
                :options="perQueueLagOptions"
                height="340px"
              />
              <div v-else style="text-align:center; padding:48px 0; color:var(--text-low);">No per-queue data available</div>
            </div>
          </template>
          <div v-else style="text-align:center; padding:32px 0; color:var(--text-low);">
            <p>No per-queue metrics recorded yet.</p>
            <p style="font-size:12px; margin-top:4px;">Queue lag data is collected when messages are popped from queues.</p>
          </div>
        </div>
      </div>

      <!-- Workers Status Panel -->
      <div v-if="workerData?.workers?.length" class="card">
        <div class="card-header">
          <h3>Workers Status</h3>
          <span class="muted">{{ workerData.workers.length }} workers</span>
        </div>
        <div class="card-body">
          <div style="overflow-x:auto;">
            <table class="t">
              <thead>
                <tr>
                  <th>Worker ID</th>
                  <th>Hostname</th>
                  <th style="text-align:right;">Avg EL</th>
                  <th style="text-align:right;">Max EL</th>
                  <th style="text-align:right;">Free Slots</th>
                  <th style="text-align:right;">DB Conn</th>
                  <th style="text-align:right;">Job Queue</th>
                  <th>Last Seen</th>
                </tr>
              </thead>
              <tbody>
                <tr
                  v-for="worker in workerData.workers"
                  :key="worker.workerId"
                >
                  <td style="font-weight:500;">{{ worker.workerId }}</td>
                  <td style="color:var(--text-mid);">{{ worker.hostname }}</td>
                  <td style="text-align:right;">
                    <span class="font-mono tabular-nums" :style="{ color: worker.avgEventLoopLagMs > 100 ? 'var(--warn-400)' : 'var(--text-hi)' }">
                      {{ worker.avgEventLoopLagMs }}ms
                    </span>
                  </td>
                  <td style="text-align:right;">
                    <span class="font-mono tabular-nums" :style="{ color: worker.maxEventLoopLagMs > 500 ? '#f43f5e' : 'var(--text-hi)' }">
                      {{ worker.maxEventLoopLagMs }}ms
                    </span>
                  </td>
                  <td style="text-align:right;" class="font-mono tabular-nums">{{ worker.freeSlots }}</td>
                  <td style="text-align:right;" class="font-mono tabular-nums">{{ worker.dbConnections }}</td>
                  <td style="text-align:right;" class="font-mono tabular-nums">{{ worker.jobQueueSize }}</td>
                  <td style="font-size:12px; color:var(--text-low);">{{ formatTime(worker.lastSeen) }}</td>
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
      <div style="display:grid; grid-template-columns:1fr 1fr; gap:16px; margin-bottom:16px;">
        <div class="card">
          <div class="card-header">
            <h3>CPU Usage</h3>
            <span class="muted">{{ systemData.replicaCount || 0 }} replicas</span>
          </div>
          <div class="card-body">
            <BaseChart
              v-if="cpuChartData.labels.length > 0"
              type="line"
              :data="cpuChartData"
              :options="cpuChartOptions"
              height="240px"
            />
            <div v-else style="text-align:center; padding:48px 0; color:var(--text-low);">
              No CPU data available
            </div>
          </div>
        </div>

        <div class="card">
          <div class="card-header">
            <h3>Memory Usage</h3>
          </div>
          <div class="card-body">
            <BaseChart
              v-if="memoryChartData.labels.length > 0"
              type="line"
              :data="memoryChartData"
              :options="memoryChartOptions"
              height="240px"
            />
            <div v-else style="text-align:center; padding:48px 0; color:var(--text-low);">
              No memory data available
            </div>
          </div>
        </div>
      </div>

      <!-- Database & Thread Pool -->
      <div style="display:grid; grid-template-columns:1fr 1fr; gap:16px; margin-bottom:16px;">
        <div class="card">
          <div class="card-header">
            <h3>Database Pool</h3>
          </div>
          <div class="card-body">
            <BaseChart
              v-if="databaseChartData.labels.length > 0"
              type="line"
              :data="databaseChartData"
              :options="poolChartOptions"
              height="200px"
            />
            <div v-else style="text-align:center; padding:48px 0; color:var(--text-low);">
              No database pool data available
            </div>
          </div>
        </div>

        <div class="card">
          <div class="card-header">
            <h3>Thread Pool</h3>
          </div>
          <div class="card-body">
            <BaseChart
              v-if="threadPoolChartData.labels.length > 0"
              type="line"
              :data="threadPoolChartData"
              :options="queueChartOptions"
              height="200px"
            />
            <div v-else style="text-align:center; padding:48px 0; color:var(--text-low);">
              No thread pool data available
            </div>
          </div>
        </div>
      </div>

      <!-- Stats Summary -->
      <div class="card" style="margin-bottom:16px;">
        <div class="card-header">
          <h3>System Summary</h3>
        </div>
        <div class="card-body">
          <div style="display:grid; grid-template-columns:repeat(6,1fr); gap:16px;">
            <div class="stat">
              <div class="stat-label">Replicas</div>
              <div class="stat-value font-mono">{{ systemData.replicaCount || 0 }}</div>
            </div>
            <div class="stat">
              <div class="stat-label">Data Points</div>
              <div class="stat-value font-mono">{{ systemData.pointCount || 0 }}</div>
            </div>
            <div class="stat">
              <div class="stat-label">Bucket Size</div>
              <div class="stat-value font-mono">{{ formatBucketSize(systemData.bucketMinutes) }}</div>
            </div>
            <div class="stat">
              <div class="stat-label">Latest CPU</div>
              <div class="stat-value font-mono" style="color:var(--ember-500);">{{ formatCPU(lastMetrics?.cpu?.user_us?.last) }}</div>
            </div>
            <div class="stat">
              <div class="stat-label">Latest Memory</div>
              <div class="stat-value font-mono" >{{ formatMemory(lastMetrics?.memory?.rss_bytes?.last) }}</div>
            </div>
            <div class="stat">
              <div class="stat-label">DB Active</div>
              <div class="stat-value font-mono">{{ lastMetrics?.database?.pool_active?.last || 0 }}</div>
            </div>
          </div>
        </div>
      </div>

      <!-- Per-Server Stats (when in individual mode) -->
      <div v-if="viewMode === 'individual' && systemData?.replicas?.length > 1" class="card">
        <div class="card-header">
          <h3>Server Details</h3>
        </div>
        <div class="card-body">
          <div style="overflow-x:auto;">
            <table class="t">
              <thead>
                <tr>
                  <th>Hostname</th>
                  <th style="text-align:right;">Port</th>
                  <th style="text-align:right;">CPU (User)</th>
                  <th style="text-align:right;">CPU (Sys)</th>
                  <th style="text-align:right;">Memory</th>
                  <th style="text-align:right;">DB Pool</th>
                </tr>
              </thead>
              <tbody>
                <tr
                  v-for="replica in systemData.replicas"
                  :key="`${replica.hostname}:${replica.port}`"
                >
                  <td style="font-weight:500;">{{ replica.hostname }}</td>
                  <td style="text-align:right; color:var(--text-mid);" class="font-mono tabular-nums">{{ replica.port }}</td>
                  <td style="text-align:right;" class="font-mono tabular-nums">
                    {{ formatCPU(getLastMetricForReplica(replica)?.cpu?.user_us?.last) }}
                  </td>
                  <td style="text-align:right;" class="font-mono tabular-nums">
                    {{ formatCPU(getLastMetricForReplica(replica)?.cpu?.system_us?.last) }}
                  </td>
                  <td style="text-align:right;" class="font-mono tabular-nums">
                    {{ formatMemory(getLastMetricForReplica(replica)?.memory?.rss_bytes?.last) }}
                  </td>
                  <td style="text-align:right;" class="font-mono tabular-nums">
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
      <div class="card" style="margin-bottom:16px;">
        <div class="card-header">
          <h3>Cache Performance</h3>
          <span class="muted">{{ postgresData.database }}</span>
        </div>
        <div class="card-body">
          <div class="grid-4">
            <div class="stat">
              <div class="stat-label">Database Hit Ratio</div>
              <div class="stat-value font-mono" :class="getCacheRatioClass(postgresData.databaseCache?.cacheHitRatio)">
                {{ postgresData.databaseCache?.cacheHitRatio || 0 }}%
              </div>
            </div>
            <div class="stat">
              <div class="stat-label">Table Hit Ratio</div>
              <div class="stat-value font-mono" :class="getCacheRatioClass(postgresData.cacheSummary?.tables?.hitRatio)">
                {{ postgresData.cacheSummary?.tables?.hitRatio || 0 }}%
              </div>
            </div>
            <div class="stat">
              <div class="stat-label">Index Hit Ratio</div>
              <div class="stat-value font-mono" :class="getCacheRatioClass(postgresData.cacheSummary?.indexes?.hitRatio)">
                {{ postgresData.cacheSummary?.indexes?.hitRatio || 0 }}%
              </div>
            </div>
            <div class="stat">
              <div class="stat-label">Shared Buffers</div>
              <div class="stat-value font-mono">
                {{ postgresData.bufferConfig?.sharedBuffersSize || 'N/A' }}
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- Table Cache Details -->
      <div class="card" style="margin-bottom:16px;">
        <div class="card-header">
          <h3>Table Cache Stats</h3>
          <span class="muted">hit ratios per table in queen schema</span>
        </div>
        <div class="card-body">
          <div style="overflow-x:auto;">
            <table class="t">
              <thead>
                <tr>
                  <th>Table</th>
                  <th style="text-align:right;">Disk Reads</th>
                  <th style="text-align:right;">Cache Hits</th>
                  <th style="text-align:right;">Hit Ratio</th>
                </tr>
              </thead>
              <tbody>
                <tr
                  v-for="table in postgresData.tableCache"
                  :key="table.table"
                >
                  <td class="font-mono" style="font-weight:500;">{{ table.table }}</td>
                  <td style="text-align:right; color:var(--text-mid);" class="font-mono tabular-nums">{{ formatNumber(table.diskReads) }}</td>
                  <td style="text-align:right; color:var(--text-mid);" class="font-mono tabular-nums">{{ formatNumber(table.cacheHits) }}</td>
                  <td style="text-align:right;">
                    <span class="font-mono tabular-nums" :class="getCacheRatioClass(table.cacheHitRatio)">
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
      <div class="card" style="margin-bottom:16px;">
        <div class="card-header">
          <h3>Index Cache Stats</h3>
          <span class="muted">top 20 by disk reads</span>
        </div>
        <div class="card-body">
          <div style="overflow-x:auto;">
            <table class="t">
              <thead>
                <tr>
                  <th>Index</th>
                  <th>Table</th>
                  <th style="text-align:right;">Disk Reads</th>
                  <th style="text-align:right;">Cache Hits</th>
                  <th style="text-align:right;">Hit Ratio</th>
                </tr>
              </thead>
              <tbody>
                <tr
                  v-for="idx in postgresData.indexCache"
                  :key="idx.index"
                >
                  <td class="font-mono" style="font-size:12px;">{{ idx.index }}</td>
                  <td style="color:var(--text-mid);">{{ idx.table }}</td>
                  <td style="text-align:right; color:var(--text-mid);" class="font-mono tabular-nums">{{ formatNumber(idx.diskReads) }}</td>
                  <td style="text-align:right; color:var(--text-mid);" class="font-mono tabular-nums">{{ formatNumber(idx.cacheHits) }}</td>
                  <td style="text-align:right;">
                    <span class="font-mono tabular-nums" :class="getCacheRatioClass(idx.cacheHitRatio)">
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
      <div v-if="postgresData.bufferUsage?.length" class="card" style="margin-bottom:16px;">
        <div class="card-header">
          <h3>Buffer Cache Contents</h3>
          <span class="muted">what's cached in shared_buffers</span>
        </div>
        <div class="card-body">
          <div style="overflow-x:auto;">
            <table class="t">
              <thead>
                <tr>
                  <th>Object</th>
                  <th style="text-align:right;">Buffered Size</th>
                  <th style="text-align:right;">% of Cache</th>
                </tr>
              </thead>
              <tbody>
                <tr
                  v-for="buf in postgresData.bufferUsage"
                  :key="buf.object"
                >
                  <td class="font-mono" style="font-weight:500;">{{ buf.object }}</td>
                  <td style="text-align:right; color:var(--text-mid);" class="font-mono tabular-nums">{{ buf.bufferedSize }}</td>
                  <td style="text-align:right;">
                    <div style="display:flex; align-items:center; justify-content:flex-end; gap:8px;">
                      <div class="bar" style="width:64px;">
                        <i :style="{ width: `${Math.min(buf.percentOfCache, 100)}%` }" />
                      </div>
                      <span class="font-mono tabular-nums" style="font-size:12px; color:var(--text-mid); width:48px; text-align:right;">
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
      <div class="card" style="margin-bottom:16px;">
        <div class="card-header">
          <h3>Table Sizes</h3>
          <span class="muted">storage usage per table</span>
        </div>
        <div class="card-body">
          <div style="overflow-x:auto;">
            <table class="t">
              <thead>
                <tr>
                  <th>Table</th>
                  <th style="text-align:right;">Total Size</th>
                  <th style="text-align:right;">Table Size</th>
                  <th style="text-align:right;">Index Size</th>
                </tr>
              </thead>
              <tbody>
                <tr
                  v-for="tbl in postgresData.tableSizes"
                  :key="tbl.table"
                >
                  <td class="font-mono" style="font-weight:500;">{{ tbl.table }}</td>
                  <td style="text-align:right; font-weight:500;" class="font-mono tabular-nums">{{ tbl.totalSize }}</td>
                  <td style="text-align:right; color:var(--text-mid);" class="font-mono tabular-nums">{{ tbl.tableSize }}</td>
                  <td style="text-align:right;" class="font-mono tabular-nums">{{ tbl.indexSize }}</td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>

      <!-- Dead Tuples & HOT Updates (side by side) -->
      <div style="display:grid; grid-template-columns:1fr 1fr; gap:16px; margin-bottom:16px;">
        <!-- Dead Tuples -->
        <div class="card">
          <div class="card-header">
            <h3>Dead Tuples</h3>
            <span v-if="postgresData.deadTuples?.length" class="chip chip-warn" style="margin-left:auto;">
              {{ postgresData.deadTuples.length }} tables
            </span>
            <span v-else class="muted">tables needing vacuum</span>
          </div>
          <div class="card-body">
            <div v-if="postgresData.deadTuples?.length" style="overflow-x:auto;">
              <table class="t">
                <thead>
                  <tr>
                    <th>Table</th>
                    <th style="text-align:right;">Dead</th>
                    <th style="text-align:right;">Dead %</th>
                    <th>Last Vacuum</th>
                  </tr>
                </thead>
                <tbody>
                  <tr
                    v-for="tbl in postgresData.deadTuples"
                    :key="tbl.table"
                  >
                    <td class="font-mono" style="font-size:12px;">{{ tbl.table }}</td>
                    <td style="text-align:right;" class="font-mono tabular-nums">{{ formatNumber(tbl.deadTuples) }}</td>
                    <td style="text-align:right;">
                      <span class="font-mono tabular-nums" :style="{ color: tbl.deadPercentage > 10 ? '#f43f5e' : 'var(--text-mid)' }">
                        {{ tbl.deadPercentage || 0 }}%
                      </span>
                    </td>
                    <td style="font-size:12px; color:var(--text-low);">
                      {{ formatTimestamp(tbl.lastAutovacuum || tbl.lastVacuum) }}
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
            <div v-else style="text-align:center; padding:32px 0; color:var(--ok-500);">
              No dead tuples — tables are clean
            </div>
          </div>
        </div>

        <!-- HOT Updates -->
        <div class="card">
          <div class="card-header">
            <h3>HOT Update Efficiency</h3>
            <span class="muted">higher is better</span>
          </div>
          <div class="card-body">
            <div v-if="postgresData.hotUpdates?.length" style="overflow-x:auto;">
              <table class="t">
                <thead>
                  <tr>
                    <th>Table</th>
                    <th style="text-align:right;">Updates</th>
                    <th style="text-align:right;">HOT %</th>
                  </tr>
                </thead>
                <tbody>
                  <tr
                    v-for="tbl in postgresData.hotUpdates"
                    :key="tbl.table"
                  >
                    <td class="font-mono" style="font-size:12px;">{{ tbl.table }}</td>
                    <td style="text-align:right; color:var(--text-mid);" class="font-mono tabular-nums">{{ formatNumber(tbl.totalUpdates) }}</td>
                    <td style="text-align:right;">
                      <span class="font-mono tabular-nums" :class="getHotRatioClass(tbl.hotUpdatePercentage)">
                        {{ tbl.hotUpdatePercentage || 0 }}%
                      </span>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
            <div v-else style="text-align:center; padding:32px 0; color:var(--text-low);">
              No updates recorded yet
            </div>
          </div>
        </div>
      </div>

      <!-- Active Queries -->
      <div class="card" style="margin-bottom:16px;">
        <div class="card-header">
          <h3>Active Queries</h3>
          <span v-if="postgresData.activeQueries?.length" class="chip chip-bad" style="margin-left:auto;">
            {{ postgresData.activeQueries.length }} slow
          </span>
          <span v-else class="muted">queries running longer than 1s</span>
        </div>
        <div class="card-body">
          <div v-if="postgresData.activeQueries?.length" style="display:flex; flex-direction:column; gap:12px;">
            <div
              v-for="query in postgresData.activeQueries"
              :key="query.pid"
              class="card" style="padding:12px 14px;"
            >
              <div style="display:flex; align-items:center; justify-content:space-between; margin-bottom:8px;">
                <span style="font-size:12px; font-weight:500; color:var(--text-mid);">
                  PID: {{ query.pid }} · {{ query.state }}
                </span>
                <span class="font-mono tabular-nums" style="font-size:12px;" :style="{ color: query.duration > 10 ? 'var(--ember-400)' : 'var(--warn-400)' }">
                  {{ formatDurationSeconds(query.duration) }}
                </span>
              </div>
              <code class="font-mono" style="font-size:12px; color:var(--text-hi); display:block; word-break:break-all;">
                {{ query.query }}
              </code>
              <div v-if="query.waitEventType" style="margin-top:8px; font-size:12px; color:var(--text-low);">
                Wait: {{ query.waitEventType }} / {{ query.waitEvent }}
              </div>
            </div>
          </div>
          <div v-else style="text-align:center; padding:32px 0; color:var(--ok-500);">
            No slow queries running
          </div>
        </div>
      </div>

      <!-- Autovacuum Status -->
      <div v-if="postgresData.autovacuumStatus?.length" class="card">
        <div class="card-header">
          <h3>Autovacuum Status</h3>
          <span class="chip chip-warn" style="margin-left:auto;">{{ postgresData.autovacuumStatus.length }} pending</span>
        </div>
        <div class="card-body">
          <div style="overflow-x:auto;">
            <table class="t">
              <thead>
                <tr>
                  <th>Table</th>
                  <th style="text-align:right;">Dead Tuples</th>
                  <th style="text-align:right;">Vacuum Count</th>
                  <th>Last Autovacuum</th>
                </tr>
              </thead>
              <tbody>
                <tr
                  v-for="tbl in postgresData.autovacuumStatus"
                  :key="tbl.table"
                >
                  <td class="font-mono" style="font-weight:500;">{{ tbl.table }}</td>
                  <td style="text-align:right;" class="font-mono tabular-nums">{{ formatNumber(tbl.deadTuples) }}</td>
                  <td style="text-align:right; color:var(--text-mid);" class="font-mono tabular-nums">{{ tbl.autovacuumCount }}</td>
                  <td style="font-size:12px; color:var(--text-low);">{{ formatTimestamp(tbl.lastAutovacuum) }}</td>
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
import MultiSelect from '@/components/MultiSelect.vue'

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
const queueLagData = ref(null)
const selectedQueues = ref([])

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

// Metric toggles — chip color matches the chart series color (monochrome-first).
//   activeDot = inline dot background; matches the line/bar color in the chart.
// Push & Pop are just two data series → two greys. Ack = healthy series → green.
const throughputMetrics = [
  { key: 'push', label: 'Push', activeClass: 'chip-mute', activeDot: '#e6e6e6' }, // primary grey
  { key: 'pop',  label: 'Pop',  activeClass: 'chip-mute', activeDot: '#8a8a92' }, // secondary grey
  { key: 'ack',  label: 'Ack',  activeClass: 'chip-ok',   activeDot: '#4ade80' }, // healthy
]
const selectedThroughputMetrics = reactive({ push: true, pop: true, ack: true })

const eventLoopMetrics = [
  { key: 'avg', label: 'Avg Event Loop', activeClass: 'chip-mute', activeDot: '#e6e6e6' }, // primary
  { key: 'max', label: 'Max Event Loop', activeClass: 'chip-bad',  activeDot: '#fb7185' }, // emergency
]
const selectedEventLoopMetrics = reactive({ avg: true, max: true })

const errorMetrics = [
  { key: 'dbErrors',  label: 'DB Errors',  activeClass: 'chip-bad',  activeDot: '#fb7185' }, // danger
  { key: 'ackFailed', label: 'Ack Failed', activeClass: 'chip-warn', activeDot: '#e6b450' }, // warn
  { key: 'dlq',       label: 'DLQ',        activeClass: 'chip-bad',  activeDot: '#fb7185' }, // danger
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

// Per-queue chart options (legend visible to distinguish queues)
const perQueueThroughputOptions = {
  plugins: {
    legend: { display: true, position: 'top', labels: { boxWidth: 12, padding: 8, font: { size: 11 } } }
  },
  scales: {
    y: {
      beginAtZero: true,
      min: 0,
      title: { display: true, text: 'Pops/s', font: { size: 11 } }
    }
  }
}

const perQueueLagOptions = {
  plugins: {
    legend: { display: true, position: 'top', labels: { boxWidth: 12, padding: 8, font: { size: 11 } } },
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
      title: { display: true, text: 'Latency', font: { size: 11 } },
      ticks: {
        callback: (value) => formatDuration(value)
      }
    }
  }
}

// Per-queue chart palette — 5 distinct shades, cycled. Starts with the
// three greys (primary / secondary / tertiary), then green (healthy) and
// pink (danger) as the last two differentiators. For >5 queues the palette
// cycles; distinction remains via the chart legend / tooltip.
const queueColors = [
  { border: '#e6e6e6', bg: 'rgba(230, 230, 230, 0.10)' }, // chart-1 (primary grey)
  { border: '#8a8a92', bg: 'rgba(138, 138, 146, 0.10)' }, // chart-2 (secondary grey)
  { border: '#6a6a6a', bg: 'rgba(106, 106, 106, 0.10)' }, // chart-3 (tertiary grey)
  { border: '#4ade80', bg: 'rgba(74, 222, 128, 0.12)'  }, // healthy
  { border: '#fb7185', bg: 'rgba(251, 113, 133, 0.12)' }, // danger
]

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
      borderColor: '#e6e6e6',
      backgroundColor: 'rgba(230, 230, 230, 0.12)',
      fill: true,
      tension: 0
    })
  }
  
  if (selectedThroughputMetrics.pop) {
    datasets.push({
      label: 'Pop/s',
      data: ts.map(t => t.popPerSecond || 0),
      borderColor: '#8a8a92',
      backgroundColor: 'rgba(138, 138, 146, 0.12)',
      fill: true,
      tension: 0
    })
  }
  
  if (selectedThroughputMetrics.ack) {
    datasets.push({
      label: 'Ack/s',
      data: ts.map(t => t.ackPerSecond || 0),
      borderColor: '#4ade80',
      backgroundColor: 'rgba(74, 222, 128, 0.12)',
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
        borderColor: '#e6e6e6',
        backgroundColor: 'rgba(230, 230, 230, 0.1)',
        fill: true,
        tension: 0
      },
      { 
        label: 'Max Lag (ms)', 
        data: ts.map(t => t.maxLagMs || 0), 
        borderColor: '#fb7185',
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
      borderColor: '#e6e6e6',
      backgroundColor: 'rgba(230, 230, 230, 0.12)',
      fill: true,
      tension: 0
    })
  }
  
  if (selectedEventLoopMetrics.max) {
    datasets.push({
      label: 'Max Event Loop (ms)',
      data: ts.map(t => t.maxEventLoopLagMs || 0),
      borderColor: '#fb7185',
      backgroundColor: 'rgba(244, 63, 94, 0.06)',
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
        borderColor: '#e6e6e6',
        backgroundColor: 'rgba(230, 230, 230, 0.12)',
        fill: true,
        tension: 0
      },
      { 
        label: 'DB Connections', 
        data: ts.map(t => t.dbConnections || 0), 
        borderColor: '#8a8a92',
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
      backgroundColor: 'rgba(244, 63, 94, 0.6)',
      borderColor: '#f43f5e',
      borderWidth: 1
    })
  }
  
  if (selectedErrorMetrics.ackFailed) {
    datasets.push({
      label: 'Ack Failed',
      data: ts.map(t => t.ackFailed || 0),
      backgroundColor: 'rgba(138, 138, 146, 0.6)',
      borderColor: '#8a8a92',
      borderWidth: 1
    })
  }
  
  if (selectedErrorMetrics.dlq) {
    datasets.push({
      label: 'DLQ',
      data: ts.map(t => t.dlqCount || 0),
      backgroundColor: 'rgba(230, 230, 230, 0.6)',
      borderColor: '#e6e6e6',
      borderWidth: 1
    })
  }
  
  return { labels, datasets }
})

// Per-Queue Chart Data
const availableQueues = computed(() => {
  const raw = queueLagData.value || []
  const names = new Set(raw.map(r => r.queueName))
  return [...names].sort()
})

const queuesToShow = computed(() => {
  if (selectedQueues.value.length > 0) return [...selectedQueues.value].sort()
  return availableQueues.value
})

// Helper: pivot flat per-queue time series into chart datasets
const buildPerQueueChart = (valueAccessor) => {
  const raw = queueLagData.value || []
  if (!raw.length) return { labels: [], datasets: [] }

  // Get unique sorted buckets ascending
  const bucketSet = new Set(raw.map(r => r.bucketTime))
  const buckets = [...bucketSet].sort()

  const multiDay = buckets.length >= 2 &&
    new Date(buckets[0]).toDateString() !== new Date(buckets[buckets.length - 1]).toDateString()

  const labels = buckets.map(b => formatChartLabel(new Date(b), multiDay))

  // Build lookup: "queue|bucket" -> row
  const lookup = {}
  raw.forEach(r => { lookup[`${r.queueName}|${r.bucketTime}`] = r })

  const datasets = queuesToShow.value.map((q, i) => {
    const color = queueColors[i % queueColors.length]
    return {
      label: q,
      data: buckets.map(b => {
        const entry = lookup[`${q}|${b}`]
        return entry ? valueAccessor(entry) : 0
      }),
      borderColor: color.border,
      backgroundColor: color.bg,
      fill: false,
      tension: 0
    }
  })

  return { labels, datasets }
}

const queuePopChartData = computed(() => buildPerQueueChart(entry => {
  // popCount is total pops in that bucket (1 minute), convert to per-second
  return Math.round(((entry.popCount || 0) / 60) * 100) / 100
}))

const queueLagChartData = computed(() => buildPerQueueChart(entry => entry.avgLagMs || 0))

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
    
    const colors = ['#e6e6e6', '#8a8a92', '#6a6a6a', '#4ade80', '#fb7185']
    
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
          borderColor: '#e6e6e6',
          backgroundColor: 'rgba(230, 230, 230, 0.12)',
          fill: true,
          tension: 0
        },
        { 
          label: 'System CPU (%)', 
          data: ts.map(t => (t.metrics?.cpu?.system_us?.[agg] || 0) / 100), 
          borderColor: '#8a8a92',
          backgroundColor: 'rgba(138, 138, 146, 0.12)',
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
    
    const colors = ['#e6e6e6', '#8a8a92', '#4ade80', '#fb7185', '#6a6a6a']
    
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
          borderColor: '#e6e6e6',
          backgroundColor: 'rgba(230, 230, 230, 0.12)',
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
        borderColor: '#e6e6e6',
        backgroundColor: 'rgba(230, 230, 230, 0.12)',
        fill: true,
        tension: 0
      },
      { 
        label: 'Idle', 
        data: ts.map(t => t.metrics?.database?.pool_idle?.[agg] || 0), 
        borderColor: '#4ade80',
        backgroundColor: 'rgba(74, 222, 128, 0.12)',
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
        borderColor: '#e6e6e6',
        backgroundColor: 'rgba(230, 230, 230, 0.12)',
        fill: true,
        tension: 0
      },
      { 
        label: 'System Queue', 
        data: ts.map(t => t.metrics?.threadpool?.system?.queue_size?.[agg] || 0), 
        borderColor: '#8a8a92',
        backgroundColor: 'rgba(138, 138, 146, 0.12)',
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
  if (ratio === undefined || ratio === null) return ''
  if (ratio >= 99) return 'color-ok'
  if (ratio >= 95) return 'color-ice'
  if (ratio >= 90) return 'color-crown'
  return 'color-ember'
}

const getHotRatioClass = (ratio) => {
  if (ratio === undefined || ratio === null) return ''
  if (ratio >= 95) return 'color-ok'
  if (ratio >= 80) return 'color-ice'
  if (ratio >= 50) return 'color-crown'
  return 'color-ember'
}

const getLastMetricForReplica = (replica) => {
  if (!replica?.timeSeries?.length) return null
  return replica.timeSeries[replica.timeSeries.length - 1]?.metrics
}

const fetchData = async () => {
  // Only show loading skeleton if we don't have data yet (smooth background refresh)
  const hasData = dataSource.value === 'postgres' ? postgresData.value : (workerData.value || systemData.value)
  if (!hasData) loading.value = true
  
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
      
      const [workerRes, systemRes, queueLagRes] = await Promise.all([
        system.getWorkerMetrics(params),
        system.getSystemMetrics(params),
        system.getQueueLag(params).catch(e => {
          console.warn('Failed to fetch per-queue lag metrics:', e.message)
          return { data: [] }
        })
      ])
      
      workerData.value = workerRes.data
      systemData.value = systemRes.data
      queueLagData.value = queueLagRes.data
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

<style scoped>
@media (max-width: 1100px) {
  div[style*="grid-template-columns:repeat(6"] { grid-template-columns: repeat(3, 1fr) !important; }
  div[style*="grid-template-columns:repeat(4"] { grid-template-columns: repeat(2, 1fr) !important; }
  div[style*="grid-template-columns:1fr 1fr"] { grid-template-columns: 1fr !important; }
}
@media (max-width: 640px) {
  div[style*="padding:28px 32px"] { padding: 16px !important; }
}
</style>
