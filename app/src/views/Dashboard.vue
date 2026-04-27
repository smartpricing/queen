<template>
  <div class="view-container">

    <!-- Range selector + active-queue filter chip -->
    <div style="display:flex; align-items:center; gap:12px; margin-bottom:20px; flex-wrap:wrap;">
      <div class="seg">
        <button v-for="r in timeRanges" :key="r.value" :class="{ on: selectedRange === r.value }" @click="selectedRange = r.value">{{ r.label }}</button>
      </div>
      <div style="display:flex; align-items:center; gap:8px; font-size:11px; font-family:'JetBrains Mono',monospace; color:var(--text-mid);">
        <span class="pulse" /> live · 30s autorefresh
      </div>
      <!-- Filter chip: appears when operator drilled into a single queue. Click
           × to clear; charts snap back to system-wide view. -->
      <button
        v-if="filterQueue"
        class="filter-chip"
        @click="clearQueueFilter"
        title="Clear queue filter"
      >
        <span>filtered by</span>
        <span class="filter-chip-queue">{{ filterQueue }}</span>
        <span class="filter-chip-x">×</span>
      </button>
      <div style="margin-left:auto; display:flex; gap:12px;">
        <div class="legend"><span class="sw" style="background:#e6e6e6;"></span> produced</div>
        <div class="legend"><span class="sw" style="background:#8a8a92;"></span> consumed</div>
      </div>
    </div>

    <!-- STAT CARDS -->
    <div class="grid-4" style="margin-bottom:20px;">
      <MetricCard label="Messages" :value="overview?.messages?.total || 0" :subtext="`${formatNumber(totalPartitions)} partitions · ${formatNumber(overview?.messages?.completed || 0)} completed`" :icon="QueuesIcon" icon-color="ice" :loading="loadingOverview" clickable @click="$router.push('/messages')" />
      <MetricCard label="Pending" :value="Math.max(0, overview?.messages?.pending || 0)" :subtext="`across ${overview?.queues || 0} queues`" :icon="PendingIcon" icon-color="crown" :loading="loadingOverview" />
      <MetricCard label="Consumer groups" :value="consumers?.length || 0" :icon="ConsumersIcon" icon-color="crown" :loading="loadingConsumers" clickable @click="$router.push('/consumers')" />
      <MetricCard label="Throughput" :value="throughput.current" format="raw" unit=" msg/s" :trend="throughput.trend" :loading="loadingStatus" />
    </div>

    <!-- Health strip — compact inline row, color only when values cross thresholds -->
    <div class="health-strip" style="margin-bottom:10px;">
      <div class="hs-item">
        <svg width="13" height="13" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.6"><path stroke-linecap="round" stroke-linejoin="round" d="M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z"/></svg>
        <span class="hs-label">Time lag</span>
        <span class="hs-value num" :class="lagNumClass(overview?.lag?.time?.avg)">{{ formatDuration(overview?.lag?.time?.avg || 0) }}</span>
        <span class="hs-sep">/</span>
        <span class="hs-value num" :class="lagNumClass(overview?.lag?.time?.max)">{{ formatDuration(overview?.lag?.time?.max || 0) }}</span>
      </div>
      <div class="hs-item">
        <svg width="13" height="13" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.6"><path stroke-linecap="round" stroke-linejoin="round" d="M9 5H7a2 2 0 00-2 2v12a2 2 0 002 2h10a2 2 0 002-2V7a2 2 0 00-2-2h-2M9 5a2 2 0 002 2h2a2 2 0 002-2M9 5a2 2 0 012-2h2a2 2 0 012 2"/></svg>
        <span class="hs-label">Pending</span>
        <span class="hs-value num">{{ formatNumber(overview?.lag?.offset?.avg || 0) }}</span>
        <span class="hs-sep">/</span>
        <span class="hs-value num" :class="pendingNumClass(overview?.lag?.offset?.max)">{{ formatNumber(overview?.lag?.offset?.max || 0) }}</span>
      </div>
      <div class="hs-item">
        <svg width="13" height="13" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.6"><path stroke-linecap="round" stroke-linejoin="round" d="M13 10V3L4 14h7v7l9-11h-7z"/></svg>
        <span class="hs-label">Event loop</span>
        <span class="hs-value num" :class="elNumClass(avgEventLoopLag)">{{ avgEventLoopLag }}ms</span>
        <span class="hs-sep">/</span>
        <span class="hs-value num" :class="elNumClass(maxEventLoopLag)">{{ maxEventLoopLag }}ms</span>
      </div>
      <div class="hs-item">
        <svg width="13" height="13" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.6"><path stroke-linecap="round" stroke-linejoin="round" d="M19 11H5m14 0a2 2 0 012 2v6a2 2 0 01-2 2H5a2 2 0 01-2-2v-6a2 2 0 012-2m14 0V9a2 2 0 00-2-2M5 11V9a2 2 0 012-2m0 0V5a2 2 0 012-2h6a2 2 0 012 2v2M7 7h10"/></svg>
        <span class="hs-label">Batch push/pop/ack</span>
        <span class="hs-value num">{{ batchEfficiency.push }}</span>
        <span class="hs-sep">·</span>
        <span class="hs-value num">{{ batchEfficiency.pop }}</span>
        <span class="hs-sep">·</span>
        <span class="hs-value num">{{ batchEfficiency.ack }}</span>
      </div>
    </div>

    <!-- Small multiples — 3x3 grid of mini-charts on a shared time axis -->
    <div class="sm-grid" style="margin-bottom:20px;">
      <!-- 1. Throughput push / pop / ack -->
      <div class="sm-card">
        <div class="sm-head">
          <span class="sm-title">Throughput · msg/s</span>
          <span v-if="chartData.labels.length" class="sm-chip">{{ throughput.current }}/s</span>
        </div>
        <BaseChart v-if="chartData.labels.length > 0" type="line" :data="chartData" :options="miniOpts('msg/s')" height="140px" />
        <div v-else class="sm-empty">—</div>
      </div>

      <!-- 2. Pending delta over the window (cumulative push - ack). -->
      <div class="sm-card">
        <div class="sm-head">
          <span class="sm-title">Pending Δ · msgs (window)</span>
          <span v-if="pendingDeltaLatest !== 0" class="sm-chip"
                :class="{ 'sm-chip-warn': pendingDeltaLatest > 0, 'sm-chip-ok': pendingDeltaLatest < 0 }">
            {{ pendingDeltaLatest > 0 ? '+' : '' }}{{ formatNumber(pendingDeltaLatest) }}
          </span>
        </div>
        <BaseChart v-if="pendingDeltaChartData.labels.length > 0" type="line" :data="pendingDeltaChartData" :options="miniOpts('msgs')" height="140px" />
        <div v-else class="sm-empty">—</div>
      </div>

      <!-- 3. Queen CPU user / sys (or per-replica when >1 replica) -->
      <div class="sm-card">
        <div class="sm-head">
          <span class="sm-title">Queen CPU · %<span v-if="hasMultipleReplicas" style="font-weight:400; color:var(--text-faint);"> · per replica</span></span>
          <span v-if="cpuLatest > 0" class="sm-chip">{{ cpuLatest.toFixed(1) }}%</span>
        </div>
        <BaseChart v-if="cpuChartData.labels.length > 0" type="line" :data="cpuChartData" :options="miniOpts('%', v => v.toFixed(1) + '%')" height="140px" />
        <div v-else class="sm-empty">—</div>
      </div>

      <!-- 3. Time lag avg / max -->
      <div class="sm-card">
        <div class="sm-head">
          <span class="sm-title">Lag · ms</span>
          <span v-if="lagLatestMax > 0" class="sm-chip" :class="{ 'sm-chip-warn': lagLatestMax > 60000, 'sm-chip-bad': lagLatestMax > 300000 }">{{ formatLagShort(lagLatestMax) }}</span>
        </div>
        <BaseChart v-if="lagChartData.labels.length > 0" type="line" :data="lagChartData" :options="miniOpts('ms', formatLagShort)" height="140px" />
        <div v-else class="sm-empty">—</div>
      </div>

      <!-- 4. Event loop lag avg / max -->
      <div class="sm-card">
        <div class="sm-head">
          <span class="sm-title">Event loop · ms</span>
          <span v-if="maxEventLoopLag > 0" class="sm-chip" :class="{ 'sm-chip-warn': maxEventLoopLag > 50, 'sm-chip-bad': maxEventLoopLag > 100 }">{{ maxEventLoopLag }}ms</span>
        </div>
        <BaseChart v-if="elChartData.labels.length > 0" type="line" :data="elChartData" :options="miniOpts('ms')" height="140px" />
        <div v-else class="sm-empty">—</div>
      </div>

      <!-- 5. Errors: db / ack failed / dlq -->
      <div class="sm-card">
        <div class="sm-head">
          <span class="sm-title">Errors</span>
          <span v-if="errorTotal > 0" class="sm-chip sm-chip-bad">{{ formatNumber(errorTotal) }}</span>
        </div>
        <BaseChart v-if="errorsChartData.labels.length > 0" type="bar" :data="errorsChartData" :options="miniOpts('count')" height="140px" />
        <div v-else class="sm-empty">—</div>
      </div>

      <!-- 6. Retention ops stacked -->
      <div class="sm-card">
        <div class="sm-head">
          <span class="sm-title">Retention · msgs</span>
          <span v-if="retentionTotal > 0" class="sm-chip">{{ formatNumber(retentionTotal) }}</span>
        </div>
        <BaseChart v-if="retentionChartData.labels.length > 0" type="bar" :data="retentionChartData" :options="miniOptsStacked('msgs')" height="140px" />
        <div v-else class="sm-empty">—</div>
      </div>

      <!-- 7. Partitions created / deleted -->
      <div class="sm-card">
        <div class="sm-head">
          <span class="sm-title">Partitions · events</span>
          <span v-if="(partitionCreatedTotal + partitionDeletedTotal) > 0" class="sm-chip">
            <span style="color:var(--text-hi);">+{{ formatNumber(partitionCreatedTotal) }}</span>
            <span style="color:var(--text-faint); margin:0 4px;">/</span>
            <span style="color:var(--text-mid);">-{{ formatNumber(partitionDeletedTotal) }}</span>
          </span>
        </div>
        <BaseChart v-if="partitionChartData.labels.length > 0" type="bar" :data="partitionChartData" :options="miniOpts('count')" height="140px" />
        <div v-else class="sm-empty">—</div>
      </div>

      <!-- 8. DB pool: active / idle / free slots -->
      <div class="sm-card">
        <div class="sm-head">
          <span class="sm-title">DB pool · conns</span>
          <span v-if="poolLatest" class="sm-chip">{{ poolLatest.active }}/{{ poolLatest.size }}</span>
        </div>
        <BaseChart v-if="poolChartData.labels.length > 0" type="line" :data="poolChartData" :options="miniOpts('conns')" height="140px" />
        <div v-else class="sm-empty">—</div>
      </div>
    </div>

    <!-- Queue time lag (secondary, compact list). Clicking a row filters the
         small-multiples above to that single queue. Click × in the top chip
         to clear, or click the same row again. -->
    <div class="card" style="margin-bottom:20px;">
      <div class="card-header">
        <h3>Queue time lag</h3>
        <span class="muted">max per queue · click to drill down</span>
      </div>
      <div class="card-body">
        <div v-if="queueTimeLag.length > 0" class="grid-3" style="gap:16px 24px;">
          <div
            v-for="q in queueTimeLag.slice(0, 9)"
            :key="q.name"
            class="ql-row"
            :class="{ 'ql-row-active': filterQueue === q.name }"
            @click="toggleQueueFilter(q.name)"
          >
            <div style="display:flex; justify-content:space-between; font-size:13px; margin-bottom:6px;">
              <span style="font-weight:500; color:var(--text-hi); overflow:hidden; text-overflow:ellipsis; white-space:nowrap; max-width:160px;">{{ q.name }}</span>
              <span class="font-mono tabular-nums font-medium num" :class="{ warn: q.lag > 60 && q.lag <= 600, bad: q.lag > 600, mute: !q.lag }">
                {{ q.lag > 0 ? formatDuration(q.lag) : '-' }}
              </span>
            </div>
            <div class="bar" style="width:100%; display:block;">
              <i :class="q.lag > 600 ? 'bad' : q.lag > 60 ? 'warn' : ''" :style="{ width: q.pct + '%' }" />
            </div>
          </div>
        </div>
        <div v-else style="height:60px; display:flex; align-items:center; justify-content:center; color:var(--text-low); font-size:13px;">
          No consumer groups
        </div>
      </div>
    </div>

    <!-- Bottom row: tables -->
    <div class="grid-7-5">
      <DataTable title="Top queues by pending depth" :subtitle="`${queues.length} queues`" :columns="queueColumns" :data="enrichedQueues" :loading="loadingQueues" :page-size="6" clickable @row-click="(row) => $router.push(`/queues/${row.name}`)">
        <template #name="{ row }">
          <div style="display:flex; align-items:center; gap:8px;">
            <span class="status-dot" :class="statusDotClass(row._status)" />
            <span style="font-weight:500; color:var(--text-hi);">{{ row.name }}</span>
          </div>
        </template>
        <template #partitions="{ value }">
          <span class="font-mono tabular-nums" style="color:var(--text-mid); font-size:12px;">{{ value || 1 }}</span>
        </template>
        <template #_pending="{ row }">
          <span class="font-mono tabular-nums">{{ formatNumber(Math.max(0, row.messages?.pending || 0)) }}</span>
        </template>
        <template #_depth="{ row }">
          <div class="bar" style="width:100px; display:block;">
            <i :class="row._status === 'degraded' ? 'bad' : row._status === 'watch' ? 'warn' : ''" :style="{ width: row._depthPct + '%' }" />
          </div>
        </template>
        <template #_lag="{ row }">
          <span class="font-mono tabular-nums" style="font-size:12px;" :style="{ color: lagColor(row._lag) }">
            {{ row._lag > 0 ? formatDuration(row._lag) : '-' }}
          </span>
        </template>
        <template #_status="{ row }">
          <span class="chip" :class="statusChipClass(row._status)"><span class="dot"></span>{{ row._status }}</span>
        </template>
      </DataTable>

      <!-- Consumer groups — opus card list -->
      <div class="card">
        <div class="card-header">
          <h3>Consumer groups</h3>
          <span class="muted">{{ consumers.length }} total</span>
        </div>
        <div v-if="loadingConsumers" class="card-body" style="display:flex; flex-direction:column; gap:8px;">
          <div v-for="i in 5" :key="i" class="skeleton" style="height:56px; border-radius:10px;" />
        </div>
        <div v-else-if="sortedConsumers.length" class="card-body" style="display:flex; flex-direction:column; gap:10px;">
          <div
            v-for="g in sortedConsumers.slice(0, 6)"
            :key="g.name + g.queueName"
            class="cg-row"
            @click="$router.push('/consumers')"
          >
            <span class="pulse" :class="cgPulseClass(g)" style="flex-shrink:0;" />
            <div style="flex:1; min-width:0;">
              <div style="display:flex; align-items:center; gap:8px;">
                <span style="font-weight:500; color:var(--text-hi); font-size:13.5px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap;">{{ g.name }}</span>
              </div>
              <div class="font-mono" style="font-size:11px; color:var(--text-low); margin-top:2px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap;">
                <span v-if="g.queueName">{{ g.queueName }} · </span>
                {{ g.members || 0 }} members · lag {{ g.maxTimeLag > 0 ? formatDuration(g.maxTimeLag) : '0s' }}
              </div>
            </div>
            <span class="chip" :class="cgChipClass(g)" style="flex-shrink:0;">
              <span class="dot"></span>{{ cgLabel(g) }}
            </span>
          </div>
        </div>
        <div v-else class="card-body" style="padding:32px; text-align:center; color:var(--text-low); font-size:13px;">
          No consumer groups
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, watch, h } from 'vue'
import { resources, queues as queuesApi, analytics, consumers as consumersApi, system as systemApi } from '@/api'
import { formatNumber } from '@/composables/useApi'
import { useRefresh } from '@/composables/useRefresh'
import MetricCard from '@/components/MetricCard.vue'
import BaseChart from '@/components/BaseChart.vue'
import DataTable from '@/components/DataTable.vue'

const overview = ref(null)
const queues = ref([])
const consumers = ref([])
const statusData = ref(null)
// Populated by PR 2 (retention timeseries) and PR 3 (per-queue ops / partitions).
// Kept as empty arrays so computed charts render nothing instead of exploding.
const retentionData = ref([])
const partitionOpsData = ref([])
// Per-replica system metrics, used only when more than one replica is present
// to split the CPU small-multiple into one line per (hostname, workerId).
const systemMetricsData = ref(null)

const loadingOverview = ref(true)
const loadingQueues = ref(true)
const loadingConsumers = ref(true)
const loadingStatus = ref(true)

const selectedRange = ref('1h')
const timeRanges = [
  { label: '1h', value: '1h', minutes: 60 },
  { label: '6h', value: '6h', minutes: 360 },
  { label: '24h', value: '24h', minutes: 1440 },
]

// Active queue drilldown. When non-empty, all multiples are scoped to this
// queue via the `queue` query param on /api/v1/status (which routes
// get_status_v3 into its queue-filtered branch). Click a row in the queue
// time lag list to set; click × in the filter chip or the same row again to clear.
const filterQueue = ref('')
const toggleQueueFilter = (name) => {
  filterQueue.value = (filterQueue.value === name) ? '' : name
  // Refetch immediately so the user sees the filtered view without waiting for the 30s interval.
  fetchStatus()
  fetchRetention()
  fetchQueueOps()
}
const clearQueueFilter = () => { if (filterQueue.value) toggleQueueFilter(filterQueue.value) }

const getTimeRangeParams = () => {
  const r = timeRanges.find(x => x.value === selectedRange.value) || timeRanges[0]
  const now = new Date()
  const params = { from: new Date(now.getTime() - r.minutes * 60 * 1000).toISOString(), to: now.toISOString() }
  // Scope every analytics call to the drilldown queue when active. The
  // status/queue-ops/retention procedures all accept a `queue` filter field.
  if (filterQueue.value) params.queue = filterQueue.value
  return params
}

const totalPartitions = computed(() =>
  queues.value.reduce((sum, q) => sum + (q.partitions || 1), 0)
)

const throughput = computed(() => {
  if (!overview.value?.throughput) return { current: '0.0', trend: 0 }
  const current = overview.value.throughput.ingestedPerSecond || overview.value.throughput.processedPerSecond || 0
  let trend = 0
  if (statusData.value?.throughput?.length >= 2) {
    const h = statusData.value.throughput
    const cr = h[0]?.ingestedPerSecond || 0
    const older = h.length > 5 ? h.slice(5) : h.slice(1)
    const pr = older.length ? older.reduce((s, x) => s + (x.ingestedPerSecond || 0), 0) / older.length : 0
    if (pr > 0 && cr > 0) trend = Math.round(((cr - pr) / pr) * 100)
    else if (pr === 0 && cr > 0) trend = 100
    else if (pr > 0 && cr === 0) trend = -100
  }
  return { current: current.toFixed(1), trend }
})

const avgEventLoopLag = computed(() => {
  const w = statusData.value?.workers
  if (!w?.length) return 0
  return Math.round(w.reduce((s, x) => s + (x.avgEventLoopLagMs || 0), 0) / w.length)
})
const maxEventLoopLag = computed(() => {
  const w = statusData.value?.workers
  if (!w?.length) return 0
  return Math.max(...w.map(x => x.maxEventLoopLagMs || 0))
})

const batchEfficiency = computed(() => {
  const b = statusData.value?.messages?.batchEfficiency
  return { push: b?.push?.toFixed(1) || '0', pop: b?.pop?.toFixed(1) || '0', ack: b?.ack?.toFixed(1) || '0' }
})

const lagColor = (s) => !s || s === 0 ? 'var(--text-mid)' : s < 60 ? '#4ade80' : s < 300 ? '#e6b450' : '#fb7185'
const elColor = (ms) => !ms || ms === 0 ? 'var(--text-mid)' : ms < 50 ? '#4ade80' : ms < 100 ? '#e6b450' : '#fb7185'

// Threshold → class name helpers (monochrome by default, color only when crossing a threshold)
const lagNumClass     = (s)  => !s || s === 0 ? ''         : s  < 60  ? ''     : s  < 300 ? 'warn' : 'bad'
const elNumClass      = (ms) => !ms || ms === 0 ? ''       : ms < 50  ? ''     : ms < 100 ? 'warn' : 'bad'
const pendingNumClass = (n)  => !n || n < 1000 ? ''        : n  < 10000 ? 'warn' : 'bad'

const formatDuration = (seconds) => {
  if (!seconds || seconds === 0) return '0s'
  if (seconds < 60) return `${Math.round(seconds)}s`
  if (seconds < 3600) { const m = Math.floor(seconds / 60); const s = Math.round(seconds % 60); return s ? `${m}m ${s}s` : `${m}m` }
  if (seconds < 86400) { const h = Math.floor(seconds / 3600); const m = Math.floor((seconds % 3600) / 60); return m ? `${h}h ${m}m` : `${h}h` }
  const d = Math.floor(seconds / 86400); const h = Math.floor((seconds % 86400) / 3600); return h ? `${d}d ${h}h` : `${d}d`
}

const formatChartLabel = (date, multi) => multi
  ? date.toLocaleString('en-US', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' })
  : date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' })

// Reversed history (ascending time) for all charts — computed once.
const history = computed(() => {
  if (!statusData.value?.throughput?.length) return []
  return [...statusData.value.throughput].reverse()
})
const multiDay = computed(() => {
  const h = history.value
  if (h.length < 2) return false
  return new Date(h[0].timestamp).toDateString() !== new Date(h[h.length-1].timestamp).toDateString()
})
const chartLabels = computed(() => history.value.map(h => formatChartLabel(new Date(h.timestamp), multiDay.value)))

// Format lag (stored in ms) with human-friendly scale: ms → s → m.
const formatLagShort = (v) => {
  const n = Number(v) || 0
  if (n < 1) return '0'
  if (n < 1000) return `${Math.round(n)}ms`
  if (n < 60000) return `${(n / 1000).toFixed(1)}s`
  return `${(n / 60000).toFixed(1)}m`
}

// Shared mini-chart options (small multiples look).
const miniOpts = (yLabel, tickFmt) => ({
  plugins: { legend: { display: false } },
  scales: {
    y: {
      title: { display: true, text: yLabel, font: { size: 10 } },
      ticks: tickFmt ? { callback: tickFmt } : undefined,
    }
  }
})
const miniOptsStacked = (yLabel) => ({
  plugins: { legend: { display: false } },
  scales: {
    x: { stacked: true },
    y: { stacked: true, title: { display: true, text: yLabel, font: { size: 10 } } }
  }
})

// 1. Throughput — Push / Pop / Ack on the same axis. Push vs Ack is the
// producer/consumer view of the system; Pop shows the read-side pressure
// separately (matters in bus mode where one message is popped by many
// consumer groups). Push/Pop are grey siblings; Ack is green (semantic
// "healthy completion") so the eye quickly spots when acks lag pops.
const chartData = computed(() => {
  const h = history.value
  if (!h.length) return { labels: [], datasets: [] }
  return {
    labels: chartLabels.value,
    datasets: [
      { label: 'Push/s', data: h.map(x => Number(x.ingestedPerSecond) || 0),
        fill: true, borderColor: '#e6e6e6', backgroundColor: 'rgba(230,230,230,0.12)' },
      { label: 'Pop/s',  data: h.map(x => Number(x.popPerSecond) || 0),
        fill: false, borderColor: '#8a8a92' },
      { label: 'Ack/s',  data: h.map(x => Number(x.processedPerSecond) || 0),
        fill: true, borderColor: '#4ade80', backgroundColor: 'rgba(74,222,128,0.10)' },
    ]
  }
})

// 2. Pending Δ — cumulative (ingested - processed) across the window,
// anchored at 0 on the left edge. Positive slope = pending is growing
// (we're falling behind), negative slope = catching up. Not absolute
// pending; deliberately labeled "Δ (window)" to make that clear.
const pendingDeltaSeries = computed(() => {
  const h = history.value
  if (!h.length) return []
  let cum = 0
  return h.map(x => {
    const ingested = Number(x.ingested) || 0
    const processed = Number(x.processed) || 0
    cum += (ingested - processed)
    return cum
  })
})
const pendingDeltaChartData = computed(() => {
  const series = pendingDeltaSeries.value
  if (!series.length) return { labels: [], datasets: [] }
  // Two overlaid series: fill area (accent color for visibility on mini chart)
  // plus a zero baseline marker so operators see which side they're on.
  return {
    labels: chartLabels.value,
    datasets: [
      { label: 'Pending Δ', data: series, fill: true,
        borderColor: '#e6e6e6', backgroundColor: 'rgba(230,230,230,0.10)' }
    ]
  }
})
const pendingDeltaLatest = computed(() => {
  const s = pendingDeltaSeries.value
  return s.length ? s[s.length - 1] : 0
})

// 2. Queen CPU.
// - Single-replica deploys: show the aggregate user / system split so the
//   operator sees the kernel vs user work balance.
// - Multi-replica deploys: flip to one line per replica so fanout imbalance
//   is visible. We use the dedicated getSystemMetrics feed (per-replica
//   time series) rather than the status_v3 average, which would hide skew.
const replicaIdentity = (r) => `${r.hostname}:${r.port}:${r.worker_id || r.workerId || ''}`
const hasMultipleReplicas = computed(() => (systemMetricsData.value?.replicas || []).length > 1)

const cpuChartData = computed(() => {
  // Multi-replica: plot one line per replica (user+sys summed).
  if (hasMultipleReplicas.value) {
    const replicas = systemMetricsData.value?.replicas || []
    // Use the first replica's time-series as the shared time axis.
    const ts = replicas[0]?.timeSeries || []
    if (!ts.length) return { labels: [], datasets: [] }
    const multi = ts.length >= 2 &&
      new Date(ts[0].timestamp).toDateString() !== new Date(ts[ts.length - 1].timestamp).toDateString()
    const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multi))
    const palette = ['#e6e6e6', '#8a8a92', '#6a6a6a', '#b8b8b8', '#4a4a4f', '#9a9a9a']
    const datasets = replicas.map((r, i) => {
      const color = palette[i % palette.length]
      return {
        label: r.hostname.substring(0, 10),  // keep legend short
        data: (r.timeSeries || []).map(t =>
          ((t.metrics?.cpu?.user_us?.avg || 0) + (t.metrics?.cpu?.system_us?.avg || 0)) / 100),
        fill: false,
        borderColor: color,
      }
    })
    return { labels, datasets }
  }
  // Single-replica fallback: show user / system split from status_v3.
  const h = history.value
  const hasCpu = h.some(x => x.queenCpuUserPct != null || x.queenCpuSysPct != null)
  if (!hasCpu) return { labels: [], datasets: [] }
  return {
    labels: chartLabels.value,
    datasets: [
      { label: 'User %',   data: h.map(x => Number(x.queenCpuUserPct) || 0), fill: true },
      { label: 'System %', data: h.map(x => Number(x.queenCpuSysPct)  || 0), fill: true },
    ]
  }
})
const cpuLatest = computed(() => {
  if (hasMultipleReplicas.value) {
    // Show the hottest replica's latest CPU — the one operators care about.
    const replicas = systemMetricsData.value?.replicas || []
    let max = 0
    for (const r of replicas) {
      const last = r.timeSeries?.[r.timeSeries.length - 1]
      if (!last) continue
      const v = ((last.metrics?.cpu?.user_us?.avg || 0) + (last.metrics?.cpu?.system_us?.avg || 0)) / 100
      if (v > max) max = v
    }
    return max
  }
  const last = history.value[history.value.length - 1]
  if (!last) return 0
  return (Number(last.queenCpuUserPct) || 0) + (Number(last.queenCpuSysPct) || 0)
})

// 3. Time lag avg / max
const lagChartData = computed(() => {
  const h = history.value
  if (!h.length) return { labels: [], datasets: [] }
  return {
    labels: chartLabels.value,
    datasets: [
      { label: 'Avg (ms)', data: h.map(x => Number(x.avgLagMs) || 0), fill: true },
      { label: 'Max (ms)', data: h.map(x => Number(x.maxLagMs) || 0), fill: false, borderColor: '#8a8a92' },
    ]
  }
})
const lagLatestMax = computed(() => {
  const last = history.value[history.value.length - 1]
  return last ? (Number(last.maxLagMs) || 0) : 0
})

// 4. Event loop lag
const elChartData = computed(() => {
  const h = history.value
  const has = h.some(x => x.avgEventLoopLagMs != null || x.maxEventLoopLagMs != null)
  if (!has) return { labels: [], datasets: [] }
  return {
    labels: chartLabels.value,
    datasets: [
      { label: 'Avg (ms)', data: h.map(x => Number(x.avgEventLoopLagMs) || 0), fill: true },
      { label: 'Max (ms)', data: h.map(x => Number(x.maxEventLoopLagMs) || 0), fill: false, borderColor: '#8a8a92' },
    ]
  }
})

// 5. Errors: db / ack failed / dlq (stacked-ish bars; we use simple bars)
const errorsChartData = computed(() => {
  const h = history.value
  const has = h.some(x => (x.dbErrors || 0) + (x.ackFailed || 0) + (x.dlqCount || 0) > 0)
  if (!has) return { labels: [], datasets: [] }
  return {
    labels: chartLabels.value,
    datasets: [
      { label: 'DB errors', data: h.map(x => Number(x.dbErrors) || 0), backgroundColor: 'rgba(244,63,94,0.6)', borderColor: '#f43f5e' },
      { label: 'Ack failed', data: h.map(x => Number(x.ackFailed) || 0), backgroundColor: 'rgba(230,180,80,0.5)', borderColor: '#e6b450' },
      { label: 'DLQ', data: h.map(x => Number(x.dlqCount) || 0), backgroundColor: 'rgba(230,230,230,0.5)', borderColor: '#e6e6e6' },
    ]
  }
})
const errorTotal = computed(() => history.value.reduce((s, x) =>
  s + (Number(x.dbErrors) || 0) + (Number(x.ackFailed) || 0) + (Number(x.dlqCount) || 0), 0))

// 6. Retention ops (populated in PR 2; empty placeholder safe today)
const retentionChartData = computed(() => {
  const rows = retentionData.value || []
  if (!rows.length) return { labels: [], datasets: [] }
  const labels = rows.map(r => formatChartLabel(new Date(r.bucket), false))
  return {
    labels,
    datasets: [
      { label: 'Retention', data: rows.map(r => Number(r.retentionMsgs) || 0), backgroundColor: 'rgba(230,230,230,0.6)', borderColor: '#e6e6e6' },
      { label: 'Completed', data: rows.map(r => Number(r.completedRetentionMsgs) || 0), backgroundColor: 'rgba(138,138,146,0.6)', borderColor: '#8a8a92' },
      { label: 'Evicted', data: rows.map(r => Number(r.evictionMsgs) || 0), backgroundColor: 'rgba(230,180,80,0.5)', borderColor: '#e6b450' },
    ]
  }
})
const retentionTotal = computed(() => (retentionData.value || []).reduce((s, r) =>
  s + (Number(r.retentionMsgs) || 0) + (Number(r.completedRetentionMsgs) || 0) + (Number(r.evictionMsgs) || 0), 0))

// 7. Partitions created / deleted — both as positive bars on a shared axis.
// Earlier versions plotted deleted as negative to get a diverging view, but
// when a retention sweep deletes thousands while creates trickle in tens,
// the positive side gets compressed to invisibility. Two honest series side
// by side is more truthful; Chart.js groups them on the same tick.
const partitionChartData = computed(() => {
  const rows = partitionOpsData.value || []
  if (!rows.length) return { labels: [], datasets: [] }
  const labels = rows.map(r => formatChartLabel(new Date(r.bucket), false))
  return {
    labels,
    datasets: [
      { label: 'Created', data: rows.map(r => Number(r.partitionsCreated) || 0),
        backgroundColor: 'rgba(230,230,230,0.5)', borderColor: '#e6e6e6', borderWidth: 1 },
      { label: 'Deleted', data: rows.map(r => Number(r.partitionsDeleted) || 0),
        backgroundColor: 'rgba(138,138,146,0.5)', borderColor: '#8a8a92', borderWidth: 1 },
    ]
  }
})
// Totals for the chip so operators see at-a-glance magnitude balance.
const partitionCreatedTotal = computed(() => (partitionOpsData.value || []).reduce((s, r) => s + (Number(r.partitionsCreated) || 0), 0))
const partitionDeletedTotal = computed(() => (partitionOpsData.value || []).reduce((s, r) => s + (Number(r.partitionsDeleted) || 0), 0))

// 8. DB pool active / idle / size (free slots from worker_metrics as bonus)
const poolChartData = computed(() => {
  const h = history.value
  const has = h.some(x => x.dbPoolActive != null || x.dbPoolIdle != null || x.minFreeSlots != null)
  if (!has) return { labels: [], datasets: [] }
  return {
    labels: chartLabels.value,
    datasets: [
      { label: 'Active', data: h.map(x => Number(x.dbPoolActive) || 0), fill: true },
      { label: 'Idle', data: h.map(x => Number(x.dbPoolIdle) || 0), fill: false },
      { label: 'Free slots', data: h.map(x => Number(x.minFreeSlots) || 0), fill: false, borderColor: '#6a6a6a' },
    ]
  }
})
const poolLatest = computed(() => {
  const last = history.value[history.value.length - 1]
  if (!last) return null
  const active = Math.round(Number(last.dbPoolActive) || 0)
  const idle = Math.round(Number(last.dbPoolIdle) || 0)
  const size = Math.round(Number(last.dbPoolSize) || (active + idle))
  return { active, idle, size }
})

const queueTimeLag = computed(() => {
  if (!consumers.value.length) return []
  const m = {}
  consumers.value.forEach(c => { const n = c.queueName || '?'; const l = c.maxTimeLag || 0; if (!m[n] || l > m[n]) m[n] = l })
  const arr = Object.entries(m).map(([n, l]) => ({ name: n, lag: l }))
  const mx = Math.max(...arr.map(x => x.lag), 1)
  return arr.sort((a, b) => b.lag - a.lag).map(x => ({ ...x, pct: (x.lag / mx) * 100 }))
})

const queueColumns = [
  { key: 'name', label: 'Queue', sortable: true },
  { key: 'partitions', label: 'Parts', sortable: true, align: 'right' },
  { key: '_pending', label: 'Pending', sortable: true, align: 'right' },
  { key: '_depth', label: 'Depth' },
  { key: '_lag', label: 'Lag', align: 'right' },
  { key: '_status', label: 'Status' },
]

const statusDotClass = (s) => s === 'degraded' ? 'status-dot-danger' : s === 'watch' ? 'status-dot-warning' : 'status-dot-success'
const statusChipClass = (s) => s === 'degraded' ? 'chip-bad' : s === 'watch' ? 'chip-warn' : 'chip-ok'

// Consumer group status derivation
const cgStatus = (g) => {
  const lag = g.maxTimeLag || 0
  if (lag >= 300) return 'stuck'
  if (lag >= 60 || (g.partitionsWithLag || 0) > 0) return 'lag'
  return 'healthy'
}
const cgPulseClass = (g) => {
  const s = cgStatus(g)
  return s === 'stuck' ? 'pulse-ember' : s === 'lag' ? 'pulse-amber' : ''
}
const cgChipClass = (g) => {
  const s = cgStatus(g)
  return s === 'stuck' ? 'chip-bad' : s === 'lag' ? 'chip-warn' : 'chip-ok'
}
const cgLabel = (g) => cgStatus(g)
// Aggregate max time lag per queue from consumer groups
const queueLagMap = computed(() => {
  const m = {}
  for (const c of consumers.value) {
    const q = c.queueName
    if (!q) continue
    const lag = c.maxTimeLag || 0
    if (!m[q] || lag > m[q]) m[q] = lag
  }
  return m
})

const enrichedQueues = computed(() => {
  const base = [...queues.value].map(q => ({ ...q, _pending: Math.max(0, q.messages?.pending || 0) }))
  const maxPending = Math.max(...base.map(q => q._pending), 1)
  return base
    .map(q => {
      const lag = queueLagMap.value[q.name] || 0
      const status = lag >= 300 || (q._pending / maxPending) > 0.8 ? 'degraded'
                   : lag >= 60 || (q._pending / maxPending) > 0.5 ? 'watch'
                   : 'healthy'
      return {
        ...q,
        _lag: lag,
        _status: status,
        _depthPct: Math.min(100, (q._pending / maxPending) * 100),
      }
    })
    .sort((a, b) => b._pending - a._pending)
})

const sortedQueues = enrichedQueues
const sortedConsumers = computed(() => [...consumers.value].sort((a, b) => (b.maxTimeLag || 0) - (a.maxTimeLag || 0)))

const fetchOverview = async () => { if (!overview.value) loadingOverview.value = true; try { overview.value = (await resources.getOverview()).data } catch {} finally { loadingOverview.value = false } }
const fetchQueues = async () => { if (!queues.value.length) loadingQueues.value = true; try { const r = await queuesApi.list(); queues.value = r.data?.queues || r.data || [] } catch {} finally { loadingQueues.value = false } }
const fetchConsumers = async () => { if (!consumers.value.length) loadingConsumers.value = true; try { const r = await consumersApi.list(); consumers.value = Array.isArray(r.data) ? r.data : r.data?.consumer_groups || [] } catch {} finally { loadingConsumers.value = false } }
const fetchStatus = async () => { if (!statusData.value) loadingStatus.value = true; try { statusData.value = (await analytics.getStatus(getTimeRangeParams())).data } catch {} finally { loadingStatus.value = false } }
const fetchRetention = async () => {
  try {
    const r = await systemApi.getRetention(getTimeRangeParams())
    retentionData.value = r.data?.series || []
  } catch { retentionData.value = [] }
}
// Queue-ops feed: aggregate per-bucket partitions_created/deleted across all
// queues for the Dashboard's "Partitions" small multiple. The System view
// consumes the full per-queue series separately.
const fetchQueueOps = async () => {
  try {
    const r = await systemApi.getQueueOps(getTimeRangeParams())
    const series = r.data?.series || []
    // Aggregate to one row per bucket.
    const byBucket = {}
    for (const row of series) {
      const b = byBucket[row.bucket] ||= { bucket: row.bucket, partitionsCreated: 0, partitionsDeleted: 0 }
      b.partitionsCreated += Number(row.partitionsCreated) || 0
      b.partitionsDeleted += Number(row.partitionsDeleted) || 0
    }
    partitionOpsData.value = Object.values(byBucket).sort((a, b) => a.bucket.localeCompare(b.bucket))
  } catch { partitionOpsData.value = [] }
}
// Per-replica system metrics. Only consumed by the CPU small-multiple when
// more than one replica is present; otherwise the status-v3 average is used.
// This fetch is cheap — same payload the System view already uses. We do NOT
// scope it by filterQueue because CPU is system-wide regardless of queue.
const fetchSystemMetrics = async () => {
  try {
    const r = timeRanges.find(x => x.value === selectedRange.value) || timeRanges[0]
    const now = new Date()
    const params = {
      from: new Date(now.getTime() - r.minutes * 60 * 1000).toISOString(),
      to: now.toISOString(),
    }
    const res = await systemApi.getSystemMetrics(params)
    systemMetricsData.value = res.data
  } catch { systemMetricsData.value = null }
}
const fetchAll = async () => {
  await Promise.all([
    fetchOverview(), fetchQueues(), fetchConsumers(),
    fetchStatus(), fetchRetention(), fetchQueueOps(),
    fetchSystemMetrics(),
  ])
}

useRefresh(fetchAll)
watch(selectedRange, () => { fetchStatus(); fetchRetention(); fetchQueueOps(); fetchSystemMetrics() })

let interval = null
onMounted(() => { fetchAll(); interval = setInterval(fetchAll, 30000) })
onUnmounted(() => { if (interval) clearInterval(interval) })

function QueuesIcon(p) { return h('svg', { ...p, fill:'none', viewBox:'0 0 24 24', stroke:'currentColor', 'stroke-width':'2' }, [h('path',{'stroke-linecap':'round','stroke-linejoin':'round', d:'M3 7h18M3 12h18M3 17h12'})]) }
function PendingIcon(p) { return h('svg', { ...p, fill:'none', viewBox:'0 0 24 24', stroke:'currentColor', 'stroke-width':'2' }, [h('circle',{cx:'12',cy:'12',r:'9'}),h('path',{d:'M12 7v5l3 2'})]) }
function ConsumersIcon(p) { return h('svg', { ...p, fill:'none', viewBox:'0 0 24 24', stroke:'currentColor', 'stroke-width':'2' }, [h('circle',{cx:'9',cy:'8',r:'3'}),h('circle',{cx:'17',cy:'10',r:'2.2'}),h('path',{d:'M3 20c0-3.3 2.7-6 6-6s6 2.7 6 6M15 20c.2-2 1.6-3.5 3.3-3.9'})]) }
</script>

<style scoped>
.cg-row {
  display: flex; align-items: center; gap: 10px;
  padding: 10px 12px; border-radius: 10px;
  border: 1px solid var(--bd);
  cursor: pointer;
  transition: background .15s var(--ease), border-color .15s var(--ease);
}
:global(.dark) .cg-row { background: rgba(255,255,255,.015); }
:global(.light) .cg-row { background: rgba(10,10,10,.015); }
.cg-row:hover { border-color: var(--bd-hi); }
:global(.dark) .cg-row:hover { background: rgba(255,255,255,.03); }
:global(.light) .cg-row:hover { background: rgba(10,10,10,.03); }

/* Small-multiples grid: dense 3x3 on wide screens (9 cells), 2xN on medium,
   1xN on mobile. 3-wide reads better than 4-wide at this chart density. */
.sm-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 10px;
}
@media (max-width: 1280px) { .sm-grid { grid-template-columns: repeat(2, 1fr); } }
@media (max-width: 640px)  { .sm-grid { grid-template-columns: 1fr; } }

.sm-card {
  border: 1px solid var(--bd);
  background: var(--ink-2);
  border-radius: 10px;
  padding: 10px 12px 6px;
  display: flex; flex-direction: column;
  min-height: 178px;
}
.sm-head {
  display: flex; align-items: center; justify-content: space-between;
  margin-bottom: 6px; gap: 8px;
}
.sm-title {
  font-size: 11px;
  font-weight: 600;
  color: var(--text-mid);
  letter-spacing: .02em;
  text-transform: uppercase;
}
.sm-chip {
  font-family: 'JetBrains Mono', monospace;
  font-size: 11px;
  color: var(--text-hi);
  background: var(--ink-3);
  border: 1px solid var(--bd);
  padding: 1px 6px;
  border-radius: 99px;
  white-space: nowrap;
}
.sm-chip-ok   { color: #4ade80; border-color: rgba(74,222,128,0.3); }
.sm-chip-warn { color: #e6b450; border-color: rgba(230,180,80,0.3); }
.sm-chip-bad  { color: #fb7185; border-color: rgba(251,113,133,0.3); }
.sm-empty {
  flex: 1;
  display: flex; align-items: center; justify-content: center;
  color: var(--text-faint); font-size: 18px;
}

/* Filter chip — pill above the multiples showing active drilldown. */
.filter-chip {
  display: inline-flex; align-items: center; gap: 6px;
  padding: 3px 8px 3px 10px;
  border-radius: 99px;
  border: 1px solid var(--bd-hi);
  background: var(--ink-3);
  color: var(--text-mid);
  font-size: 11px;
  font-family: 'JetBrains Mono', monospace;
  cursor: pointer;
  transition: background .15s var(--ease), border-color .15s var(--ease), color .15s var(--ease);
}
.filter-chip:hover { background: var(--ink-4); color: var(--text-hi); border-color: var(--text-low); }
.filter-chip-queue { color: var(--text-hi); font-weight: 500; }
.filter-chip-x { color: var(--text-low); font-size: 14px; line-height: 1; padding-left: 2px; }

/* Clickable queue lag row — subtle affordance, strong active state. */
.ql-row {
  padding: 4px 8px;
  margin: -4px -8px;
  border-radius: 6px;
  cursor: pointer;
  transition: background .12s var(--ease);
}
.ql-row:hover { background: rgba(255,255,255,0.03); }
.ql-row-active {
  background: rgba(255,255,255,0.06);
  box-shadow: inset 2px 0 0 var(--accent);
}
</style>

