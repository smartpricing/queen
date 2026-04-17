<template>
  <div class="view-container animate-fade-in">

    <!-- Page head -->
    <div class="page-head">
      <div>
        <div class="eyebrow">Control plane</div>
        <h1>Good afternoon.</h1>
        <p v-if="overview">
          Your fleet processed <b class="font-mono">{{ formatNumber(overview.messages?.total || 0) }}</b> messages
          across <b class="font-mono">{{ overview.queues || 0 }}</b> queues
          with <b class="font-mono">{{ formatNumber(totalPartitions) }}</b> partitions.
        </p>
      </div>
      <div class="actions">
        <router-link to="/queues" class="btn">Browse queues</router-link>
        <router-link to="/analytics" class="btn">Analytics</router-link>
      </div>
    </div>

    <!-- Range selector -->
    <div style="display:flex; align-items:center; gap:12px; margin-bottom:20px; flex-wrap:wrap;">
      <div class="seg">
        <button v-for="r in timeRanges" :key="r.value" :class="{ on: selectedRange === r.value }" @click="selectedRange = r.value">{{ r.label }}</button>
      </div>
      <div style="display:flex; align-items:center; gap:8px; font-size:11px; font-family:'JetBrains Mono',monospace; color:var(--text-mid);">
        <span class="pulse" /> live · 30s autorefresh
      </div>
      <div style="margin-left:auto; display:flex; gap:12px;">
        <div class="legend"><span class="sw" style="background:#22d3ee;"></span> produced</div>
        <div class="legend"><span class="sw" style="background:#fbbf24;"></span> consumed</div>
      </div>
    </div>

    <!-- STAT CARDS -->
    <div class="grid-4" style="margin-bottom:20px;">
      <MetricCard label="Messages" :value="overview?.messages?.total || 0" :subtext="`${formatNumber(totalPartitions)} partitions · ${formatNumber(overview?.messages?.completed || 0)} completed`" :icon="QueuesIcon" icon-color="ice" :loading="loadingOverview" clickable @click="$router.push('/messages')" />
      <MetricCard label="Pending" :value="Math.max(0, overview?.messages?.pending || 0)" :subtext="`across ${overview?.queues || 0} queues`" :icon="PendingIcon" icon-color="crown" :loading="loadingOverview" />
      <MetricCard label="Consumer groups" :value="consumers?.length || 0" :icon="ConsumersIcon" icon-color="crown" :loading="loadingConsumers" clickable @click="$router.push('/consumers')" />
      <MetricCard label="Throughput" :value="throughput.current" format="raw" unit=" msg/s" :trend="throughput.trend" :loading="loadingStatus" />
    </div>

    <!-- Health indicator strip -->
    <div class="grid-4" style="gap:12px; margin-bottom:20px;">
      <div class="card" style="padding:8px 12px;">
        <div style="display:flex; align-items:center; justify-content:space-between;">
          <div style="display:flex; align-items:center; gap:6px;">
            <svg class="w-3.5 h-3.5" style="color:#fbbf24;" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2"><path stroke-linecap="round" stroke-linejoin="round" d="M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z"/></svg>
            <span style="font-size:11px; font-weight:500; color:var(--text-low);">Time Lag</span>
          </div>
          <div style="display:flex; align-items:center; gap:8px; font-size:12px;">
            <span class="font-mono tabular-nums font-semibold" :style="{ color: lagColor(overview?.lag?.time?.avg) }">{{ formatDuration(overview?.lag?.time?.avg || 0) }}</span>
            <span style="color:var(--text-faint);">/</span>
            <span class="font-mono tabular-nums font-semibold" :style="{ color: lagColor(overview?.lag?.time?.max) }">{{ formatDuration(overview?.lag?.time?.max || 0) }}</span>
          </div>
        </div>
      </div>
      <div class="card" style="padding:8px 12px;">
        <div style="display:flex; align-items:center; justify-content:space-between;">
          <div style="display:flex; align-items:center; gap:6px;">
            <svg class="w-3.5 h-3.5" style="color:#22d3ee;" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2"><path stroke-linecap="round" stroke-linejoin="round" d="M9 5H7a2 2 0 00-2 2v12a2 2 0 002 2h10a2 2 0 002-2V7a2 2 0 00-2-2h-2M9 5a2 2 0 002 2h2a2 2 0 002-2M9 5a2 2 0 012-2h2a2 2 0 012 2"/></svg>
            <span style="font-size:11px; font-weight:500; color:var(--text-low);">Pending</span>
          </div>
          <div style="display:flex; align-items:center; gap:8px; font-size:12px;">
            <span class="font-mono tabular-nums font-semibold" style="color:var(--text-hi);">{{ formatNumber(overview?.lag?.offset?.avg || 0) }}</span>
            <span style="color:var(--text-faint);">/</span>
            <span class="font-mono tabular-nums font-semibold" style="color:var(--text-hi);">{{ formatNumber(overview?.lag?.offset?.max || 0) }}</span>
          </div>
        </div>
      </div>
      <div class="card" style="padding:8px 12px;">
        <div style="display:flex; align-items:center; justify-content:space-between;">
          <div style="display:flex; align-items:center; gap:6px;">
            <svg class="w-3.5 h-3.5" style="color:#fb7185;" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2"><path stroke-linecap="round" stroke-linejoin="round" d="M13 10V3L4 14h7v7l9-11h-7z"/></svg>
            <span style="font-size:11px; font-weight:500; color:var(--text-low);">Event Loop</span>
          </div>
          <div style="display:flex; align-items:center; gap:8px; font-size:12px;">
            <span class="font-mono tabular-nums font-semibold" :style="{ color: elColor(avgEventLoopLag) }">{{ avgEventLoopLag }}ms</span>
            <span style="color:var(--text-faint);">/</span>
            <span class="font-mono tabular-nums font-semibold" :style="{ color: elColor(maxEventLoopLag) }">{{ maxEventLoopLag }}ms</span>
          </div>
        </div>
      </div>
      <div class="card" style="padding:8px 12px;">
        <div style="display:flex; align-items:center; justify-content:space-between;">
          <div style="display:flex; align-items:center; gap:6px;">
            <svg class="w-3.5 h-3.5" style="color:#34d399;" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2"><path stroke-linecap="round" stroke-linejoin="round" d="M19 11H5m14 0a2 2 0 012 2v6a2 2 0 01-2 2H5a2 2 0 01-2-2v-6a2 2 0 012-2m14 0V9a2 2 0 00-2-2M5 11V9a2 2 0 012-2m0 0V5a2 2 0 012-2h6a2 2 0 012 2v2M7 7h10"/></svg>
            <span style="font-size:11px; font-weight:500; color:var(--text-low);">Batch</span>
          </div>
          <div style="display:flex; align-items:center; gap:6px; font-size:12px;">
            <span class="font-mono tabular-nums font-semibold" style="color:#34d399;">{{ batchEfficiency.push }}</span>
            <span class="font-mono tabular-nums font-semibold" style="color:#22d3ee;">{{ batchEfficiency.pop }}</span>
            <span class="font-mono tabular-nums font-semibold" style="color:#fbbf24;">{{ batchEfficiency.ack }}</span>
          </div>
        </div>
      </div>
    </div>

    <!-- Charts row -->
    <div class="grid-2-1" style="margin-bottom:20px;">
      <div class="card card-accent">
        <div class="card-header">
          <h3>Throughput · {{ selectedRange }}</h3>
          <span v-if="chartData.labels.length" class="chip chip-ice"><span class="dot"></span>{{ throughput.current }} msg/s</span>
        </div>
        <div class="card-body">
          <BaseChart v-if="chartData.labels.length > 0" type="line" :data="chartData" :options="chartOptions" height="260px" />
          <div v-else style="height:260px; display:flex; align-items:center; justify-content:center; color:var(--text-mid);">
            <span class="spinner" style="margin-right:8px;" /> Loading…
          </div>
        </div>
      </div>

      <!-- Queue time lag -->
      <div class="card">
        <div class="card-header">
          <h3>Queue time lag</h3>
          <span class="muted">max per queue</span>
        </div>
        <div class="card-body">
          <div v-if="queueTimeLag.length > 0" style="display:flex; flex-direction:column; gap:14px;">
            <div v-for="q in queueTimeLag.slice(0, 6)" :key="q.name">
              <div style="display:flex; justify-content:space-between; font-size:13px; margin-bottom:6px;">
                <span style="font-weight:500; color:var(--text-hi); overflow:hidden; text-overflow:ellipsis; white-space:nowrap; max-width:140px;">{{ q.name }}</span>
                <span class="font-mono tabular-nums font-medium" :style="{ color: q.lag > 600 ? '#fb7185' : q.lag > 60 ? '#fbbf24' : '#34d399' }">
                  {{ q.lag > 0 ? formatDuration(q.lag) : '-' }}
                </span>
              </div>
              <div class="bar" style="width:100%; display:block;">
                <i :class="q.lag > 600 ? 'bad' : q.lag > 60 ? 'warn' : ''" :style="{ width: q.pct + '%', background: q.lag > 600 ? 'linear-gradient(90deg,#f43f5e,#e11d48)' : q.lag > 60 ? 'linear-gradient(90deg,#f59e0b,#f43f5e)' : 'linear-gradient(90deg,#06b6d4,#34d399)' }" />
              </div>
            </div>
          </div>
          <div v-else style="height:200px; display:flex; align-items:center; justify-content:center; color:var(--text-low); font-size:13px;">
            No consumer groups
          </div>
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
import { resources, queues as queuesApi, analytics, consumers as consumersApi } from '@/api'
import { formatNumber } from '@/composables/useApi'
import { useRefresh } from '@/composables/useRefresh'
import MetricCard from '@/components/MetricCard.vue'
import BaseChart from '@/components/BaseChart.vue'
import DataTable from '@/components/DataTable.vue'

const overview = ref(null)
const queues = ref([])
const consumers = ref([])
const statusData = ref(null)

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

const getTimeRangeParams = () => {
  const r = timeRanges.find(x => x.value === selectedRange.value) || timeRanges[0]
  const now = new Date()
  return { from: new Date(now.getTime() - r.minutes * 60 * 1000).toISOString(), to: now.toISOString() }
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

const lagColor = (s) => !s || s === 0 ? 'var(--text-mid)' : s < 60 ? '#34d399' : s < 300 ? '#fbbf24' : '#fb7185'
const elColor = (ms) => !ms || ms === 0 ? 'var(--text-mid)' : ms < 50 ? '#34d399' : ms < 100 ? '#fbbf24' : '#fb7185'

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

const chartData = computed(() => {
  if (!statusData.value?.throughput?.length) return { labels: [], datasets: [] }
  const hist = [...statusData.value.throughput].reverse()
  let multi = false
  if (hist.length > 1) { const f = new Date(hist[0].timestamp); const l = new Date(hist[hist.length-1].timestamp); multi = f.toDateString() !== l.toDateString() }
  return {
    labels: hist.map(h => formatChartLabel(new Date(h.timestamp), multi)),
    datasets: [
      { label: 'In (msg/s)', data: hist.map(h => h.ingestedPerSecond || 0), fill: true },
      { label: 'Out (msg/s)', data: hist.map(h => h.processedPerSecond || 0), fill: true },
    ]
  }
})
const chartOptions = { plugins: { legend: { display: false } }, scales: { y: { title: { display: true, text: 'msg/s', font: { size: 11 } } } } }

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
const fetchAll = async () => { await Promise.all([fetchOverview(), fetchQueues(), fetchConsumers(), fetchStatus()]) }

useRefresh(fetchAll)
watch(selectedRange, fetchStatus)

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
</style>

