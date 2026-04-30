<template>
  <div class="view-container">

    <!--
      ========================================================================
      Top bar — range, autorefresh, drilldown chip, last-refresh ticker.
      One slim row, no legend (per-row sparklines are self-evident).
      ========================================================================
    -->
    <div class="dash-bar">
      <div class="seg">
        <button
          v-for="r in timeRanges"
          :key="r.value"
          :class="{ on: selectedRange === r.value }"
          @click="selectedRange = r.value"
        >{{ r.label }}</button>
      </div>

      <div class="dash-live">
        <span class="pulse" />
        <span>live · 30s autorefresh</span>
      </div>

      <div class="dash-bar-right">
        <button
          class="dash-master-toggle"
          @click="toggleAllRows"
          :title="anyExpanded ? 'Collapse every metric chart' : 'Expand every metric into a full chart'"
        >
          <svg width="13" height="13" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
            <!-- Two stacked rows + chevrons. Up-pointing chevrons when
                 anything is expanded (so click "lifts up" / collapses);
                 down-pointing when collapsed (click "drops down" / expands). -->
            <path :d="anyExpanded ? 'M3 7l5-3 5 3M3 11l5-3 5 3' : 'M3 5l5 3 5-3M3 9l5 3 5-3'" />
          </svg>
          <span>{{ anyExpanded ? 'Collapse all' : 'Expand all' }}</span>
        </button>
        <span class="dash-refresh-tick">last refresh {{ refreshAgo }}</span>
      </div>
    </div>

    <!--
      ========================================================================
      Counts strip — cluster scope counts on the left, point-in-time perf
      stats on the right. Both are snapshot values (not time series), so
      they don't belong in the metric table below; this strip is the
      designated home for "what's happening right now, no chart needed".
      The pending count is the only number with a threshold tone.
      ========================================================================
    -->
    <div class="counts-strip">
      <div class="counts-group">
        <button class="count-item" @click="$router.push('/messages')" :disabled="loadingOverview">
          <strong>{{ formatNumber(overview?.messages?.total || 0) }}</strong>
          <span>messages</span>
        </button>
        <span class="count-sep">·</span>
        <button class="count-item" @click="$router.push('/queues')" :disabled="loadingQueues">
          <strong>{{ formatNumber(overview?.queues || 0) }}</strong>
          <span>queues</span>
        </button>
        <span class="count-sep">·</span>
        <span class="count-item count-static">
          <strong>{{ formatNumber(totalPartitions) }}</strong>
          <span>partitions</span>
        </span>
        <span class="count-sep">·</span>
        <button class="count-item" @click="$router.push('/consumers')" :disabled="loadingConsumers">
          <strong>{{ formatNumber(consumers?.length || 0) }}</strong>
          <span>consumer groups</span>
        </button>
        <span class="count-sep">·</span>
        <span class="count-item count-static">
          <strong class="num" :class="pendingNumClass(overview?.messages?.pending)">{{ formatNumber(Math.max(0, overview?.messages?.pending || 0)) }}</strong>
          <span>pending</span>
        </span>
        <span class="count-sep">·</span>
        <span class="count-item count-static count-muted">
          <strong>{{ formatNumber(overview?.messages?.completed || 0) }}</strong>
          <span>completed</span>
        </span>
      </div>

      <!-- Right group — current engine efficiency. Avg rows per batch is a
           snapshot (no time-series), so it lives here rather than in the
           metric table below. Higher = healthier (less per-commit overhead). -->
      <div class="counts-group counts-group-right" :title="'Average rows per batch — push / pop / ack. Higher = healthier engine, less per-commit overhead.'">
        <span class="count-item-label">batch eff</span>
        <span class="count-item count-static count-tight">
          <strong>{{ batchEfficiency.push }}</strong>
          <span class="count-suffix">push</span>
        </span>
        <span class="count-sep">·</span>
        <span class="count-item count-static count-tight">
          <strong>{{ batchEfficiency.pop }}</strong>
          <span class="count-suffix">pop</span>
        </span>
        <span class="count-sep">·</span>
        <span class="count-item count-static count-tight">
          <strong>{{ batchEfficiency.ack }}</strong>
          <span class="count-suffix">ack</span>
        </span>
      </div>
    </div>

    <!--
      ========================================================================
      Metric table — the heart of the redesign. Nine rows, each carries:
        dot · label · value+unit · context · sparkline.
      Color only fires when a row crosses its threshold; the eye scans the
      dot column and stops on the first non-grey one. Order is by operator
      priority (flow → health → admin), not by code structure.
      ========================================================================
    -->
    <div class="metric-table">
      <div class="metric-head">
        <span></span>
        <span>Metric</span>
        <span class="h-value">Now</span>
        <span>Context</span>
        <span class="h-spark">{{ selectedRange }}</span>
        <span></span>
      </div>

      <!-- Flow -->
      <MetricRow
        label="Throughput"
        :value="throughput.current"
        unit="/s"
        :context="throughputContext"
        :series="throughputSeries"
        :labels="chartLabels"
        :value-format="fmtRate"
        expand-unit="msgs / sec"
        :loading="loadingStatus"
        :expanded="isExpanded('throughput')"
        @toggle-expand="toggleRow('throughput')"
      />
      <MetricRow
        label="Pending Δ"
        :value="pendingDeltaDisplay"
        unit="msgs"
        :context="pendingDeltaContext"
        :sparkline="pendingDeltaSeries"
        :labels="chartLabels"
        :value-format="fmtCount"
        expand-unit="msgs (cumulative)"
        :severity="pendingDeltaSeverity"
        :loading="loadingStatus"
        tooltip="Cumulative (push − ack) over the selected window. Positive = falling behind, negative = catching up."
        :expanded="isExpanded('pendingDelta')"
        @toggle-expand="toggleRow('pendingDelta')"
      />
      <!-- Parked + Fill ratio sit between Pending Δ and Time lag because
           together they answer the consumer-side of the flow story:
           who's waiting (Parked) and how often they're actually fed
           (Fill). Time lag below tells you how *late* messages are
           when they finally do get delivered. -->
      <MetricRow
        label="Parked"
        :value="formatNumber(Math.round(parkedLatest))"
        unit="consumers"
        :context="parkedContext"
        :series="parkedSeriesData"
        :labels="partitionLabels"
        :value-format="fmtCount"
        expand-unit="long-polls"
        :loading="loadingStatus"
        tooltip="Long-poll consumer connections currently waiting for work, summed across all queues. Approximates idle connected consumers (busy consumers, mid-job, not counted)."
        :expanded="isExpanded('parked')"
        @toggle-expand="toggleRow('parked')"
      />
      <MetricRow
        label="Fill ratio"
        :context="fillContext"
        :series="fillSeriesData"
        :labels="partitionLabels"
        :value-format="fmtFillPct"
        expand-unit="%"
        :severity="fillSeverity"
        :loading="loadingStatus"
        tooltip="Long-polls returning a message ÷ all long-poll completions, across all queues. Below 30% with traffic = consumers mostly waiting (over-provisioned); near 100% sustained = consumers fully utilized — watch the Time lag row for under-provisioning."
        :expanded="isExpanded('fillRatio')"
        @toggle-expand="toggleRow('fillRatio')"
      >
        <template #value>
          <template v-if="fillLatest === null">
            <span class="num">—</span>
          </template>
          <template v-else>
            <span class="num" :class="fillSeverity">{{ fillLatest.toFixed(1) }}</span><i class="mr-unit">%</i>
          </template>
        </template>
      </MetricRow>
      <MetricRow
        label="Time lag"
        :context="'avg / max p99 across consumer groups'"
        :series="lagSeriesData"
        :labels="chartLabels"
        :value-format="fmtLagMs"
        expand-unit="ms"
        :severity="lagSeverity"
        :loading="loadingOverview"
        :expanded="isExpanded('timeLag')"
        @toggle-expand="toggleRow('timeLag')"
      >
        <template #value>
          <span class="num" :class="lagNumClass(overview?.lag?.time?.avg)">{{ formatDuration(overview?.lag?.time?.avg || 0) }}</span>
          <span class="mr-sep">/</span>
          <span class="num" :class="lagNumClass(overview?.lag?.time?.max)">{{ formatDuration(overview?.lag?.time?.max || 0) }}</span>
        </template>
      </MetricRow>

      <!-- Health -->
      <MetricRow
        label="Errors"
        :value="formatNumber(errorTotal)"
        :context="errorContext"
        :series="errorSeriesData"
        :labels="chartLabels"
        :value-format="fmtCount"
        expand-unit="count"
        :severity="errorSeverity"
        :clickable="errorTotal > 0"
        @click="$router.push('/dlq')"
        :loading="loadingStatus"
        :expanded="isExpanded('errors')"
        @toggle-expand="toggleRow('errors')"
      />
      <MetricRow
        label="Event loop"
        :context="`${workerCount || 0} worker${workerCount === 1 ? '' : 's'} · avg / max`"
        :series="elSeriesData"
        :labels="chartLabels"
        :value-format="(v) => v + ' ms'"
        expand-unit="ms"
        :severity="elNumClass(maxEventLoopLag)"
        :loading="loadingStatus"
        :expanded="isExpanded('eventLoop')"
        @toggle-expand="toggleRow('eventLoop')"
      >
        <template #value>
          <span class="num" :class="elNumClass(avgEventLoopLag)">{{ avgEventLoopLag }}<i class="mr-unit">ms</i></span>
          <span class="mr-sep">/</span>
          <span class="num" :class="elNumClass(maxEventLoopLag)">{{ maxEventLoopLag }}<i class="mr-unit">ms</i></span>
        </template>
      </MetricRow>
      <MetricRow
        label="Queen CPU"
        :value="cpuLatest.toFixed(1)"
        unit="%"
        :context="cpuContext"
        :series="cpuSeriesData"
        :labels="cpuLabels"
        :value-format="(v) => v.toFixed(1) + '%'"
        expand-unit="%"
        :loading="loadingStatus"
        :expanded="isExpanded('cpu')"
        @toggle-expand="toggleRow('cpu')"
      />
      <MetricRow
        label="DB pool"
        :context="poolContext"
        :series="poolSeriesData"
        :labels="chartLabels"
        :value-format="(v) => Math.round(v) + ' conns'"
        expand-unit="connections"
        :severity="poolSeverity"
        :loading="loadingStatus"
        :expanded="isExpanded('dbPool')"
        @toggle-expand="toggleRow('dbPool')"
      >
        <template #value>
          <span class="num" :class="poolSeverity">{{ poolLatest?.active ?? 0 }}</span>
          <span class="mr-sep">/</span>
          <span class="num">{{ poolLatest?.size ?? '—' }}</span>
          <i class="mr-unit">conns</i>
        </template>
      </MetricRow>

      <!-- Admin -->
      <MetricRow
        label="Partitions"
        :context="'created / deleted in window'"
        :series="partitionSeriesData"
        :labels="partitionLabels"
        :value-format="fmtCount"
        expand-unit="count"
        :expanded="isExpanded('partitions')"
        @toggle-expand="toggleRow('partitions')"
      >
        <template #value>
          <span class="num" style="color:var(--text-hi);">+{{ formatNumber(partitionCreatedTotal) }}</span>
          <span class="mr-sep">/</span>
          <span class="num mute">−{{ formatNumber(partitionDeletedTotal) }}</span>
        </template>
      </MetricRow>
      <MetricRow
        label="Retention"
        :value="formatNumber(retentionTotal)"
        unit="msgs"
        context="evicted + completed-retention in window"
        :series="retentionSeriesData"
        :labels="retentionLabels"
        :value-format="fmtCount"
        expand-unit="msgs"
        :expanded="isExpanded('retention')"
        @toggle-expand="toggleRow('retention')"
      />
    </div>

    <!--
      ========================================================================
      Bottom row — two symmetric panels using the same row idiom:
          dot · name · right-side metric  |  meta line below.
      Top queues is sorted by pending depth (the operational priority);
      consumer groups by max time lag (the operational symptom). Click a
      row to drill into its detail page; "see all" footer links the full
      list view.
      ========================================================================
    -->
    <div class="grid-2">
      <div class="card">
        <div class="card-header">
          <h3>Top queues by pending</h3>
          <span class="muted">{{ enrichedQueues.length }} queues</span>
        </div>

        <div v-if="loadingQueues" class="card-body entity-list">
          <div v-for="i in 6" :key="i" class="skeleton" style="height:48px; border-radius:8px;" />
        </div>

        <div v-else-if="topPendingQueues.length" class="card-body entity-list">
          <button
            v-for="q in topPendingQueues"
            :key="q.name"
            class="entity-row"
            @click="$router.push(`/queues/${encodeURIComponent(q.name)}`)"
          >
            <div class="entity-head">
              <span class="status-dot" :class="statusDotClass(q._status)" />
              <span class="entity-name">{{ q.name }}</span>
              <span class="entity-right num" :class="lagNumClass(q._lag)">
                {{ q._lag > 0 ? formatDuration(q._lag) : '—' }}
              </span>
            </div>
            <div class="entity-meta">
              <span class="bar bar-meta">
                <i :class="depthBarClass(q._status)" :style="{ width: q._depthPct + '%' }" />
              </span>
              <span class="meta-text">
                <strong>{{ formatNumber(q._pending) }}</strong> pending
                <span class="meta-sep">·</span>
                {{ q.partitions || 1 }} {{ (q.partitions || 1) === 1 ? 'part' : 'parts' }}
              </span>
            </div>
          </button>
        </div>

        <div v-else class="card-body entity-empty">No queues</div>

        <div class="card-foot">
          <a class="card-foot-link" @click="$router.push('/queues')">See all queues →</a>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <h3>Consumer groups by lag</h3>
          <span class="muted">{{ consumers.length }} total · {{ laggingCount }} lagging</span>
        </div>

        <div v-if="loadingConsumers" class="card-body entity-list">
          <div v-for="i in 6" :key="i" class="skeleton" style="height:48px; border-radius:8px;" />
        </div>

        <div v-else-if="sortedConsumers.length" class="card-body entity-list">
          <button
            v-for="g in sortedConsumers.slice(0, 6)"
            :key="g.name + '@' + g.queueName"
            class="entity-row"
            @click="$router.push('/consumers')"
          >
            <div class="entity-head">
              <span class="status-dot" :class="cgDotClass(g)" />
              <span class="entity-name">{{ g.queueName || '?' }}</span>
              <span class="entity-right num" :class="lagNumClass(g.maxTimeLag)">
                {{ (g.maxTimeLag || 0) > 0 ? formatDuration(g.maxTimeLag) : '—' }}
              </span>
            </div>
            <div class="entity-meta">
              <span class="meta-text">
                <span v-if="g.name === '__QUEUE_MODE__'" class="meta-tag">queue mode</span>
                <span v-else><strong>{{ g.name }}</strong></span>
                <span class="meta-sep">·</span>
                {{ g.members || 0 }} {{ (g.members || 0) === 1 ? 'member' : 'members' }}
                <template v-if="(g.partitionsWithLag || 0) > 0">
                  <span class="meta-sep">·</span>
                  <span class="num warn">{{ g.partitionsWithLag }} lagging</span>
                </template>
              </span>
            </div>
          </button>
        </div>

        <div v-else class="card-body entity-empty">No consumer groups</div>

        <div class="card-foot">
          <a class="card-foot-link" @click="$router.push('/consumers')">See all consumer groups →</a>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import { resources, queues as queuesApi, analytics, consumers as consumersApi, system as systemApi } from '@/api'
import { formatNumber, toNum, latestFinite, trimIncompleteBuckets } from '@/composables/useApi'
import { useRefresh } from '@/composables/useRefresh'
import MetricRow from '@/components/MetricRow.vue'

// ---------------------------------------------------------------------------
// State (unchanged from prior dashboard — same data sources)
// ---------------------------------------------------------------------------
const overview = ref(null)
const queues = ref([])
const consumers = ref([])
const statusData = ref(null)
const retentionData = ref([])
const partitionOpsData = ref([])
const systemMetricsData = ref(null)

const loadingOverview = ref(true)
const loadingQueues = ref(true)
const loadingConsumers = ref(true)
const loadingStatus = ref(true)

const selectedRange = ref('1h')
const timeRanges = [
  { label: '1h',  value: '1h',  minutes: 60 },
  { label: '6h',  value: '6h',  minutes: 360 },
  { label: '24h', value: '24h', minutes: 1440 },
]

// ---------------------------------------------------------------------------
// Expand state — per-row + master toggle.
// We use a Set keyed by stable row ids (not the human-readable label)
// so future label tweaks don't blow away the user's expanded selection.
// ---------------------------------------------------------------------------
const ALL_ROW_KEYS = [
  'throughput', 'pendingDelta', 'parked', 'fillRatio', 'timeLag',
  'errors', 'eventLoop', 'cpu', 'dbPool',
  'partitions', 'retention',
]
const expandedRows = ref(new Set())
const isExpanded = (key) => expandedRows.value.has(key)
const toggleRow = (key) => {
  const next = new Set(expandedRows.value)
  if (next.has(key)) next.delete(key); else next.add(key)
  expandedRows.value = next
}
const anyExpanded = computed(() => expandedRows.value.size > 0)
const toggleAllRows = () => {
  expandedRows.value = anyExpanded.value ? new Set() : new Set(ALL_ROW_KEYS)
}

// Dashboard is cluster-wide by design. Per-queue investigation lives on
// /queues/[name] (and will get its own metric table when that page is
// redesigned), so we no longer host a queue-scope filter here.
const getTimeRangeParams = () => {
  const r = timeRanges.find(x => x.value === selectedRange.value) || timeRanges[0]
  const now = new Date()
  return {
    from: new Date(now.getTime() - r.minutes * 60 * 1000).toISOString(),
    to: now.toISOString(),
  }
}

// ---------------------------------------------------------------------------
// History (oldest → newest) — single source for every chart below.
// chartLabels are pre-formatted timestamps so RowChart tooltips can render
// "01:33 PM" or "Apr 28, 01:33 PM" as the title without the children
// having to know about the time-axis convention.
// ---------------------------------------------------------------------------
const history = computed(() => {
  if (!statusData.value?.throughput?.length) return []
  // Status v3 returns throughput rows newest → oldest; reverse for charts.
  // Then drop the in-flight current bucket so the right edge of each
  // chart doesn't show the partial-minute sample reading low/zero.
  const sorted = [...statusData.value.throughput].reverse()
  return trimIncompleteBuckets(sorted, {
    bucketKey: 'timestamp',
    bucketMinutes: statusData.value?.bucketMinutes || 1,
  })
})
const multiDay = computed(() => {
  const h = history.value
  if (h.length < 2) return false
  return new Date(h[0].timestamp).toDateString() !== new Date(h[h.length - 1].timestamp).toDateString()
})
const formatChartLabel = (date, multi) => multi
  ? date.toLocaleString('en-US', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' })
  : date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' })
const chartLabels = computed(() =>
  history.value.map(h => formatChartLabel(new Date(h.timestamp), multiDay.value))
)

// ---------------------------------------------------------------------------
// Counts strip helpers
// ---------------------------------------------------------------------------
const totalPartitions = computed(() =>
  queues.value.reduce((sum, q) => sum + (q.partitions || 1), 0)
)

// ---------------------------------------------------------------------------
// Throughput row — three real series (push / pop / ack) so the operator
// can hover and see all three rates at any moment in time. ack is given
// the semantic green color (healthy completion) so it visually pops when
// it falls behind push.
// ---------------------------------------------------------------------------
const throughput = computed(() => {
  if (!overview.value?.throughput) return { current: '0.0' }
  const current =
    overview.value.throughput.ingestedPerSecond ||
    overview.value.throughput.processedPerSecond || 0
  return { current: current.toFixed(1) }
})
const throughputSeries = computed(() => {
  const h = history.value
  if (!h.length) return null
  return [
    { label: 'Push', data: h.map(x => toNum(x.ingestedPerSecond)) },
    { label: 'Pop',  data: h.map(x => toNum(x.popPerSecond)) },
    { label: 'Ack',  data: h.map(x => toNum(x.processedPerSecond)), color: '#4ade80' },
  ]
})
const throughputPeak = computed(() => {
  const h = history.value
  if (!h.length) return 0
  // Math.max ignores nulls when filtered first; default to 0 if all missing.
  const finite = h.map(x => toNum(x.ingestedPerSecond)).filter(v => v !== null)
  return finite.length ? Math.max(0, ...finite) : 0
})
const throughputContext = computed(() => {
  const peak = throughputPeak.value
  const cur = Number(throughput.value.current) || 0
  if (peak === 0 && cur === 0) return 'idle · no traffic in window'
  if (peak === 0) return `current ${cur.toFixed(1)} /s`
  return `peak ${formatNumber(Math.round(peak))} /s · push ≈ ack`
})

// ---------------------------------------------------------------------------
// Pending Δ row — cumulative (ingested - processed) across the window.
// Positive = falling behind, negative = catching up. The severity tone is
// what makes this row tell its story: grey at zero, amber when growing,
// red when growing fast, green only when actively shrinking by a lot.
// ---------------------------------------------------------------------------
// Cumulative push − ack across the window. When a bucket's source values
// are missing we emit `null` (not 0) so the chart shows a gap instead of
// the line lying flat. The cumulative carries forward across gaps so the
// next valid bucket continues from the last known total.
const pendingDeltaSeries = computed(() => {
  const h = history.value
  if (!h.length) return []
  let cum = 0
  return h.map(x => {
    const ingested = toNum(x.ingested)
    const processed = toNum(x.processed)
    if (ingested === null && processed === null) return null
    cum += (ingested || 0) - (processed || 0)
    return cum
  })
})
const pendingDeltaLatest = computed(() => latestFinite(pendingDeltaSeries.value) ?? 0)
const pendingDeltaDisplay = computed(() => {
  const v = pendingDeltaLatest.value
  if (v === 0) return '0'
  return (v > 0 ? '+' : '−') + formatNumber(Math.abs(v))
})
const pendingDeltaContext = computed(() => {
  const v = pendingDeltaLatest.value
  if (v === 0) return 'flat · push = ack across window'
  if (v > 0) return 'falling behind · push > ack'
  return 'catching up · ack > push'
})
const pendingDeltaSeverity = computed(() => {
  const v = pendingDeltaLatest.value
  if (v > 100000) return 'bad'
  if (v > 1000)   return 'warn'
  if (v < -1000)  return 'ok'
  return ''
})

// ---------------------------------------------------------------------------
// Time lag row — uses overview.lag.time.{avg,max} (seconds) for the value
// shown left-of-chart. The chart pulls avg + max from history.{avg,max}LagMs
// so hovering the chart reveals both at any given timestamp.
// ---------------------------------------------------------------------------
const lagSeriesData = computed(() => {
  const h = history.value
  if (!h.length) return null
  return [
    { label: 'Avg', data: h.map(x => toNum(x.avgLagMs)) },
    { label: 'Max', data: h.map(x => toNum(x.maxLagMs)) },
  ]
})
const lagNumClass = (s) => !s || s === 0 ? '' : s < 60 ? '' : s < 300 ? 'warn' : 'bad'
const lagSeverity = computed(() => lagNumClass(overview.value?.lag?.time?.max || 0))

// ---------------------------------------------------------------------------
// Errors row
// ---------------------------------------------------------------------------
// Errors chart has TWO series: db errors (red) and ack failures (amber).
// dlqCount is a cumulative snapshot rather than a per-bucket count, so we
// only show its latest value in the context line — never on the chart.
const errorSeriesData = computed(() => {
  const h = history.value
  if (!h.length) return null
  return [
    { label: 'DB errors', data: h.map(x => toNum(x.dbErrors)), color: '#fb7185' },
    { label: 'Ack failed', data: h.map(x => toNum(x.ackFailed)), color: '#e6b450' },
  ]
})
const errorBuckets = computed(() => {
  let db = 0, ack = 0
  for (const x of history.value) {
    db  += toNum(x.dbErrors)  || 0
    ack += toNum(x.ackFailed) || 0
  }
  // DLQ is a cumulative snapshot — read the most recent finite value
  // (skipping over null buckets so an in-flight last bucket doesn't read 0).
  const dlq = latestFinite(history.value.map(x => x.dlqCount)) ?? 0
  return { db, ack, dlq }
})
const errorTotal = computed(() => {
  const b = errorBuckets.value
  return b.db + b.ack + b.dlq
})
const errorContext = computed(() => {
  const b = errorBuckets.value
  return `db ${formatNumber(b.db)} · ack ${formatNumber(b.ack)} · dlq ${formatNumber(b.dlq)}`
})
const errorSeverity = computed(() => {
  const b = errorBuckets.value
  if (b.db > 0 || b.ack > 100) return 'bad'
  if (b.ack > 0 || b.dlq > 0)  return 'warn'
  return ''
})

// ---------------------------------------------------------------------------
// Event loop row
// ---------------------------------------------------------------------------
const workerCount = computed(() => statusData.value?.workers?.length || 0)
const avgEventLoopLag = computed(() => {
  const w = statusData.value?.workers
  if (!w?.length) return 0
  // Average only over workers that actually reported a value — a worker
  // missing the field shouldn't pull the cluster average towards zero.
  const finite = w.map(x => toNum(x.avgEventLoopLagMs)).filter(v => v !== null)
  if (!finite.length) return 0
  return Math.round(finite.reduce((s, x) => s + x, 0) / finite.length)
})
const maxEventLoopLag = computed(() => {
  const w = statusData.value?.workers
  if (!w?.length) return 0
  const finite = w.map(x => toNum(x.maxEventLoopLagMs)).filter(v => v !== null)
  return finite.length ? Math.max(...finite) : 0
})
const elSeriesData = computed(() => {
  const h = history.value
  if (!h.length) return null
  return [
    { label: 'Avg', data: h.map(x => toNum(x.avgEventLoopLagMs)) },
    { label: 'Max', data: h.map(x => toNum(x.maxEventLoopLagMs)) },
  ]
})
const elNumClass = (ms) => !ms || ms === 0 ? '' : ms < 50 ? '' : ms < 100 ? 'warn' : 'bad'

// ---------------------------------------------------------------------------
// Queen CPU row — single replica or "hottest" replica when multi-replica.
// CPU is reported as cumulative-across-cores % (so 4 cores fully pinned =
// 400%); we don't tone it semantic without core count, just show the
// number and a contextual breakdown.
// ---------------------------------------------------------------------------
const hasMultipleReplicas = computed(() =>
  (systemMetricsData.value?.replicas || []).length > 1
)
// Single replica → split into user/system so the chart shows the kernel
// vs userspace work balance. Multi replica → one line per replica so
// fanout imbalance becomes visible (the metric value still shows the
// hottest replica). Both modes are real multi-series charts.
const cpuSeriesData = computed(() => {
  if (hasMultipleReplicas.value) {
    const replicas = systemMetricsData.value?.replicas || []
    return replicas.map(r => ({
      label: r.hostname?.substring(0, 12) || 'replica',
      // If either user_us or system_us is missing for a bucket we have no
      // honest CPU number — emit null so the line renders a gap there.
      data: (r.timeSeries || []).map(t => {
        const user = toNum(t.metrics?.cpu?.user_us?.avg)
        const sys  = toNum(t.metrics?.cpu?.system_us?.avg)
        if (user === null && sys === null) return null
        return ((user || 0) + (sys || 0)) / 100
      }),
    }))
  }
  const h = history.value
  if (!h.length) return null
  return [
    { label: 'User',   data: h.map(x => toNum(x.queenCpuUserPct)) },
    { label: 'System', data: h.map(x => toNum(x.queenCpuSysPct)) },
  ]
})

// CPU labels need their own computed in multi-replica mode, since the
// per-replica time-series buckets aren't necessarily aligned with the
// throughput-history buckets that drive the rest of the rows. Fall back
// to chartLabels when single-replica (data IS history-aligned there).
const cpuLabels = computed(() => {
  if (!hasMultipleReplicas.value) return chartLabels.value
  const replicas = systemMetricsData.value?.replicas || []
  const ts = replicas[0]?.timeSeries || []
  if (!ts.length) return []
  const multi = ts.length >= 2 &&
    new Date(ts[0].timestamp).toDateString() !== new Date(ts[ts.length - 1].timestamp).toDateString()
  return ts.map(t => formatChartLabel(new Date(t.timestamp), multi))
})
// Per-replica CPU "Now" — pull each replica's latest finite combined %
// (so a replica that hasn't reported in the last bucket doesn't drag the
// hottest reading down to 0).
const cpuLatest = computed(() => {
  if (hasMultipleReplicas.value) {
    const replicas = systemMetricsData.value?.replicas || []
    let max = 0
    for (const r of replicas) {
      const ts = r.timeSeries || []
      const combined = ts.map(t => {
        const user = toNum(t.metrics?.cpu?.user_us?.avg)
        const sys  = toNum(t.metrics?.cpu?.system_us?.avg)
        if (user === null && sys === null) return null
        return ((user || 0) + (sys || 0)) / 100
      })
      const v = latestFinite(combined) || 0
      if (v > max) max = v
    }
    return max
  }
  const u = latestFinite(history.value.map(x => x.queenCpuUserPct)) || 0
  const s = latestFinite(history.value.map(x => x.queenCpuSysPct)) || 0
  return u + s
})
const cpuContext = computed(() => {
  if (hasMultipleReplicas.value) {
    const n = (systemMetricsData.value?.replicas || []).length
    return `${n} replicas · hottest shown`
  }
  const u = latestFinite(history.value.map(x => x.queenCpuUserPct))
  const s = latestFinite(history.value.map(x => x.queenCpuSysPct))
  if (u === null && s === null) return '—'
  return `user ${(u || 0).toFixed(0)}% · sys ${(s || 0).toFixed(0)}%`
})

// ---------------------------------------------------------------------------
// DB pool row — saturation = waiters; warn at 80% util, bad once active = size.
// ---------------------------------------------------------------------------
// Pool "Now" reads the most recent finite value per field, since the
// bucket boundary often arrives with system_metrics not yet flushed.
const poolLatest = computed(() => {
  const h = history.value
  if (!h.length) return null
  const active = Math.round(latestFinite(h.map(x => x.dbPoolActive)) ?? 0)
  const idle   = Math.round(latestFinite(h.map(x => x.dbPoolIdle))   ?? 0)
  const sizeRaw = latestFinite(h.map(x => x.dbPoolSize))
  const size   = Math.round(sizeRaw ?? (active + idle))
  return { active, idle, size }
})
const poolSeriesData = computed(() => {
  const h = history.value
  if (!h.length) return null
  return [
    { label: 'Active', data: h.map(x => toNum(x.dbPoolActive)) },
    { label: 'Idle',   data: h.map(x => toNum(x.dbPoolIdle)) },
  ]
})
const poolSeverity = computed(() => {
  const p = poolLatest.value
  if (!p || !p.size) return ''
  if (p.active >= p.size) return 'bad'
  if (p.active / p.size > 0.8) return 'warn'
  return ''
})
const poolContext = computed(() => {
  const p = poolLatest.value
  if (!p) return '—'
  return `idle ${p.idle} · size ${p.size}`
})

// ---------------------------------------------------------------------------
// Partitions row — admin events; no severity, just shape + counts.
// Partitions and retention have their own time-series buckets (separate
// from the throughput history), so they each carry their own labels too.
// ---------------------------------------------------------------------------
const partitionSeriesData = computed(() => {
  const rows = partitionOpsData.value || []
  if (!rows.length) return null
  return [
    { label: 'Created', data: rows.map(r => toNum(r.partitionsCreated)) },
    { label: 'Deleted', data: rows.map(r => toNum(r.partitionsDeleted)) },
  ]
})
const partitionLabels = computed(() =>
  (partitionOpsData.value || []).map(r => formatChartLabel(new Date(r.bucket), false))
)
const partitionCreatedTotal = computed(() =>
  (partitionOpsData.value || []).reduce((s, r) => s + (toNum(r.partitionsCreated) || 0), 0)
)
const partitionDeletedTotal = computed(() =>
  (partitionOpsData.value || []).reduce((s, r) => s + (toNum(r.partitionsDeleted) || 0), 0)
)

// ---------------------------------------------------------------------------
// Parked row — gauge: in-flight long-poll consumer connections, summed
// across all queues per bucket. Approximates "idle connected consumers"
// at any moment in time (busy consumers, mid-job, are NOT counted —
// they're not parked). The latest bucket is what reads on the row; the
// sparkline is the trend across the selected window.
// ---------------------------------------------------------------------------
const parkedSeriesData = computed(() => {
  const rows = partitionOpsData.value || []
  if (!rows.length) return null
  return [{ label: 'Parked', data: rows.map(r => toNum(r.parkedTotal)) }]
})
const parkedLatest = computed(() => {
  const rows = partitionOpsData.value || []
  // Walk back to the most recent populated bucket — avoids reading "0
  // parked" from a still-aggregating tail bucket.
  return latestFinite(rows.map(r => r.parkedTotal)) ?? 0
})
const parkedAvg = computed(() => {
  const rows = partitionOpsData.value || []
  if (!rows.length) return 0
  const finite = rows.map(r => toNum(r.parkedTotal)).filter(v => v !== null)
  if (!finite.length) return 0
  return finite.reduce((s, v) => s + v, 0) / finite.length
})
const parkedContext = computed(() => {
  const rows = partitionOpsData.value || []
  if (!rows.length) return '—'
  return `avg ${parkedAvg.value.toFixed(1)} across window · idle long-polls`
})

// ---------------------------------------------------------------------------
// Fill ratio row — popMessages / (popMessages + popEmpty) aggregated across
// queues per bucket, rendered as a percentage. The single best "is my
// consumer pool sized right?" signal computable from queue-ops alone:
//   • near 0% with traffic → consumers waiting in vain (over-provisioned)
//   • near 100% sustained  → consumers fully utilized; watch lag for
//                            under-provisioning
// We gate on FILL_NOISE_FLOOR completions per bucket to silence single-event
// noise in quiet windows (and return null so RowChart renders gaps rather
// than misleading drops to zero).
// ---------------------------------------------------------------------------
const FILL_NOISE_FLOOR = 5

// Quiet-bucket handling: RowChart strips non-finite values from its data
// arrays via Number.isFinite, but Number(null) === 0 sneaks past that
// check and would render quiet buckets as a misleading "0% fill" dip.
// Returning NaN/undefined would drop the points but desync the timestamp
// labels in the expanded chart. Pragmatic compromise: carry forward the
// previous bucket's measured value through quiet periods so the sparkline
// stays continuous and honest. The headline "Now" value (fillLatest)
// uses its own gating with a real null → "—" to make truly-idle windows
// distinguishable in the row's value cell.
const fillSeriesData = computed(() => {
  const rows = partitionOpsData.value || []
  if (!rows.length) return null
  // We distinguish three states per bucket:
  //   - null source data       → emit null (real gap)
  //   - <FILL_NOISE_FLOOR pops → carry the last computed value (so the
  //                              chart stays visually continuous through
  //                              quiet but populated minutes)
  //   - enough activity        → compute and remember it
  let last = null
  const data = rows.map(r => {
    const pop = toNum(r.popMessages)
    const empty = toNum(r.popEmpty)
    if (pop === null && empty === null) return null
    const total = (pop || 0) + (empty || 0)
    if (total < FILL_NOISE_FLOOR) return last
    last = Math.round(((pop || 0) / total) * 1000) / 10
    return last
  })
  return [{ label: 'Fill', data }]
})

// "Now" value smoothed across the last few buckets so a single noisy
// minute doesn't dictate the headline number. Returns null when the
// recent window has too little long-poll activity to compute meaningfully.
const fillLatest = computed(() => {
  const rows = partitionOpsData.value || []
  if (!rows.length) return null
  const tail = rows.slice(-5)
  let pop = 0, empty = 0
  for (const r of tail) {
    pop   += toNum(r.popMessages) || 0
    empty += toNum(r.popEmpty)    || 0
  }
  if (pop + empty < FILL_NOISE_FLOOR) return null
  return Math.round((pop / (pop + empty)) * 1000) / 10
})

const fillContext = computed(() => {
  const v = fillLatest.value
  if (v === null) return 'idle window · no long-poll activity'
  if (v >= 80) return 'high utilization · consumers busy serving'
  if (v < 30)  return 'low utilization · consumers mostly empty'
  return 'balanced · consumer pool sized OK'
})

// We only flag low fill as `warn` — high fill is a positive signal in
// isolation (becomes a problem only when paired with rising lag, which
// already has its own row). Without traffic, leave neutral.
const fillSeverity = computed(() => {
  const v = fillLatest.value
  if (v === null) return ''
  if (v < 30) return 'warn'
  return ''
})

// ---------------------------------------------------------------------------
// Retention row — three series (retention sweep, completed-retention sweep,
// hard eviction). Only "Evicted" gets the warn color; the others are quiet.
// ---------------------------------------------------------------------------
const retentionSeriesData = computed(() => {
  const rows = retentionData.value || []
  if (!rows.length) return null
  return [
    { label: 'Retention', data: rows.map(r => toNum(r.retentionMsgs)) },
    { label: 'Completed', data: rows.map(r => toNum(r.completedRetentionMsgs)) },
    { label: 'Evicted',   data: rows.map(r => toNum(r.evictionMsgs)), color: '#e6b450' },
  ]
})
const retentionLabels = computed(() =>
  (retentionData.value || []).map(r => formatChartLabel(new Date(r.bucket), false))
)
const retentionTotal = computed(() =>
  (retentionData.value || []).reduce((s, r) =>
    s +
    (toNum(r.retentionMsgs) || 0) +
    (toNum(r.completedRetentionMsgs) || 0) +
    (toNum(r.evictionMsgs) || 0)
  , 0)
)

// ---------------------------------------------------------------------------
// Batch efficiency row — point-in-time averages (no series), shows whether
// batching is healthy. <5 means we're committing tiny batches (overhead-bound).
// ---------------------------------------------------------------------------
const batchEfficiency = computed(() => {
  const b = statusData.value?.messages?.batchEfficiency
  return {
    push: b?.push?.toFixed(1) || '0',
    pop:  b?.pop?.toFixed(1)  || '0',
    ack:  b?.ack?.toFixed(1)  || '0',
  }
})

// ---------------------------------------------------------------------------
// Counts strip — pending tone (only number that gets thresholded inline)
// ---------------------------------------------------------------------------
const pendingNumClass = (n) => !n || n < 1000 ? '' : n < 10000 ? 'warn' : 'bad'

// ---------------------------------------------------------------------------
// Bottom panels — Top queues by pending + Consumer groups by lag.
// Both panels share one row idiom (dot · name · right metric · meta line),
// so the entity-row / .entity-head / .entity-meta classes are applied
// identically below regardless of which entity is rendered.
// ---------------------------------------------------------------------------

// Per-queue worst-lag rollup, keyed off consumer-group lag (the queue
// itself doesn't carry a lag; it's a property of its consumer groups).
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
      const status =
        lag >= 300 || (q._pending / maxPending) > 0.8 ? 'degraded'
      : lag >= 60  || (q._pending / maxPending) > 0.5 ? 'watch'
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

// Top 6 by pending depth — the operational priority for "what's piling up".
const topPendingQueues = computed(() =>
  enrichedQueues.value.filter(q => q._pending > 0).slice(0, 6)
    // If no queue has pending, still show the top 6 (mostly to populate
    // the panel with something rather than the empty state).
    .concat(enrichedQueues.value.filter(q => q._pending === 0).slice(0, 6))
    .slice(0, 6)
)

const sortedConsumers = computed(() =>
  [...consumers.value].sort((a, b) => (b.maxTimeLag || 0) - (a.maxTimeLag || 0))
)
const laggingCount = computed(() =>
  consumers.value.filter(c => (c.maxTimeLag || 0) >= 60 || (c.partitionsWithLag || 0) > 0).length
)

// Severity → status-dot class mapping. Reused for both panels.
const statusDotClass = (s) =>
  s === 'degraded' ? 'status-dot-danger'
: s === 'watch'    ? 'status-dot-warning'
                   : 'status-dot-success'

const cgStatus = (g) => {
  const lag = g.maxTimeLag || 0
  if (lag >= 300) return 'stuck'
  if (lag >= 60 || (g.partitionsWithLag || 0) > 0) return 'lag'
  return 'healthy'
}
const cgDotClass = (g) => {
  const s = cgStatus(g)
  return s === 'stuck' ? 'status-dot-danger'
       : s === 'lag'   ? 'status-dot-warning'
                       : 'status-dot-success'
}

// Bar fill color follows queue status (same severity vocabulary).
const depthBarClass = (s) => s === 'degraded' ? 'bad' : s === 'watch' ? 'warn' : ''

// ---------------------------------------------------------------------------
// Tooltip value formatters — passed to RowChart so hover tooltips render
// human-friendly numbers instead of raw floats. fmtRate caps precision the
// same way the Queues page does (no IEEE-754 tails).
// ---------------------------------------------------------------------------
const fmtRate = (n) => {
  const v = Number(n) || 0
  if (Math.abs(v) >= 1000) return (v / 1000).toFixed(1) + 'k /s'
  if (Math.abs(v) >= 100)  return Math.round(v) + ' /s'
  if (Math.abs(v) >= 10)   return v.toFixed(1) + ' /s'
  return v.toFixed(2) + ' /s'
}
const fmtCount = (n) => {
  const v = Number(n) || 0
  return formatNumber(Math.round(v))
}
const fmtLagMs = (n) => {
  const v = Number(n) || 0
  if (v < 1) return '0'
  if (v < 1000) return Math.round(v) + ' ms'
  if (v < 60000) return (v / 1000).toFixed(1) + ' s'
  return (v / 60000).toFixed(1) + ' m'
}
// Fill ratio tooltip formatter — the Fill row inserts null in `data` for
// buckets that didn't have enough long-poll activity to compute meaningfully.
// We render those as an em dash so the user can tell "no data" apart from
// "0% delivered".
const fmtFillPct = (n) => {
  if (n === null || n === undefined) return '—'
  const v = Number(n)
  if (!Number.isFinite(v)) return '—'
  return v.toFixed(1) + '%'
}

// ---------------------------------------------------------------------------
// Helpers — duration + last-refresh ticker
// ---------------------------------------------------------------------------
const formatDuration = (seconds) => {
  if (!seconds || seconds === 0) return '0s'
  if (seconds < 60) return `${Math.round(seconds)}s`
  if (seconds < 3600) {
    const m = Math.floor(seconds / 60); const s = Math.round(seconds % 60)
    return s ? `${m}m ${s}s` : `${m}m`
  }
  if (seconds < 86400) {
    const h = Math.floor(seconds / 3600); const m = Math.floor((seconds % 3600) / 60)
    return m ? `${h}h ${m}m` : `${h}h`
  }
  const d = Math.floor(seconds / 86400); const h = Math.floor((seconds % 86400) / 3600)
  return h ? `${d}d ${h}h` : `${d}d`
}

const lastRefreshAt = ref(null)
const nowTick = ref(Date.now())
const refreshAgo = computed(() => {
  if (!lastRefreshAt.value) return '—'
  const sec = Math.max(0, Math.floor((nowTick.value - lastRefreshAt.value) / 1000))
  if (sec < 5) return 'just now'
  if (sec < 60) return `${sec}s ago`
  if (sec < 3600) return `${Math.floor(sec / 60)}m ago`
  return `${Math.floor(sec / 3600)}h ago`
})

// ---------------------------------------------------------------------------
// Fetchers (unchanged)
// ---------------------------------------------------------------------------
const fetchOverview = async () => {
  if (!overview.value) loadingOverview.value = true
  try { overview.value = (await resources.getOverview()).data } catch {}
  finally { loadingOverview.value = false }
}
const fetchQueues = async () => {
  if (!queues.value.length) loadingQueues.value = true
  try { const r = await queuesApi.list(); queues.value = r.data?.queues || r.data || [] } catch {}
  finally { loadingQueues.value = false }
}
const fetchConsumers = async () => {
  if (!consumers.value.length) loadingConsumers.value = true
  try {
    const r = await consumersApi.list()
    consumers.value = Array.isArray(r.data) ? r.data : r.data?.consumer_groups || []
  } catch {}
  finally { loadingConsumers.value = false }
}
const fetchStatus = async () => {
  if (!statusData.value) loadingStatus.value = true
  try { statusData.value = (await analytics.getStatus(getTimeRangeParams())).data } catch {}
  finally { loadingStatus.value = false }
}
const fetchRetention = async () => {
  try {
    const r = await systemApi.getRetention(getTimeRangeParams())
    const series = r.data?.series || []
    // Trim the still-aggregating current bucket so the right edge of the
    // retention sparkline doesn't read a partial-minute sample.
    retentionData.value = trimIncompleteBuckets(series, {
      bucketKey: 'bucket',
      bucketMinutes: r.data?.bucketMinutes || 1,
    })
  } catch { retentionData.value = [] }
}
const fetchQueueOps = async () => {
  try {
    const r = await systemApi.getQueueOps(getTimeRangeParams())
    const series = r.data?.series || []
    // Roll up the per-(queue, bucket) series into one row per bucket.
    // Beyond the original partition lifecycle counters, we also accumulate
    // parked / popMessages / popEmpty across queues so the Parked and
    // Fill ratio rows below can chart cluster-wide gauges/derivations.
    // (parkedCount is a SUM across workers per (queue, bucket), and we
    // SUM again here across queues for the cluster-wide gauge — a
    // legitimate gauge composition since each long-poll lives on exactly
    // one (queue, partition, worker).)
    const byBucket = {}
    for (const row of series) {
      const b = byBucket[row.bucket] ||= {
        bucket: row.bucket,
        partitionsCreated: 0, partitionsDeleted: 0,
        parkedTotal: 0, popMessages: 0, popEmpty: 0
      }
      b.partitionsCreated += Number(row.partitionsCreated) || 0
      b.partitionsDeleted += Number(row.partitionsDeleted) || 0
      b.parkedTotal       += Number(row.parkedCount)      || 0
      b.popMessages       += Number(row.popMessages)      || 0
      b.popEmpty          += Number(row.popEmpty)         || 0
    }
    const sorted = Object.values(byBucket).sort((a, b) => a.bucket.localeCompare(b.bucket))
    partitionOpsData.value = trimIncompleteBuckets(sorted, {
      bucketKey: 'bucket',
      bucketMinutes: r.data?.bucketMinutes || 1,
    })
  } catch { partitionOpsData.value = [] }
}
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
  lastRefreshAt.value = Date.now()
}

useRefresh(fetchAll)
watch(selectedRange, () => { fetchStatus(); fetchRetention(); fetchQueueOps(); fetchSystemMetrics() })

let interval = null
let tickInterval = null
onMounted(() => {
  fetchAll()
  interval = setInterval(fetchAll, 30000)
  tickInterval = setInterval(() => { nowTick.value = Date.now() }, 1000)
})
onUnmounted(() => {
  if (interval) clearInterval(interval)
  if (tickInterval) clearInterval(tickInterval)
})
</script>

<style scoped>
/* ---------------------------------------------------------------------------
   Top bar
   --------------------------------------------------------------------------- */
.dash-bar {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 14px;
  flex-wrap: wrap;
}
.dash-live {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 11px;
  font-family: 'JetBrains Mono', monospace;
  color: var(--text-mid);
}
.dash-bar-right {
  margin-left: auto;
  display: flex;
  align-items: center;
  gap: 12px;
}
.dash-refresh-tick {
  font-size: 11px;
  font-family: 'JetBrains Mono', monospace;
  color: var(--text-low);
}
.dash-master-toggle {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 4px 10px 4px 8px;
  border-radius: 6px;
  border: 1px solid var(--bd);
  background: var(--ink-2);
  color: var(--text-mid);
  font-size: 11px;
  font-family: 'JetBrains Mono', monospace;
  cursor: pointer;
  transition: color .12s var(--ease), border-color .12s var(--ease), background .12s var(--ease);
}
.dash-master-toggle:hover {
  color: var(--text-hi);
  border-color: var(--bd-hi);
  background: var(--ink-3);
}

/* ---------------------------------------------------------------------------
   Counts strip — cluster scope (left) + point-in-time perf stats (right).
   Splits into two visual groups so the eye reads "what we have" separately
   from "how the engine is performing right now". Both are snapshot stats,
   no time series — they live here so the metric table below can be
   exclusively about charts.
   --------------------------------------------------------------------------- */
.counts-strip {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 14px 24px;
  padding: 12px 16px;
  margin-bottom: 14px;
  border: 1px solid var(--bd);
  background: var(--ink-2);
  border-radius: 8px;
}
.counts-group {
  display: flex;
  align-items: baseline;
  flex-wrap: wrap;
  gap: 10px;
}
.counts-group-right {
  /* Slightly muted so the eye reads cluster counts first; perf stats
     are still important but secondary. */
  color: var(--text-low);
}
.count-item-label {
  font-family: 'JetBrains Mono', monospace;
  font-size: 10.5px;
  letter-spacing: .08em;
  text-transform: uppercase;
  color: var(--text-low);
  margin-right: 2px;
}
.count-item {
  display: inline-flex;
  align-items: baseline;
  gap: 6px;
  background: transparent;
  border: none;
  padding: 0;
  color: var(--text-mid);
  font-size: 12.5px;
  cursor: pointer;
  transition: color .12s var(--ease);
}
.count-item.count-static { cursor: default; }
.count-item.count-tight { gap: 4px; }
.count-item:not(:disabled):not(.count-static):hover { color: var(--text-hi); }
.count-item:not(:disabled):not(.count-static):hover strong { color: var(--accent); }
.count-item:disabled { opacity: .5; cursor: default; }
.count-item strong {
  font-family: 'JetBrains Mono', monospace;
  font-variant-numeric: tabular-nums;
  font-size: 14px;
  font-weight: 600;
  color: var(--text-hi);
  letter-spacing: -.005em;
}
.counts-group-right .count-item strong {
  /* Perf stats are secondary, so use medium contrast instead of high.
     The numbers still read clearly but don't compete with the cluster
     scope on the left. */
  color: var(--text-mid);
  font-size: 13px;
}
.count-suffix {
  font-size: 10.5px;
  font-family: 'JetBrains Mono', monospace;
  color: var(--text-low);
  letter-spacing: .04em;
  text-transform: uppercase;
}
.count-muted strong { color: var(--text-mid); }
.count-sep {
  color: var(--bd-hi);
  font-size: 11px;
  user-select: none;
}

/* ---------------------------------------------------------------------------
   Metric table — the heart of the redesign.
   --------------------------------------------------------------------------- */
.metric-table {
  border: 1px solid var(--bd);
  background: var(--ink-2);
  border-radius: 8px;
  overflow: hidden;
  margin-bottom: 14px;
}
.metric-head {
  display: grid;
  grid-template-columns: 14px 150px 160px 1fr 260px 24px;
  gap: 16px;
  padding: 9px 16px;
  font-size: 10.5px;
  font-weight: 600;
  letter-spacing: .08em;
  text-transform: uppercase;
  color: var(--text-low);
  border-bottom: 1px solid var(--bd);
  background: rgba(255, 255, 255, .015);
}
.metric-head .h-value { text-align: right; }
.metric-head .h-spark { text-align: left; font-family: 'JetBrains Mono', monospace; letter-spacing: .04em; text-transform: lowercase; }
@media (max-width: 1100px) {
  .metric-head {
    grid-template-columns: 14px 140px 140px 1fr 180px 24px;
    gap: 12px;
  }
}
@media (max-width: 900px) {
  .metric-head {
    grid-template-columns: 14px 1fr auto 110px 24px;
  }
  .metric-head > :nth-child(4) { display: none; }
}

/* The slot-defined value separators — used by compound rows like "avg / max" */
:deep(.mr-sep) {
  color: var(--text-low);
  margin: 0 4px;
  font-weight: 400;
}
:deep(.mr-unit) {
  font-style: normal;
  color: var(--text-low);
  margin-left: 3px;
  font-size: 11px;
  font-weight: 400;
}

/* ---------------------------------------------------------------------------
   Bottom panels — symmetric entity rows.

   One idiom, two panels. Each row is a 2-line card: the head carries
   the leading severity dot, the entity name (left), and the right-side
   numeric metric; the meta line below carries either a depth bar with
   counts (queues) or a tag + counts (consumer groups). The two panels
   render the same .entity-row class so they read as a single visual
   system on the dashboard.
   --------------------------------------------------------------------------- */
.entity-list {
  display: flex;
  flex-direction: column;
  gap: 6px;
  padding: 10px 10px 6px;
}

.entity-row {
  display: block;
  width: 100%;
  text-align: left;
  border: 1px solid var(--bd);
  border-radius: 8px;
  background: transparent;
  padding: 9px 12px 10px;
  cursor: pointer;
  transition: border-color .12s var(--ease), background .12s var(--ease);
}
:global(.dark) .entity-row { background: rgba(255, 255, 255, .012); }
:global(.light) .entity-row { background: rgba(10, 10, 10, .012); }
.entity-row:hover { border-color: var(--bd-hi); }
:global(.dark) .entity-row:hover { background: rgba(255, 255, 255, .03); }
:global(.light) .entity-row:hover { background: rgba(10, 10, 10, .03); }

.entity-head {
  display: flex;
  align-items: center;
  gap: 8px;
}
.entity-name {
  flex: 1;
  font-size: 13.5px;
  font-weight: 500;
  color: var(--text-hi);
  letter-spacing: -.005em;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.entity-right {
  font-family: 'JetBrains Mono', monospace;
  font-variant-numeric: tabular-nums;
  font-size: 12px;
  color: var(--text-mid);
  white-space: nowrap;
  flex-shrink: 0;
}

.entity-meta {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-top: 6px;
  padding-left: 14px;  /* indent under the dot so the meta line aligns with name */
}
.entity-meta .bar-meta {
  flex: 0 1 160px;
  width: 160px;
  height: 4px;
}
.meta-text {
  font-family: 'JetBrains Mono', monospace;
  font-size: 11px;
  color: var(--text-low);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.meta-text strong {
  color: var(--text-mid);
  font-weight: 600;
}
.meta-sep { color: var(--bd-hi); margin: 0 4px; }
.meta-tag {
  display: inline-block;
  padding: 1px 6px;
  border-radius: 99px;
  border: 1px solid var(--bd);
  background: var(--ink-3);
  color: var(--text-mid);
  font-size: 10px;
  font-weight: 500;
  letter-spacing: .02em;
  margin-right: 2px;
}

.entity-empty {
  padding: 32px 16px;
  text-align: center;
  color: var(--text-low);
  font-size: 13px;
}

.card-foot {
  border-top: 1px solid var(--bd);
  padding: 8px 14px;
  display: flex;
  justify-content: flex-end;
}
.card-foot-link {
  font-size: 11.5px;
  color: var(--text-mid);
  cursor: pointer;
  letter-spacing: -.005em;
  transition: color .12s var(--ease);
}
.card-foot-link:hover { color: var(--text-hi); }
</style>
