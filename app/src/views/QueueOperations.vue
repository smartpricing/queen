<template>
  <div class="view-container">

    <!-- Controls: time range picker. The "Source" toggle that used to live
         here disappeared when this view became its own page — the only
         data source is queue-operations, so the toggle had no second
         option to offer. -->
    <div class="card" style="padding:12px 16px; margin-bottom:20px;">
      <div style="display:flex; flex-wrap:wrap; align-items:center; gap:12px 20px;">
        <div style="display:flex; align-items:center; gap:8px; margin-left:auto;">
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

    <!-- Loading skeleton — shown only on first fetch; subsequent refreshes
         leave the previous data on screen for a smooth update. -->
    <div v-if="loading">
      <div style="display:grid; grid-template-columns:repeat(2,1fr); gap:16px;">
        <div v-for="i in 4" :key="i" class="card" style="padding:24px;">
          <div class="skeleton" style="height:192px; width:100%; border-radius:8px;" />
        </div>
      </div>
    </div>

    <template v-else-if="workerData">
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

      <!-- Event Loop / Connection Pool / Job Queue Depth -->
      <div class="three-col-row">
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

        <!-- Job queue depth: backpressure signal that surfaces before
             event-loop lag does. avg = filled area, max = upper envelope.
             A growing max with stable avg = bursty; a growing avg = the
             worker isn't draining its async pipeline fast enough. -->
        <div class="card">
          <div class="card-header">
            <h3>Job Queue Depth</h3>
            <span class="muted">async work waiting in worker queue</span>
          </div>
          <div class="card-body">
            <BaseChart
              v-if="jobQueueChartData.labels.length > 0"
              type="line"
              :data="jobQueueChartData"
              :options="jobQueueChartOptions"
              height="200px"
            />
            <div v-else style="text-align:center; padding:48px 0; color:var(--text-low);">
              No job queue data available
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

      <!-- Dead-letter queue rate -->
      <!-- DLQ messages have already failed retries enough times to be moved
           aside — they're silent business failures (bad payload, downstream
           crash) and the response is investigation, not infra fix. That's a
           different on-call workflow than the generic Errors panel above,
           which is why this chart is separate. -->
      <div class="card" style="margin-bottom:16px;">
        <div class="card-header">
          <h3>Dead Letter Queue</h3>
          <span v-if="dlqTotal > 0" class="chip chip-bad" style="margin-left:auto;">
            {{ formatNumber(dlqTotal) }} in period
          </span>
          <span v-else class="muted">messages moved to DLQ</span>
        </div>
        <div class="card-body">
          <BaseChart
            v-if="dlqChartData.labels.length > 0"
            type="bar"
            :data="dlqChartData"
            :options="dlqChartOptions"
            height="180px"
          />
          <div v-else style="text-align:center; padding:32px 0; color:var(--ok-500);">
            No DLQ events in this period
          </div>
        </div>
      </div>

      <!-- Retention & Eviction Jobs -->
      <div class="card" style="margin-bottom:16px;">
        <div class="card-header">
          <h3>Retention &amp; Eviction Jobs</h3>
          <span class="muted">messages deleted by retention / eviction workers</span>
        </div>
        <div class="card-body">
          <div v-if="retentionChartData.labels.length > 0">
            <div style="display:flex; gap:24px; flex-wrap:wrap; margin-bottom:14px;">
              <div class="stat" style="padding:8px 12px;">
                <div class="stat-label">Retention</div>
                <div class="stat-value font-mono">{{ formatNumber(retentionTotals?.retentionMsgs || 0) }}</div>
              </div>
              <div class="stat" style="padding:8px 12px;">
                <div class="stat-label">Completed retention</div>
                <div class="stat-value font-mono">{{ formatNumber(retentionTotals?.completedRetentionMsgs || 0) }}</div>
              </div>
              <div class="stat" style="padding:8px 12px;">
                <div class="stat-label">Eviction</div>
                <div class="stat-value font-mono">{{ formatNumber(retentionTotals?.evictionMsgs || 0) }}</div>
              </div>
              <div class="stat" style="padding:8px 12px;">
                <div class="stat-label">Events</div>
                <div class="stat-value font-mono">{{ formatNumber(retentionTotals?.eventCount || 0) }}</div>
              </div>
            </div>
            <BaseChart
              type="bar"
              :data="retentionChartData"
              :options="retentionChartOptions"
              height="240px"
            />
          </div>
          <div v-else style="text-align:center; padding:32px 0; color:var(--text-low);">
            No retention / eviction events in this range
          </div>
        </div>
      </div>

      <!-- Top Queues leaderboard (window snapshot) -->
      <div v-if="hasTopQueueData" class="card" style="margin-bottom:16px;">
        <div class="card-header">
          <h3>Top Queues</h3>
          <span class="muted">averaged across the selected window — what to look at first</span>
        </div>
        <div class="card-body">
          <div class="top-queues-grid">
            <!-- Push rate -->
            <div>
              <h4 class="label-xs" style="margin-bottom:8px;">By Push/s</h4>
              <table v-if="topQueues.push.length" class="t top-queues">
                <tbody>
                  <tr v-for="(row, i) in topQueues.push" :key="`push-${row.queue}`">
                    <td class="rank">{{ i + 1 }}</td>
                    <td class="qname" :title="row.queue">{{ row.queue }}</td>
                    <td class="font-mono tabular-nums val">{{ formatRate(row.push) }}</td>
                  </tr>
                </tbody>
              </table>
              <div v-else class="empty-tile">No push activity</div>
            </div>
            <!-- Pop rate -->
            <div>
              <h4 class="label-xs" style="margin-bottom:8px;">By Pop/s</h4>
              <table v-if="topQueues.pop.length" class="t top-queues">
                <tbody>
                  <tr v-for="(row, i) in topQueues.pop" :key="`pop-${row.queue}`">
                    <td class="rank">{{ i + 1 }}</td>
                    <td class="qname" :title="row.queue">{{ row.queue }}</td>
                    <td class="font-mono tabular-nums val">{{ formatRate(row.pop) }}</td>
                  </tr>
                </tbody>
              </table>
              <div v-else class="empty-tile">No pop activity</div>
            </div>
            <!-- Parked (waiting consumers) -->
            <div>
              <h4 class="label-xs" style="margin-bottom:8px;">By Parked (waiting consumers)</h4>
              <table v-if="topQueues.parked.length" class="t top-queues">
                <tbody>
                  <tr v-for="(row, i) in topQueues.parked" :key="`parked-${row.queue}`">
                    <td class="rank">{{ i + 1 }}</td>
                    <td class="qname" :title="row.queue">{{ row.queue }}</td>
                    <td class="font-mono tabular-nums val">{{ formatParked(row.parked) }}</td>
                  </tr>
                </tbody>
              </table>
              <div v-else class="empty-tile">No parked long-polls</div>
            </div>
            <!-- Lag -->
            <div>
              <h4 class="label-xs" style="margin-bottom:8px;">By Avg Lag</h4>
              <table v-if="topQueues.lag.length" class="t top-queues">
                <tbody>
                  <tr v-for="(row, i) in topQueues.lag" :key="`lag-${row.queue}`">
                    <td class="rank">{{ i + 1 }}</td>
                    <td class="qname" :title="row.queue">{{ row.queue }}</td>
                    <td class="font-mono tabular-nums val">{{ formatDuration(row.lag) }}</td>
                  </tr>
                </tbody>
              </table>
              <div v-else class="empty-tile">No measured lag</div>
            </div>
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
            <div style="display:flex; align-items:center; gap:8px; margin-bottom:12px;">
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
              <!-- Per-queue view-mode toggle: only meaningful for the Parked
                   tab today (cluster-aggregate vs per-replica). Hidden on
                   other ops to keep the surface tidy. -->
              <div v-if="selectedQueueOp === 'parked'" style="display:flex; align-items:center; gap:8px; margin-left:auto;">
                <span style="font-size:11px; font-weight:500; color:var(--text-low);">View</span>
                <div class="seg">
                  <button
                    @click="viewMode = 'aggregate'"
                    :class="{ on: viewMode === 'aggregate' }"
                  >Cluster</button>
                  <button
                    @click="viewMode = 'individual'"
                    :class="{ on: viewMode === 'individual' }"
                  >Per Replica</button>
                </div>
              </div>
            </div>
            <!-- Op selector: choose which per-queue chart to show -->
            <div style="display:flex; align-items:center; gap:8px; flex-wrap:wrap; margin-bottom:14px;">
              <span style="font-size:12px; color:var(--text-low); white-space:nowrap;">Op:</span>
              <button
                v-for="op in queueOpTabs"
                :key="op.key"
                @click="selectedQueueOp = op.key"
                style="display:inline-flex; align-items:center; gap:6px; padding:3px 10px; border-radius:999px; font-size:11px; font-weight:500; cursor:pointer; border:1px solid var(--bd-hi); transition:.15s;"
                :class="selectedQueueOp === op.key ? op.activeClass : 'opacity-50'"
              >
                <span style="width:6px; height:6px; border-radius:99px;" :style="{ background: selectedQueueOp === op.key ? op.activeDot : 'var(--text-faint)' }" />
                {{ op.label }}
              </button>
            </div>
            <div>
              <h4 class="label-xs" style="margin-bottom:10px;">
                {{ queueOpActive.label }} by Queue<span v-if="isParkedIndividual"> &amp; Replica</span>
                <span v-if="queueOpActive.kind === 'rate'" style="color:var(--text-low); font-weight:normal;">(per second)</span>
                <span v-else-if="queueOpActive.kind === 'rate-signed'" style="color:var(--text-low); font-weight:normal;">(push − pop messages/s; positive = backlog filling, negative = draining)</span>
                <span v-else-if="queueOpActive.kind === 'percent'" style="color:var(--text-low); font-weight:normal;">(long-polls returning a message ÷ all long-poll completions; gaps = quiet bucket)</span>
                <span v-else-if="isParkedIndividual" style="color:var(--text-low); font-weight:normal;">(in-flight long-polls, per replica, averaged each minute)</span>
                <span v-else-if="queueOpActive.kind === 'gauge'" style="color:var(--text-low); font-weight:normal;">(in-flight long-polls, averaged each minute)</span>
              </h4>
              <BaseChart
                v-if="perQueueChartData.labels.length > 0"
                :key="`per-queue-ops-${queueOpActive.key}-${isParkedIndividual ? 'individual' : 'aggregate'}-${hasExplicitQueueSelection ? 'legend' : 'nolegend'}`"
                type="line"
                :data="perQueueChartData"
                :options="perQueueThroughputOptions"
                height="340px"
              />
              <div v-else style="text-align:center; padding:48px 0; color:var(--text-low);">No per-queue data for this op yet</div>
            </div>
            <div style="margin-top:20px;">
              <h4 class="label-xs" style="margin-bottom:10px;">Avg Latency by Queue</h4>
              <BaseChart
                v-if="queueLagChartData.labels.length > 0"
                :key="`per-queue-lag-${hasExplicitQueueSelection ? 'legend' : 'nolegend'}`"
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
            <p style="font-size:12px; margin-top:4px;">Per-queue data is collected as soon as libqueen flushes its first minute boundary.</p>
          </div>
        </div>
      </div>

      <!-- Partitions per queue -->
      <div class="card" style="margin-bottom:16px;">
        <div class="card-header">
          <h3>Partitions</h3>
          <span class="muted">count &amp; creation / deletion rate per queue</span>
        </div>
        <div class="card-body">
          <template v-if="partitionCountChartData.labels.length > 0 || partitionRateChartData.labels.length > 0">
            <div>
              <h4 class="label-xs" style="margin-bottom:10px;">Partition count by Queue</h4>
              <BaseChart
                v-if="partitionCountChartData.labels.length > 0"
                :key="`per-queue-partitions-${hasExplicitQueueSelection ? 'legend' : 'nolegend'}`"
                type="line"
                :data="partitionCountChartData"
                :options="perQueuePartitionCountOptions"
                height="280px"
              />
              <div v-else style="text-align:center; padding:32px 0; color:var(--text-low);">No partition-count snapshots yet</div>
            </div>
            <div style="margin-top:20px;">
              <h4 class="label-xs" style="margin-bottom:10px;">Partition creation / deletion rate (events per bucket, across all queues)</h4>
              <BaseChart
                v-if="partitionRateChartData.labels.length > 0"
                type="bar"
                :data="partitionRateChartData"
                :options="partitionRateChartOptions"
                height="240px"
              />
              <div v-else style="text-align:center; padding:32px 0; color:var(--text-low);">No partition create/delete events in this range</div>
            </div>
          </template>
          <div v-else style="text-align:center; padding:32px 0; color:var(--text-low);">
            <p>No partition metrics recorded yet.</p>
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
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, watch } from 'vue'
import { system } from '@/api'
import { useRefresh } from '@/composables/useRefresh'
import BaseChart from '@/components/BaseChart.vue'
import MultiSelect from '@/components/MultiSelect.vue'

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
const loading = ref(true)
const timeRange = ref(60)
const customMode = ref(false)
const customFrom = ref('')
const customTo = ref('')

const workerData = ref(null)
const queueLagData = ref(null)
const queueOpsData = ref(null)
// Per-replica parked breakdown — only fetched when the Parked tab is active
// and the per-queue view is set to 'individual'. Null otherwise so the
// aggregate path is unaffected.
const queueParkedReplicasData = ref(null)
const retentionData = ref(null)

const selectedQueues = ref([])
const selectedQueueOp = ref('pop')
// Cluster-aggregate ('aggregate') vs per-replica ('individual'); only
// affects the Parked tab. Lives at this scope (not inside queueOpTabs)
// so the Parked-tab toggle can drive a refetch via the watcher below.
const viewMode = ref('aggregate')

const timeRanges = [
  { label: '15m', value: 15 },
  { label: '1h', value: 60 },
  { label: '6h', value: 360 },
  { label: '24h', value: 1440 }
]

// ---------------------------------------------------------------------------
// Formatters (local copies — these are small and we'd rather not couple two
// pages through a shared util file just for ~30 lines of helpers)
// ---------------------------------------------------------------------------
const formatDateTimeLocal = (date) => {
  const year = date.getFullYear()
  const month = String(date.getMonth() + 1).padStart(2, '0')
  const day = String(date.getDate()).padStart(2, '0')
  const hours = String(date.getHours()).padStart(2, '0')
  const minutes = String(date.getMinutes()).padStart(2, '0')
  return `${year}-${month}-${day}T${hours}:${minutes}`
}

const formatDuration = (ms) => {
  if (ms === undefined || ms === null) return '0ms'
  if (ms < 1000) return `${Math.round(ms)}ms`
  if (ms < 60000) return `${(ms / 1000).toFixed(1)}s`
  return `${(ms / 60000).toFixed(1)}m`
}

const formatNumber = (num) => {
  if (num === undefined || num === null) return '0'
  if (num >= 1e12) return (num / 1e12).toFixed(2) + 'T'
  if (num >= 1e9) return (num / 1e9).toFixed(2) + 'B'
  if (num >= 1e6) return (num / 1e6).toFixed(2) + 'M'
  if (num >= 1e3) return (num / 1e3).toFixed(1) + 'K'
  return num.toString()
}

const formatTime = (timestamp) => {
  if (!timestamp) return '-'
  return new Date(timestamp).toLocaleTimeString('en-US', {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit'
  })
}

const formatRate = (v) => {
  const n = Number(v) || 0
  if (n === 0) return '0/s'
  if (n < 10) return `${n.toFixed(2)}/s`
  if (n < 100) return `${n.toFixed(1)}/s`
  return `${Math.round(n)}/s`
}

const formatParked = (v) => {
  const n = Number(v) || 0
  if (n >= 10) return Math.round(n).toString()
  return n.toFixed(2).replace(/\.?0+$/, '') || '0'
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

const isMultiDay = (timeSeries) => {
  if (!timeSeries || timeSeries.length < 2) return false
  const firstDate = new Date(timeSeries[0].timestamp)
  const lastDate = new Date(timeSeries[timeSeries.length - 1].timestamp)
  return firstDate.toDateString() !== lastDate.toDateString()
}

// ---------------------------------------------------------------------------
// Throughput / latency / event loop / errors — chip metric toggles
// ---------------------------------------------------------------------------
const throughputMetrics = [
  { key: 'push', label: 'Push', activeClass: 'chip-mute', activeDot: '#e6e6e6' },
  { key: 'pop',  label: 'Pop',  activeClass: 'chip-mute', activeDot: '#8a8a92' },
  { key: 'ack',  label: 'Ack',  activeClass: 'chip-ok',   activeDot: '#4ade80' },
]
const selectedThroughputMetrics = reactive({ push: true, pop: true, ack: true })
const toggleThroughputMetric = (key) => { selectedThroughputMetrics[key] = !selectedThroughputMetrics[key] }

const eventLoopMetrics = [
  { key: 'avg', label: 'Avg Event Loop', activeClass: 'chip-mute', activeDot: '#e6e6e6' },
  { key: 'max', label: 'Max Event Loop', activeClass: 'chip-bad',  activeDot: '#fb7185' },
]
const selectedEventLoopMetrics = reactive({ avg: true, max: true })
const toggleEventLoopMetric = (key) => { selectedEventLoopMetrics[key] = !selectedEventLoopMetrics[key] }

const errorMetrics = [
  { key: 'dbErrors',  label: 'DB Errors',  activeClass: 'chip-bad',  activeDot: '#fb7185' },
  { key: 'ackFailed', label: 'Ack Failed', activeClass: 'chip-warn', activeDot: '#e6b450' },
  { key: 'dlq',       label: 'DLQ',        activeClass: 'chip-bad',  activeDot: '#fb7185' },
]
const selectedErrorMetrics = reactive({ dbErrors: true, ackFailed: true, dlq: true })
const toggleErrorMetric = (key) => { selectedErrorMetrics[key] = !selectedErrorMetrics[key] }

// ---------------------------------------------------------------------------
// Chart options (each one annotated where its semantics aren't obvious)
// ---------------------------------------------------------------------------
const lagChartOptions = {
  plugins: {
    legend: { display: false },
    tooltip: {
      callbacks: { label: (ctx) => ` ${ctx.dataset.label}: ${formatDuration(ctx.parsed.y)}` }
    }
  },
  scales: {
    y: {
      beginAtZero: true,
      min: 0,
      title: { display: true, text: 'Latency', font: { size: 11 } },
      ticks: { callback: (value) => formatDuration(value) }
    }
  }
}

const throughputChartOptions = {
  plugins: { legend: { display: false } },
  scales: { y: { title: { display: true, text: 'Operations/s', font: { size: 11 } } } }
}

const poolChartOptions = {
  plugins: { legend: { display: false } },
  scales: { y: { title: { display: true, text: 'Connections', font: { size: 11 } } } }
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

const dlqChartOptions = {
  plugins: { legend: { display: false } },
  scales: {
    y: {
      beginAtZero: true,
      min: 0,
      title: { display: true, text: 'DLQ messages / bucket', font: { size: 11 } },
      ticks: { precision: 0 }
    }
  }
}

const jobQueueChartOptions = {
  plugins: { legend: { display: true, position: 'top', labels: { boxWidth: 12, padding: 8, font: { size: 11 } } } },
  scales: {
    y: {
      beginAtZero: true,
      min: 0,
      title: { display: true, text: 'Pending jobs', font: { size: 11 } },
      ticks: { precision: 0 }
    }
  }
}

const retentionChartOptions = {
  plugins: { legend: { display: true, position: 'top', labels: { boxWidth: 12, padding: 8, font: { size: 11 } } } },
  scales: {
    x: { stacked: true },
    y: {
      stacked: true,
      beginAtZero: true,
      min: 0,
      title: { display: true, text: 'Messages deleted', font: { size: 11 } }
    }
  }
}

// ---------------------------------------------------------------------------
// Per-queue ops tabs — each tab plots one derived counter on the per-queue
// chart. `kind` distinguishes:
//   - 'rate'        → value is per-second; subtitle says "(per second)".
//   - 'rate-signed' → signed per-second delta (push − pop); y-axis crosses zero.
//   - 'count'       → absolute count over the bucket (transactions).
//   - 'gauge'       → snapshot (parked long-polls); no /sec semantics.
//   - 'percent'     → 0–100% derived ratio (fill ratio).
// `field` is either a string property name or a function `(entry) => number`
// for derived metrics.
// ---------------------------------------------------------------------------
const queueOpTabs = [
  { key: 'pop',    label: 'Pop/s',   activeClass: 'chip-mute', activeDot: '#8a8a92', field: 'popPerSecond',   yLabel: 'Pops/s',     kind: 'rate'  },
  { key: 'push',   label: 'Push/s',  activeClass: 'chip-mute', activeDot: '#e6e6e6', field: 'pushPerSecond',  yLabel: 'Pushes/s',   kind: 'rate'  },
  { key: 'ack',    label: 'Ack/s',   activeClass: 'chip-ok',   activeDot: '#4ade80', field: 'ackPerSecond',   yLabel: 'Acks/s',     kind: 'rate'  },
  { key: 'empty',  label: 'Empty/s', activeClass: 'chip-mute', activeDot: '#b8b8b8', field: 'emptyPerSecond', yLabel: 'Empty/s',    kind: 'rate'  },
  // Fill ratio = popMessages / (popMessages + popEmpty). Returns null
  // (= chart gap) when the bucket has too few completions to compute
  // meaningfully — a once-a-minute blip mustn't read as 100%.
  { key: 'fill',   label: 'Fill %',  activeClass: 'chip-ok',   activeDot: '#4ade80',
    field: (e) => {
      const pop = Number(e.popMessages) || 0
      const empty = Number(e.popEmpty) || 0
      const total = pop + empty
      if (total < 2) return null
      return Math.round((pop / total) * 1000) / 10
    },
    yLabel: 'Fill %', kind: 'percent' },
  // Signed push − pop rate: positive = backlog growing, negative = draining.
  { key: 'delta',  label: 'Push−Pop Δ', activeClass: 'chip-warn', activeDot: '#e6b450',
    field: (e) => {
      const push = Number(e.pushPerSecond) || 0
      const pop = Number(e.popPerSecond) || 0
      return Math.round((push - pop) * 100) / 100
    },
    yLabel: 'Push − Pop (msgs/s)', kind: 'rate-signed' },
  { key: 'trx',    label: 'Trx',     activeClass: 'chip-mute', activeDot: '#e6b450', field: 'transactions',   yLabel: 'Transactions', kind: 'count' },
  { key: 'parked', label: 'Parked',  activeClass: 'chip-mute', activeDot: '#7aa2f7', field: 'parkedCount',    yLabel: 'Parked',     kind: 'gauge' },
]
const queueOpActive = computed(() => queueOpTabs.find(t => t.key === selectedQueueOp.value) || queueOpTabs[0])
const isParkedIndividual = computed(() =>
  selectedQueueOp.value === 'parked' && viewMode.value === 'individual')

// True when the user has explicitly picked queues via the MultiSelect.
// Drives whether the per-queue charts show the legend (with dozens of
// queues a legend would dominate the chart, so we suppress it by default).
const hasExplicitQueueSelection = computed(() => selectedQueues.value.length > 0)

// Tooltip behaviour shared by every per-queue chart. Two cheap rules cut
// the tooltip back to the queues that matter at a given timestamp:
//   1) `filter` drops items whose value is zero — they convey nothing,
//      and the chart line is already at y=0.
//   2) `itemSort` orders biggest-first so most-active queues are on top.
const queueTooltipBase = {
  filter: (item) => Number(item.parsed.y) > 0,
  itemSort: (a, b) => Number(b.parsed.y) - Number(a.parsed.y),
}

const lagTooltip = {
  ...queueTooltipBase,
  callbacks: { label: (ctx) => ` ${ctx.dataset.label}: ${formatDuration(ctx.parsed.y)}` }
}

const perQueuePartitionCountOptions = computed(() => ({
  plugins: {
    legend: { display: hasExplicitQueueSelection.value, position: 'top', labels: { boxWidth: 12, padding: 8, font: { size: 11 } } },
    tooltip: queueTooltipBase
  },
  scales: {
    y: {
      beginAtZero: true,
      min: 0,
      title: { display: true, text: 'Partitions', font: { size: 11 } },
      ticks: { precision: 0 }
    }
  }
}))

const partitionRateChartOptions = {
  plugins: { legend: { display: true, position: 'top', labels: { boxWidth: 12, padding: 8, font: { size: 11 } } } },
  scales: {
    y: {
      beginAtZero: true,
      title: { display: true, text: 'Events / bucket', font: { size: 11 } },
      ticks: { precision: 0 }
    }
  }
}

// Per-queue chart options. Y-axis behavior per kind:
//   - rate / count / gauge  → starts at 0
//   - rate-signed (delta)   → auto-scaled, crosses zero (positive = filling,
//                             negative = draining); zero gridline emphasized.
//   - percent (fill ratio)  → fixed 0–100 with % suffix on ticks/tooltip.
const perQueueThroughputOptions = computed(() => {
  const kind = queueOpActive.value.kind
  const yScale = {
    title: { display: true, text: queueOpActive.value.yLabel, font: { size: 11 } }
  }
  let tooltipOpts = queueTooltipBase
  if (kind === 'percent') {
    yScale.beginAtZero = true
    yScale.min = 0
    yScale.max = 100
    yScale.ticks = { callback: (value) => `${value}%` }
    // Override the base tooltip's "drop zero" filter — for fill ratio,
    // 0% is itself meaningful (every long-poll empty). Keep big-first sort.
    tooltipOpts = {
      itemSort: queueTooltipBase.itemSort,
      callbacks: { label: (ctx) => ` ${ctx.dataset.label}: ${Number(ctx.parsed.y).toFixed(1)}%` }
    }
  } else if (kind === 'rate-signed') {
    yScale.grid = {
      color: (ctx) => ctx.tick.value === 0 ? 'rgba(230,230,230,0.35)' : 'rgba(230,230,230,0.06)',
      lineWidth: (ctx) => ctx.tick.value === 0 ? 1.5 : 1
    }
    tooltipOpts = {
      itemSort: queueTooltipBase.itemSort,
      callbacks: {
        label: (ctx) => {
          const v = Number(ctx.parsed.y)
          const sign = v > 0 ? '+' : ''
          return ` ${ctx.dataset.label}: ${sign}${v.toFixed(2)} msgs/s`
        }
      }
    }
  } else {
    yScale.beginAtZero = true
    yScale.min = 0
    if (kind === 'gauge') yScale.ticks = { precision: 0 }
  }
  return {
    plugins: {
      legend: { display: hasExplicitQueueSelection.value, position: 'top', labels: { boxWidth: 12, padding: 8, font: { size: 11 } } },
      tooltip: tooltipOpts
    },
    scales: { y: yScale }
  }
})

const perQueueLagOptions = computed(() => ({
  plugins: {
    legend: { display: hasExplicitQueueSelection.value, position: 'top', labels: { boxWidth: 12, padding: 8, font: { size: 11 } } },
    tooltip: lagTooltip
  },
  scales: {
    y: {
      beginAtZero: true,
      min: 0,
      title: { display: true, text: 'Latency', font: { size: 11 } },
      ticks: { callback: (value) => formatDuration(value) }
    }
  }
}))

// ---------------------------------------------------------------------------
// Per-queue color palette — five greys + one warm accent for the 5th line.
// We deliberately do NOT use red/green/yellow here: those are reserved for
// status semantics (chips, threshold tones). Distinction stays via legend
// + tooltip rather than color symbolism that would mislead.
// ---------------------------------------------------------------------------
const queueColors = [
  { border: '#e6e6e6', bg: 'rgba(230, 230, 230, 0.10)' },
  { border: '#8a8a92', bg: 'rgba(138, 138, 146, 0.10)' },
  { border: '#6a6a6a', bg: 'rgba(106, 106, 106, 0.10)' },
  { border: '#b8b8b8', bg: 'rgba(184, 184, 184, 0.10)' },
  { border: '#4a4a4f', bg: 'rgba(74, 74, 79, 0.18)'    },
]

// ---------------------------------------------------------------------------
// Computed: derived metrics + chart datasets
// ---------------------------------------------------------------------------
const totalErrors = computed(() => {
  if (!workerData.value?.timeSeries?.length) return 0
  return workerData.value.timeSeries.reduce((sum, t) => {
    return sum + (t.dbErrors || 0) + (t.ackFailed || 0) + (t.dlqCount || 0)
  }, 0)
})

const throughputChartData = computed(() => {
  if (!workerData.value?.timeSeries?.length) return { labels: [], datasets: [] }

  const ts = [...workerData.value.timeSeries].reverse()
  const multiDay = isMultiDay(ts)
  const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multiDay))

  const datasets = []
  if (selectedThroughputMetrics.push) {
    datasets.push({
      label: 'Push/s', data: ts.map(t => t.pushPerSecond || 0),
      borderColor: '#e6e6e6', backgroundColor: 'rgba(230, 230, 230, 0.12)',
      fill: true, tension: 0
    })
  }
  if (selectedThroughputMetrics.pop) {
    datasets.push({
      label: 'Pop/s', data: ts.map(t => t.popPerSecond || 0),
      borderColor: '#8a8a92', backgroundColor: 'rgba(138, 138, 146, 0.12)',
      fill: true, tension: 0
    })
  }
  if (selectedThroughputMetrics.ack) {
    datasets.push({
      label: 'Ack/s', data: ts.map(t => t.ackPerSecond || 0),
      borderColor: '#6a6a6a', backgroundColor: 'rgba(106, 106, 106, 0.12)',
      fill: true, tension: 0
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
      { label: 'Avg Lag (ms)', data: ts.map(t => t.avgLagMs || 0),
        borderColor: '#e6e6e6', backgroundColor: 'rgba(230, 230, 230, 0.1)',
        fill: true, tension: 0 },
      { label: 'Max Lag (ms)', data: ts.map(t => t.maxLagMs || 0),
        borderColor: '#8a8a92', fill: false, tension: 0 }
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
      label: 'Avg Event Loop (ms)', data: ts.map(t => t.avgEventLoopLagMs || 0),
      borderColor: '#e6e6e6', backgroundColor: 'rgba(230, 230, 230, 0.12)',
      fill: true, tension: 0
    })
  }
  if (selectedEventLoopMetrics.max) {
    datasets.push({
      label: 'Max Event Loop (ms)', data: ts.map(t => t.maxEventLoopLagMs || 0),
      borderColor: '#8a8a92', backgroundColor: 'rgba(138, 138, 146, 0.10)',
      fill: true, tension: 0
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
      { label: 'Free Slots', data: ts.map(t => t.avgFreeSlots || 0),
        borderColor: '#e6e6e6', backgroundColor: 'rgba(230, 230, 230, 0.12)',
        fill: true, tension: 0 },
      { label: 'DB Connections', data: ts.map(t => t.dbConnections || 0),
        borderColor: '#8a8a92', fill: false, tension: 0 }
    ]
  }
})

// Job queue depth (avg filled, max line). Hidden when both series are flat
// at zero across the window — keeps the panel quiet on healthy clusters
// and only attracts attention when something is queueing up.
const jobQueueChartData = computed(() => {
  if (!workerData.value?.timeSeries?.length) return { labels: [], datasets: [] }
  const ts = [...workerData.value.timeSeries].reverse()
  const avgs = ts.map(t => Number(t.avgJobQueueSize) || 0)
  const maxes = ts.map(t => Number(t.maxJobQueueSize) || 0)
  if (avgs.every(v => v === 0) && maxes.every(v => v === 0)) {
    return { labels: [], datasets: [] }
  }
  const multiDay = isMultiDay(ts)
  const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multiDay))
  return {
    labels,
    datasets: [
      { label: 'Avg', data: avgs,
        borderColor: '#e6e6e6', backgroundColor: 'rgba(230, 230, 230, 0.12)',
        fill: true, tension: 0 },
      { label: 'Max', data: maxes,
        borderColor: '#fb7185', fill: false, tension: 0, borderDash: [4, 3] }
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
      label: 'DB Errors', data: ts.map(t => t.dbErrors || 0),
      backgroundColor: 'rgba(244, 63, 94, 0.6)', borderColor: '#f43f5e', borderWidth: 1
    })
  }
  if (selectedErrorMetrics.ackFailed) {
    datasets.push({
      label: 'Ack Failed', data: ts.map(t => t.ackFailed || 0),
      backgroundColor: 'rgba(138, 138, 146, 0.6)', borderColor: '#8a8a92', borderWidth: 1
    })
  }
  if (selectedErrorMetrics.dlq) {
    datasets.push({
      label: 'DLQ', data: ts.map(t => t.dlqCount || 0),
      backgroundColor: 'rgba(230, 230, 230, 0.6)', borderColor: '#e6e6e6', borderWidth: 1
    })
  }
  return { labels, datasets }
})

// DLQ-only chart. Reuses dlqCount from the worker-metrics time series, but
// gets its own panel because DLQ events have a different operational
// meaning than generic errors (retry-exhausted business failures, not
// infra). Drops bars at zero rather than draw a flat line so spikes in a
// quiet window pop visually.
const dlqChartData = computed(() => {
  if (!workerData.value?.timeSeries?.length) return { labels: [], datasets: [] }
  const ts = [...workerData.value.timeSeries].reverse()
  const data = ts.map(t => Number(t.dlqCount) || 0)
  if (data.every(v => v === 0)) return { labels: [], datasets: [] }
  const multiDay = isMultiDay(ts)
  const labels = ts.map(t => formatChartLabel(new Date(t.timestamp), multiDay))
  return {
    labels,
    datasets: [{
      label: 'DLQ', data,
      backgroundColor: 'rgba(244, 63, 94, 0.6)',
      borderColor: '#f43f5e', borderWidth: 1
    }]
  }
})

const dlqTotal = computed(() => {
  if (!workerData.value?.timeSeries?.length) return 0
  return workerData.value.timeSeries.reduce((sum, t) => sum + (Number(t.dlqCount) || 0), 0)
})

// Retention / eviction time series
const retentionTotals = computed(() => retentionData.value?.totals || null)
const retentionChartData = computed(() => {
  const rows = retentionData.value?.series || []
  if (!rows.length) return { labels: [], datasets: [] }
  const multiDay = rows.length >= 2 &&
    new Date(rows[0].bucket).toDateString() !== new Date(rows[rows.length-1].bucket).toDateString()
  const labels = rows.map(r => formatChartLabel(new Date(r.bucket), multiDay))
  return {
    labels,
    datasets: [
      { label: 'Retention',          data: rows.map(r => Number(r.retentionMsgs) || 0),
        backgroundColor: 'rgba(230,230,230,0.6)', borderColor: '#e6e6e6', borderWidth: 1 },
      { label: 'Completed retention', data: rows.map(r => Number(r.completedRetentionMsgs) || 0),
        backgroundColor: 'rgba(138,138,146,0.6)', borderColor: '#8a8a92', borderWidth: 1 },
      { label: 'Eviction',           data: rows.map(r => Number(r.evictionMsgs) || 0),
        backgroundColor: 'rgba(230,180,80,0.5)', borderColor: '#e6b450', borderWidth: 1 },
    ]
  }
})

// ---------------------------------------------------------------------------
// Per-queue ops time series helpers — pivots queue_ops data.
// ---------------------------------------------------------------------------
const buildPerQueueOpsChart = (valueAccessor) => {
  const raw = queueOpsData.value?.series || []
  if (!raw.length) return { labels: [], datasets: [] }
  const bucketSet = new Set(raw.map(r => r.bucket))
  const buckets = [...bucketSet].sort()
  const multiDay = buckets.length >= 2 &&
    new Date(buckets[0]).toDateString() !== new Date(buckets[buckets.length - 1]).toDateString()
  const labels = buckets.map(b => formatChartLabel(new Date(b), multiDay))

  const queuesAvailable = [...new Set(raw.map(r => r.queueName))]
  const pick = selectedQueues.value.length > 0
    ? queuesAvailable.filter(q => selectedQueues.value.includes(q))
    : queuesAvailable
  pick.sort()

  const lookup = {}
  raw.forEach(r => { lookup[`${r.queueName}|${r.bucket}`] = r })

  const datasets = pick.map((q, i) => {
    const color = queueColors[i % queueColors.length]
    return {
      label: q,
      // valueAccessor may return null to indicate "not enough data this
      // bucket" (e.g. fill ratio with <2 events). Preserve null so
      // Chart.js renders a gap instead of a misleading drop to zero.
      data: buckets.map(b => {
        const entry = lookup[`${q}|${b}`]
        if (!entry) return 0
        const v = valueAccessor(entry)
        if (v === null || v === undefined) return null
        const n = Number(v)
        return Number.isFinite(n) ? n : null
      }),
      borderColor: color.border,
      backgroundColor: color.bg,
      fill: false,
      tension: 0,
      spanGaps: true
    }
  })
  return { labels, datasets }
}

const queueOpsRateChartData = computed(() => {
  const field = queueOpActive.value.field
  // `field` may be a string property name on the bucket entry, or a derived
  // accessor function for tabs like Fill % and Push−Pop Δ. We normalize
  // here so buildPerQueueOpsChart only ever sees a function.
  const accessor = typeof field === 'function' ? field : (e => e[field])
  return buildPerQueueOpsChart(accessor)
})

// Per-(queue × replica) breakdown for the Parked tab in individual mode.
// Used as-is when both: Parked tab active AND view mode == individual.
// Each line keyed "<queue> @ <hostname>:<workerId>" so a queue running
// across N replicas yields N lines.
const queueParkedReplicaChartData = computed(() => {
  const raw = queueParkedReplicasData.value?.series || []
  if (!raw.length) return { labels: [], datasets: [] }

  const filterQueues = selectedQueues.value.length > 0
    ? new Set(selectedQueues.value)
    : null
  const filtered = filterQueues
    ? raw.filter(r => filterQueues.has(r.queueName))
    : raw

  const bucketSet = new Set(filtered.map(r => r.bucket))
  const buckets = [...bucketSet].sort()
  const multiDay = buckets.length >= 2 &&
    new Date(buckets[0]).toDateString() !== new Date(buckets[buckets.length - 1]).toDateString()
  const labels = buckets.map(b => formatChartLabel(new Date(b), multiDay))

  const seriesKey = r => `${r.queueName}@${r.hostname}:${r.workerId}`
  const seriesSet = new Set(filtered.map(seriesKey))
  const series = [...seriesSet].sort()

  const lookup = {}
  for (const r of filtered) lookup[`${seriesKey(r)}|${r.bucket}`] = r

  const datasets = series.map((s, i) => {
    const color = queueColors[i % queueColors.length]
    return {
      label: s,
      data: buckets.map(b => {
        const entry = lookup[`${s}|${b}`]
        return entry ? (Number(entry.parkedCount) || 0) : 0
      }),
      borderColor: color.border,
      backgroundColor: color.bg,
      fill: false,
      tension: 0
    }
  })
  return { labels, datasets }
})

// The chart picks per-replica data only when Parked tab is active *and*
// the per-queue view mode is 'individual'. Everything else uses the
// cluster-aggregated path.
const perQueueChartData = computed(() => {
  if (selectedQueueOp.value === 'parked' && viewMode.value === 'individual') {
    return queueParkedReplicaChartData.value
  }
  return queueOpsRateChartData.value
})

// Partition count snapshot per queue (line chart over time, ~one value per minute)
const partitionCountChartData = computed(() => buildPerQueueOpsChart(e => e.partitionCount))

// Partition create / delete rate across all queues (bar chart summed per bucket)
const partitionRateChartData = computed(() => {
  const raw = queueOpsData.value?.series || []
  if (!raw.length) return { labels: [], datasets: [] }
  const bucketSet = new Set(raw.map(r => r.bucket))
  const buckets = [...bucketSet].sort()
  const multiDay = buckets.length >= 2 &&
    new Date(buckets[0]).toDateString() !== new Date(buckets[buckets.length - 1]).toDateString()
  const labels = buckets.map(b => formatChartLabel(new Date(b), multiDay))

  const created = {}
  const deleted = {}
  buckets.forEach(b => { created[b] = 0; deleted[b] = 0 })
  for (const r of raw) {
    created[r.bucket] = (created[r.bucket] || 0) + (Number(r.partitionsCreated) || 0)
    deleted[r.bucket] = (deleted[r.bucket] || 0) + (Number(r.partitionsDeleted) || 0)
  }

  const hasActivity = buckets.some(b => created[b] > 0 || deleted[b] > 0)
  if (!hasActivity) return { labels: [], datasets: [] }

  return {
    labels,
    datasets: [
      { label: 'Created', data: buckets.map(b => created[b]),
        backgroundColor: 'rgba(230,230,230,0.5)', borderColor: '#e6e6e6', borderWidth: 1 },
      { label: 'Deleted', data: buckets.map(b => -deleted[b]),
        backgroundColor: 'rgba(138,138,146,0.5)', borderColor: '#8a8a92', borderWidth: 1 },
    ]
  }
})

// ---------------------------------------------------------------------------
// Per-queue chart data (lag stream)
// ---------------------------------------------------------------------------
// availableQueues is the union of queues seen in the pop-only lag stream
// (queueLagData) and the per-op stream (queueOpsData). The server returns
// queueOpsData.queues separately for UI filter convenience.
const availableQueues = computed(() => {
  const s = new Set()
  const lag = queueLagData.value || []
  for (const r of lag) s.add(r.queueName)
  const ops = queueOpsData.value?.queues || []
  for (const q of ops) s.add(q)
  return [...s].sort()
})

const queuesToShow = computed(() => {
  if (selectedQueues.value.length > 0) return [...selectedQueues.value].sort()
  return availableQueues.value
})

const buildPerQueueChart = (valueAccessor) => {
  const raw = queueLagData.value || []
  if (!raw.length) return { labels: [], datasets: [] }

  const bucketSet = new Set(raw.map(r => r.bucketTime))
  const buckets = [...bucketSet].sort()
  const multiDay = buckets.length >= 2 &&
    new Date(buckets[0]).toDateString() !== new Date(buckets[buckets.length - 1]).toDateString()
  const labels = buckets.map(b => formatChartLabel(new Date(b), multiDay))

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

const queueLagChartData = computed(() => buildPerQueueChart(entry => entry.avgLagMs || 0))

// ---------------------------------------------------------------------------
// Top queues leaderboard
// ---------------------------------------------------------------------------
// Compresses the per-queue spaghetti chart into 4 sortable mini-tables:
// which queues are pushing / popping / parking / lagging the most across
// the selected window. Computed from the same queue-ops series the
// per-queue chart consumes — no extra fetches.
//
// Aggregation rules per metric:
//   - pushPerSecond / popPerSecond  → window average (rate semantics)
//   - parkedCount                   → window average (gauge semantics)
//   - avgLagMs                      → pop-count-weighted average (a queue
//                                     with one slow pop in 60 minutes
//                                     shouldn't dominate the lag chart;
//                                     weighting by pop_count gives the
//                                     "typical message lag" the user feels).
const TOP_QUEUES_LIMIT = 5

const topQueues = computed(() => {
  const series = queueOpsData.value?.series || []
  if (!series.length) return null

  const agg = new Map()
  for (const r of series) {
    let a = agg.get(r.queueName)
    if (!a) {
      a = { queue: r.queueName, n: 0,
            pushSum: 0, popSum: 0, parkedSum: 0,
            lagWeightedSum: 0, popCount: 0 }
      agg.set(r.queueName, a)
    }
    a.n += 1
    a.pushSum   += Number(r.pushPerSecond) || 0
    a.popSum    += Number(r.popPerSecond)  || 0
    a.parkedSum += Number(r.parkedCount)   || 0
    const popMsgs = Number(r.popMessages) || 0
    a.popCount       += popMsgs
    a.lagWeightedSum += (Number(r.avgLagMs) || 0) * popMsgs
  }

  const rows = []
  for (const a of agg.values()) {
    rows.push({
      queue:  a.queue,
      push:   a.n > 0 ? a.pushSum / a.n   : 0,
      pop:    a.n > 0 ? a.popSum  / a.n   : 0,
      parked: a.n > 0 ? a.parkedSum / a.n : 0,
      lag:    a.popCount > 0 ? a.lagWeightedSum / a.popCount : 0,
      popCount: a.popCount
    })
  }

  const top = (key, opts = {}) => {
    const filter = opts.requirePops ? rows.filter(r => r.popCount > 0) : rows
    return [...filter]
      .filter(r => Number(r[key]) > 0)
      .sort((a, b) => b[key] - a[key])
      .slice(0, TOP_QUEUES_LIMIT)
  }

  return {
    push:   top('push'),
    pop:    top('pop'),
    parked: top('parked'),
    // Lag leaderboard only includes queues that actually saw pops in the
    // window — otherwise idle queues with stale max_lag readings poison
    // the ranking.
    lag:    top('lag', { requirePops: true })
  }
})

const hasTopQueueData = computed(() => {
  const t = topQueues.value
  if (!t) return false
  return t.push.length > 0 || t.pop.length > 0 || t.parked.length > 0 || t.lag.length > 0
})

// ---------------------------------------------------------------------------
// Fetcher — only the worker / queue-ops endpoints. The per-replica parked
// fetch is skipped unless we're actually rendering the per-replica view,
// so the steady-state cost is one round-trip per refresh.
// ---------------------------------------------------------------------------
const fetchData = async () => {
  // Show skeleton only on first fetch; refreshes leave the previous
  // data on screen for a smooth update.
  if (!workerData.value) loading.value = true

  try {
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

    const wantParkedReplicas = selectedQueueOp.value === 'parked'
      && viewMode.value === 'individual'

    const [workerRes, queueLagRes, queueOpsRes, retentionRes, parkedReplicasRes] = await Promise.all([
      system.getWorkerMetrics(params),
      system.getQueueLag(params).catch(e => {
        console.warn('Failed to fetch per-queue lag metrics:', e.message)
        return { data: [] }
      }),
      system.getQueueOps(params).catch(e => {
        console.warn('Failed to fetch per-queue ops metrics:', e.message)
        return { data: { series: [], queues: [] } }
      }),
      system.getRetention(params).catch(e => {
        console.warn('Failed to fetch retention timeseries:', e.message)
        return { data: { series: [], totals: {} } }
      }),
      wantParkedReplicas
        ? system.getQueueParkedReplicas(params).catch(e => {
            console.warn('Failed to fetch per-replica parked metrics:', e.message)
            return { data: { series: [], replicas: [] } }
          })
        : Promise.resolve({ data: null })
    ])

    workerData.value = workerRes.data
    queueLagData.value = queueLagRes.data
    queueOpsData.value = queueOpsRes.data
    queueParkedReplicasData.value = parkedReplicasRes.data
    retentionData.value = retentionRes.data
  } catch (err) {
    console.error('Failed to fetch queue operations metrics:', err)
  } finally {
    loading.value = false
  }
}

useRefresh(fetchData)
onMounted(fetchData)

// When the user lands on Parked + individual after the initial fetch,
// kick a refetch so the per-replica series shows up without waiting for
// the global refresh tick. Watcher is intentionally narrow: only
// re-runs when the active op or view mode changes meaningfully.
watch([selectedQueueOp, viewMode], ([op, mode], [prevOp, prevMode]) => {
  if (op === prevOp && mode === prevMode) return
  const wasIndividualParked = prevOp === 'parked' && prevMode === 'individual'
  const isIndividualParked  = op === 'parked' && mode === 'individual'
  if (wasIndividualParked !== isIndividualParked) fetchData()
})
</script>

<style scoped>
@media (max-width: 1100px) {
  div[style*="grid-template-columns:repeat(2"] { grid-template-columns: 1fr !important; }
}

/* Three-column card row (Event Loop / Connection Pool / Job Queue Depth).
   Collapses to 1 column under 1100px to match the 2-col responsive
   behavior used elsewhere on this page. */
.three-col-row {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 16px;
  margin-bottom: 16px;
}
@media (max-width: 1100px) {
  .three-col-row { grid-template-columns: 1fr; }
}

/* Top Queues leaderboard — 2x2 grid so each mini-table gets enough
   horizontal room to show full queue names without ellipsis. The earlier
   4-across layout looked sleek but truncated everything to "test-q…",
   which defeats the panel's purpose ("which queue do I click into?").
   Collapses to a single column under 640px. */
.top-queues-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 16px 24px;
}
@media (max-width: 640px) {
  .top-queues-grid { grid-template-columns: 1fr; }
}
/* Explicit table layout — without `table-layout: fixed`, the auto
   algorithm interprets `max-width: 0` on the qname cell as "this column
   wants zero width", and the resulting allocation truncates names long
   before the leftover space is exhausted. Fixed layout + explicit
   widths on the rank and value columns lets qname reliably absorb
   everything that's left. */
.top-queues { table-layout: fixed; width: 100%; }
.top-queues td { padding: 4px 8px; font-size: 12px; }
.top-queues td.rank {
  width: 28px; color: var(--text-low); text-align: right;
  font-variant-numeric: tabular-nums; font-size: 11px;
}
.top-queues td.qname {
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  font-family: var(--font-mono, ui-monospace, SFMono-Regular, monospace);
  color: var(--text-mid);
}
.top-queues td.val {
  width: 80px;
  text-align: right; white-space: nowrap; color: var(--text-hi);
}
.empty-tile {
  font-size: 12px; color: var(--text-low); text-align: center;
  padding: 16px 0; border: 1px dashed var(--bd); border-radius: 6px;
}
</style>
