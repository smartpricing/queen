<template>
  <div class="view-container">

    <!-- Controls: source toggle (Resources / Postgres) + range picker.
         Queue Operations used to be the third option here but it now
         lives at /operations as its own first-class page under the
         Overview sidebar group. -->
    <div class="card" style="padding:12px 16px; margin-bottom:20px;">
      <div style="display:flex; flex-wrap:wrap; align-items:center; gap:12px 20px;">
        <div style="display:flex; align-items:center; gap:8px;">
          <span style="font-size:11px; font-weight:500; color:var(--text-low);">Source</span>
          <div class="seg">
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

      <!-- Secondary row: System-only contextual controls (per-server vs
           aggregate, plus aggregation type for the resource charts). -->
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

    <!-- Loading skeleton -->
    <div v-if="loading">
      <div style="display:grid; grid-template-columns:repeat(2,1fr); gap:16px;">
        <div v-for="i in 4" :key="i" class="card" style="padding:24px;">
          <div class="skeleton" style="height:192px; width:100%; border-radius:8px;" />
        </div>
      </div>
    </div>

    <!-- System Resources View -->
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
              <div class="stat-value font-mono">{{ formatCPU(lastMetrics?.cpu?.user_us?.last) }}</div>
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
import { ref, computed, onMounted } from 'vue'
import { system } from '@/api'
import { useRefresh } from '@/composables/useRefresh'
import BaseChart from '@/components/BaseChart.vue'

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
const loading = ref(true)
// Default landed-on tab is 'system'. The third historical option, 'worker',
// has been promoted to its own page at /operations under Overview, so the
// Source toggle on this page only switches between Resources and Postgres.
const dataSource = ref('system')
const viewMode = ref('aggregate')
const aggregationType = ref('avg')
const timeRange = ref(60)
const customMode = ref(false)
const customFrom = ref('')
const customTo = ref('')

const systemData = ref(null)
const postgresData = ref(null)

const timeRanges = [
  { label: '15m', value: 15 },
  { label: '1h', value: 60 },
  { label: '6h', value: 360 },
  { label: '24h', value: 1440 }
]

const aggregationTypes = [
  { label: 'Average', value: 'avg' },
  { label: 'Maximum', value: 'max' },
  { label: 'Minimum', value: 'min' }
]

// ---------------------------------------------------------------------------
// Formatters (local copies, intentionally not extracted to a shared util —
// see the matching block in QueueOperations.vue for the reasoning)
// ---------------------------------------------------------------------------
const formatDateTimeLocal = (date) => {
  const year = date.getFullYear()
  const month = String(date.getMonth() + 1).padStart(2, '0')
  const day = String(date.getDate()).padStart(2, '0')
  const hours = String(date.getHours()).padStart(2, '0')
  const minutes = String(date.getMinutes()).padStart(2, '0')
  return `${year}-${month}-${day}T${hours}:${minutes}`
}

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

// ---------------------------------------------------------------------------
// Time range picker
// ---------------------------------------------------------------------------
const selectQuickRange = (value) => {
  customMode.value = false
  timeRange.value = value
  fetchData()
}

const toggleCustomMode = () => {
  customMode.value = !customMode.value
  if (customMode.value) {
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

const formatChartLabel = (date, isMultiDayFlag) => {
  if (isMultiDayFlag) {
    return date.toLocaleString('en-US', {
      month: 'short',
      day: 'numeric',
      hour: '2-digit',
      minute: '2-digit'
    })
  }
  return date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' })
}

const isMultiDay = (timeSeries) => {
  if (!timeSeries || timeSeries.length < 2) return false
  const firstDate = new Date(timeSeries[0].timestamp)
  const lastDate = new Date(timeSeries[timeSeries.length - 1].timestamp)
  return firstDate.toDateString() !== lastDate.toDateString()
}

// ---------------------------------------------------------------------------
// Postgres helpers
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Chart options
// ---------------------------------------------------------------------------
const cpuChartOptions = {
  plugins: { legend: { display: false } },
  scales: {
    y: {
      title: { display: true, text: 'CPU %', font: { size: 11 } },
      ticks: { callback: (value) => value.toFixed(1) + '%' }
    }
  }
}

const memoryChartOptions = {
  plugins: { legend: { display: false } },
  scales: { y: { title: { display: true, text: 'Memory (MB)', font: { size: 11 } } } }
}

const poolChartOptions = {
  plugins: { legend: { display: false } },
  scales: { y: { title: { display: true, text: 'Connections', font: { size: 11 } } } }
}

const queueChartOptions = {
  plugins: { legend: { display: false } },
  scales: { y: { title: { display: true, text: 'Queue Size', font: { size: 11 } } } }
}

// ---------------------------------------------------------------------------
// Computed
// ---------------------------------------------------------------------------
const lastMetrics = computed(() => {
  if (!systemData.value?.replicas?.length) return null
  const firstReplica = systemData.value.replicas[0]
  if (!firstReplica?.timeSeries?.length) return null
  return firstReplica.timeSeries[firstReplica.timeSeries.length - 1]?.metrics
})

const cpuChartData = computed(() => {
  if (!systemData.value?.replicas?.length) return { labels: [], datasets: [] }

  const agg = aggregationType.value

  if (viewMode.value === 'individual') {
    const datasets = []
    const firstReplica = systemData.value.replicas[0]
    const ts = firstReplica?.timeSeries || []
    const multiDay = isMultiDay(ts)
    const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multiDay))

    const colors = ['#e6e6e6', '#8a8a92', '#6a6a6a', '#b8b8b8', '#4a4a4f']

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
    const ts = systemData.value.replicas[0]?.timeSeries || []
    const multiDay = isMultiDay(ts)
    const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multiDay))

    return {
      labels,
      datasets: [
        { label: 'User CPU (%)',
          data: ts.map(t => (t.metrics?.cpu?.user_us?.[agg] || 0) / 100),
          borderColor: '#e6e6e6', backgroundColor: 'rgba(230, 230, 230, 0.12)',
          fill: true, tension: 0 },
        { label: 'System CPU (%)',
          data: ts.map(t => (t.metrics?.cpu?.system_us?.[agg] || 0) / 100),
          borderColor: '#8a8a92', backgroundColor: 'rgba(138, 138, 146, 0.12)',
          fill: true, tension: 0 }
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

    const colors = ['#e6e6e6', '#8a8a92', '#6a6a6a', '#b8b8b8', '#4a4a4f']

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
        { label: 'RSS (MB)',
          data: ts.map(t => Math.round((t.metrics?.memory?.rss_bytes?.[agg] || 0) / 1024 / 1024)),
          borderColor: '#e6e6e6', backgroundColor: 'rgba(230, 230, 230, 0.12)',
          fill: true, tension: 0 }
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
      { label: 'Active',
        data: ts.map(t => t.metrics?.database?.pool_active?.[agg] || 0),
        borderColor: '#e6e6e6', backgroundColor: 'rgba(230, 230, 230, 0.12)',
        fill: true, tension: 0 },
      { label: 'Idle',
        data: ts.map(t => t.metrics?.database?.pool_idle?.[agg] || 0),
        borderColor: '#8a8a92', backgroundColor: 'rgba(138, 138, 146, 0.12)',
        fill: true, tension: 0 }
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
      { label: 'DB Queue',
        data: ts.map(t => t.metrics?.threadpool?.db?.queue_size?.[agg] || 0),
        borderColor: '#e6e6e6', backgroundColor: 'rgba(230, 230, 230, 0.12)',
        fill: true, tension: 0 },
      { label: 'System Queue',
        data: ts.map(t => t.metrics?.threadpool?.system?.queue_size?.[agg] || 0),
        borderColor: '#8a8a92', backgroundColor: 'rgba(138, 138, 146, 0.12)',
        fill: true, tension: 0 }
    ]
  }
})

// ---------------------------------------------------------------------------
// Fetcher — only loads what the active source needs. Postgres has no time
// dimension (it's a one-shot snapshot), so it ignores the range picker.
// ---------------------------------------------------------------------------
const fetchData = async () => {
  const hasData = dataSource.value === 'postgres' ? postgresData.value : systemData.value
  if (!hasData) loading.value = true

  try {
    if (dataSource.value === 'postgres') {
      const res = await system.getPostgresStats()
      postgresData.value = res.data
    } else {
      let from, to
      if (customMode.value && customFrom.value && customTo.value) {
        from = new Date(customFrom.value)
        to = new Date(customTo.value)
      } else {
        const now = new Date()
        from = new Date(now.getTime() - timeRange.value * 60 * 1000)
        to = now
      }
      const params = { from: from.toISOString(), to: to.toISOString() }
      const res = await system.getSystemMetrics(params)
      systemData.value = res.data
    }
  } catch (err) {
    console.error('Failed to fetch system metrics:', err)
  } finally {
    loading.value = false
  }
}

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
