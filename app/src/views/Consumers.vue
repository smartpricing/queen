<template>
  <div class="view-container">

    <!--
      ========================================================================
      Toolbar — same idiom as Queues.vue: search · filters · sort segmented ·
      legend. Replaces the old 4-stat-card row + separate filter card. The
      "lagging only" check is the only consumer-specific addition.
      ========================================================================
    -->
    <div class="qtoolbar">
      <div class="qtoolbar-search">
        <svg class="qtoolbar-search-icon" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
          <path stroke-linecap="round" stroke-linejoin="round" d="M21 21l-5.197-5.197m0 0A7.5 7.5 0 105.196 5.196a7.5 7.5 0 0010.607 10.607z" />
        </svg>
        <input
          v-model="searchQuery"
          type="text"
          placeholder="Search consumer groups..."
          class="input"
        />
      </div>

      <select v-model="filterNamespace" class="input" style="width:140px;">
        <option value="">All namespaces</option>
        <option v-for="ns in namespaces" :key="ns" :value="ns">{{ ns || '(empty)' }}</option>
      </select>

      <select v-model="filterTask" class="input" style="width:140px;">
        <option value="">All tasks</option>
        <option v-for="t in tasks" :key="t" :value="t">{{ t || '(empty)' }}</option>
      </select>

      <select v-model="filterQueue" class="input" style="width:160px;">
        <option value="">All queues</option>
        <option v-for="q in scopedQueueNames" :key="q" :value="q">{{ q }}</option>
      </select>

      <label class="qtoolbar-check">
        <input v-model="showLaggingOnly" type="checkbox" />
        <span>Lagging only</span>
      </label>

      <span style="flex:1;"></span>

      <span class="label-xs" style="color:var(--text-low);">Sort</span>
      <div class="seg">
        <button
          v-for="opt in sortOptions"
          :key="opt.value"
          :class="{ on: sortBy === opt.value }"
          @click="sortBy = opt.value"
        >{{ opt.label }}</button>
      </div>

      <span class="qhg-legend">
        <span class="ld" style="background:var(--ok-500);"></span> stable
        <span class="ld" style="background:var(--warn-400);"></span> lagging
        <span class="ld" style="background:var(--ember-400);"></span> stuck
        <span class="ld" style="background:var(--bd-hi);"></span> dead
      </span>
    </div>

    <!--
      ========================================================================
      Lagging Partitions — placed at the top because it's actionable: the
      operator wants to know "is anything stuck right now?" before they
      scan the per-group list. Auto-loaded on mount with the 1h default
      threshold so the count badge is visible without the user having to
      open it; collapsed by default so a busy cluster doesn't push the
      main grid below the fold.
      ========================================================================
    -->
    <div class="lp-card" :class="{ 'lp-card-warn': laggingPartitions.length > 0 }">
      <button
        class="lp-head"
        :class="{ 'lp-head-open': showLaggingSection }"
        @click="showLaggingSection = !showLaggingSection"
      >
        <svg class="lp-head-chevron" width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
          <path d="M6 4l4 4-4 4" />
        </svg>
        <span class="lp-head-title">Lagging partitions</span>

        <!-- Count badge — visible whether collapsed or expanded so the
             "is anything stuck?" answer is always one glance away. -->
        <span
          v-if="laggingLoaded && laggingPartitions.length > 0"
          class="lp-head-badge lp-head-badge-warn"
        >
          {{ laggingPartitions.length }}
          {{ laggingPartitions.length === 1 ? 'partition' : 'partitions' }} above {{ getLagLabel(lagThreshold) }}
        </span>
        <span
          v-else-if="laggingLoaded"
          class="lp-head-badge lp-head-badge-ok"
        >
          none above {{ getLagLabel(lagThreshold) }}
        </span>
        <span
          v-else-if="laggingLoading"
          class="lp-head-hint"
        >loading…</span>
        <span
          v-else
          class="lp-head-hint"
        >expand to inspect partitions individually</span>

        <span style="flex:1;"></span>

        <span v-if="showLaggingSection" class="lp-head-meta" @click.stop>
          <span class="label-xs">Threshold</span>
          <div class="seg">
            <button
              v-for="preset in lagPresets"
              :key="preset.value"
              :class="{ on: lagThreshold === preset.value }"
              @click="lagThreshold = preset.value; loadLaggingPartitions()"
            >{{ preset.label }}</button>
          </div>
          <button
            class="btn btn-ghost"
            :disabled="laggingLoading"
            @click="loadLaggingPartitions"
          >
            <svg style="width:12px; height:12px;" :class="{ 'animate-spin': laggingLoading }" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
              <path stroke-linecap="round" stroke-linejoin="round" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
            </svg>
            {{ laggingLoading ? 'Loading…' : 'Refresh' }}
          </button>
        </span>
      </button>

      <div v-if="showLaggingSection" class="lp-body">
        <div v-if="laggingLoading" class="lp-empty">
          <span class="spinner" style="margin-right:10px;" />
          Loading lagging partitions…
        </div>

        <div v-else-if="laggingPartitions.length > 0" class="lp-table-wrap">
          <table class="t lp-table">
            <thead>
              <tr>
                <th>Group</th>
                <th>Queue</th>
                <th>Partition</th>
                <th>Worker</th>
                <th style="text-align:right;">Offset lag</th>
                <th style="text-align:right;">Time lag</th>
                <th>Oldest unconsumed</th>
                <th style="text-align:right;">Action</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="p in laggingPartitions" :key="`${p.consumer_group}@${p.queue_name}@${p.partition_name}`">
                <td><span class="lp-cell-strong">{{ p.consumer_group === '__QUEUE_MODE__' ? 'queue mode' : p.consumer_group }}</span></td>
                <td><span class="lp-cell-mid">{{ p.queue_name }}</span></td>
                <td class="font-mono tabular-nums" style="color:var(--text-mid);">{{ p.partition_name }}</td>
                <td>
                  <code class="lp-cell-code">{{ p.worker_id || '—' }}</code>
                </td>
                <td style="text-align:right;">
                  <span class="font-mono tabular-nums num warn">{{ formatNumber(p.offset_lag) }}</span>
                </td>
                <td style="text-align:right;">
                  <span
                    class="font-mono tabular-nums"
                    :class="{ 'num': true, 'warn': (p.time_lag_seconds || 0) > 60, 'bad': (p.time_lag_seconds || 0) > 600 }"
                  >{{ formatDuration((p.time_lag_seconds || 0) * 1000) }}</span>
                </td>
                <td>
                  <span class="font-mono" style="font-size:11.5px; color:var(--text-mid);">{{ formatTimestamp(p.oldest_unconsumed_at) }}</span>
                </td>
                <td style="text-align:right;">
                  <button
                    class="btn btn-ghost"
                    style="font-size:11px; padding:3px 8px;"
                    :disabled="skippingPartition === `${p.consumer_group}-${p.queue_name}-${p.partition_name}`"
                    @click="handleSkipPartition(p)"
                  >
                    {{ skippingPartition === `${p.consumer_group}-${p.queue_name}-${p.partition_name}` ? 'Skipping…' : 'Skip to end' }}
                  </button>
                </td>
              </tr>
            </tbody>
          </table>
        </div>

        <div v-else class="lp-empty lp-empty-ok">
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="color:var(--ok-500);">
            <path stroke-linecap="round" stroke-linejoin="round" d="M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z" />
          </svg>
          <span>No partitions lag more than {{ getLagLabel(lagThreshold) }}.</span>
        </div>
      </div>
    </div>

    <!--
      ========================================================================
      Health grid — the main entity list. Click row → detail modal; hover
      reveals per-row actions (view, move-to-now, seek, delete).
      ========================================================================
    -->
    <ConsumerHealthGrid
      :consumers="filteredConsumers"
      :loading="loading"
      :sort-by="sortBy"
      @select="viewConsumer"
      @view="viewConsumer"
      @move-now="handleMoveToNow"
      @seek="openSeekModal"
      @delete="confirmDelete"
    >
      <template #empty>
        <h3 style="font-size:13px; font-weight:600; color:var(--text-hi); margin:0 0 4px;">
          {{ consumers.length === 0 ? 'No consumer groups found' : 'No groups match your filters' }}
        </h3>
        <p style="font-size:13px; color:var(--text-mid); margin:0;">
          {{ consumers.length === 0
            ? 'Consumer groups will appear here when clients connect.'
            : 'Try adjusting your search or filter.' }}
        </p>
      </template>
    </ConsumerHealthGrid>

    <!--
      ========================================================================
      Modals — kept verbatim from the previous design. Functionally
      unchanged; just live below the new grid in the document.
      ========================================================================
    -->
    <div
      v-if="selectedConsumer"
      class="modal-backdrop"
      @click="selectedConsumer = null"
    >
      <div class="card modal-card" style="max-width:672px;" @click.stop>
        <div class="card-header" style="justify-content:space-between;">
          <h3>{{ selectedConsumer.name === '__QUEUE_MODE__' ? selectedConsumer.queueName : selectedConsumer.name }}</h3>
          <button @click="selectedConsumer = null" class="btn btn-ghost btn-icon">
            <svg style="width:18px; height:18px;" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
              <path stroke-linecap="round" stroke-linejoin="round" d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>
        <div class="card-body" style="overflow-y:auto; max-height:60vh;">
          <div class="grid-4" style="margin-bottom:24px;">
            <div class="stat" style="text-align:center; min-height:auto; padding:16px;">
              <div class="stat-label" style="justify-content:center;">Partitions</div>
              <div class="stat-value font-mono" style="font-size:24px;">{{ selectedConsumer.members || 0 }}</div>
            </div>
            <div class="stat" style="text-align:center; min-height:auto; padding:16px;">
              <div class="stat-label" style="justify-content:center;">State</div>
              <div
                style="font-size:18px; font-weight:600; margin-top:8px;"
                :style="getStatusText(selectedConsumer) === 'Stable'
                  ? { color: 'var(--ok-500)' }
                  : getStatusText(selectedConsumer) === 'Lagging'
                    ? { color: 'var(--warn-400)' }
                    : { color: 'var(--text-mid)' }"
              >{{ getStatusText(selectedConsumer) }}</div>
            </div>
            <div class="stat" style="text-align:center; min-height:auto; padding:16px;">
              <div class="stat-label" style="justify-content:center;">Lag parts</div>
              <div class="stat-value font-mono" style="font-size:24px;" :style="{ color: (selectedConsumer.partitionsWithLag || 0) > 0 ? 'var(--warn-400)' : 'var(--text-hi)' }">
                {{ selectedConsumer.partitionsWithLag || 0 }}
              </div>
            </div>
            <div class="stat" style="text-align:center; min-height:auto; padding:16px;">
              <div class="stat-label" style="justify-content:center;">Time lag</div>
              <div class="stat-value font-mono" style="font-size:24px; color:var(--text-hi);">
                {{ (selectedConsumer.maxTimeLag || 0) > 0 ? formatDuration(selectedConsumer.maxTimeLag * 1000) : '—' }}
              </div>
            </div>
          </div>

          <div style="margin-bottom:16px;">
            <span class="label-xs" style="display:block; margin-bottom:8px;">Queue</span>
            <div class="modal-queue-pill">
              <span style="font-weight:500; color:var(--text-hi);">{{ selectedConsumer.queueName || '—' }}</span>
            </div>
          </div>

          <div v-if="selectedConsumer.topics?.length > 0">
            <span class="label-xs" style="display:block; margin-bottom:12px;">Topics</span>
            <div style="display:flex; flex-direction:column; gap:8px;">
              <div
                v-for="topic in selectedConsumer.topics"
                :key="topic"
                class="modal-queue-pill"
                style="display:flex; align-items:center; justify-content:space-between;"
              >
                <span style="font-weight:500; color:var(--text-hi);">{{ topic }}</span>
                <button @click="seekQueue(selectedConsumer.name, topic)" class="btn btn-ghost">Seek</button>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <div
      v-if="consumerToDelete"
      class="modal-backdrop"
      @click="consumerToDelete = null"
    >
      <div class="card modal-card" style="padding:24px; max-width:448px;" @click.stop>
        <h3 style="font-size:18px; font-weight:600; color:var(--text-hi); margin-bottom:8px;">
          Delete consumer group
        </h3>
        <p style="color:var(--text-mid); margin-bottom:16px;">
          Are you sure you want to delete <strong>{{ consumerToDelete.name === '__QUEUE_MODE__' ? '(queue mode)' : consumerToDelete.name }}</strong>
          <span v-if="consumerToDelete.queueName"> for queue <strong>{{ consumerToDelete.queueName }}</strong></span>?
        </p>
        <p style="font-size:13px; color:var(--text-low); margin-bottom:24px;">
          This will remove partition consumer state{{ consumerToDelete.queueName ? ' for this queue only' : '' }}.
        </p>

        <label style="display:flex; align-items:center; gap:8px; font-size:13px; color:var(--text-mid); margin-bottom:24px; cursor:pointer;">
          <input v-model="deleteMetadata" type="checkbox" style="width:16px; height:16px; accent-color:var(--accent);" />
          Also delete subscription metadata
        </label>

        <div style="display:flex; align-items:center; justify-content:flex-end; gap:12px;">
          <button @click="consumerToDelete = null" class="btn btn-ghost">Cancel</button>
          <button @click="deleteConsumer" :disabled="actionLoading" class="btn btn-danger">
            {{ actionLoading ? 'Deleting…' : 'Delete' }}
          </button>
        </div>
      </div>
    </div>

    <div
      v-if="showSeekModal"
      class="modal-backdrop"
      @click="showSeekModal = false"
    >
      <div class="card modal-card" style="padding:24px; max-width:448px;" @click.stop>
        <h3 style="font-size:18px; font-weight:600; color:var(--text-hi); margin-bottom:8px;">
          Seek cursor position
        </h3>
        <p style="color:var(--text-mid); margin-bottom:16px;">
          {{ seekConsumer?.name === '__QUEUE_MODE__' ? '(queue mode)' : seekConsumer?.name }} / {{ seekConsumer?.queueName }}
        </p>

        <div style="margin-bottom:16px;">
          <span class="label-xs" style="display:block; margin-bottom:8px;">Target timestamp</span>
          <input v-model="seekTimestamp" type="datetime-local" class="input" />
          <p style="font-size:12px; color:var(--text-low); margin-top:8px;">
            The cursor will move to the last message at or before this timestamp. Messages after this point will be re-consumed.
          </p>
        </div>

        <div style="display:flex; align-items:center; justify-content:flex-end; gap:12px;">
          <button @click="showSeekModal = false" class="btn btn-ghost">Cancel</button>
          <button @click="handleSeekToTimestamp" :disabled="actionLoading || !seekTimestamp" class="btn btn-primary">
            {{ actionLoading ? 'Seeking…' : 'Seek to timestamp' }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, watch } from 'vue'
import { useRoute } from 'vue-router'
import { consumers as consumersApi } from '@/api'
import { formatNumber, formatDuration } from '@/composables/useApi'
import { useRefresh } from '@/composables/useRefresh'
import { useQueuesStore } from '@/stores/queuesStore'
import ConsumerHealthGrid from '@/components/ConsumerHealthGrid.vue'

// Shared queues store. The Queues page populates it; we just consume here.
// queueMeta gives us the queue → { namespace, task } join we need to scope
// consumer rows by namespace/task without a second API call when the data
// is fresh.
const { queueMeta, namespaces, tasks, fetchQueues } = useQueuesStore()

const route = useRoute()

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
const consumers = ref([])
const loading = ref(true)
const searchQuery = ref('')
const showLaggingOnly = ref(false)
const filterNamespace = ref('')
const filterTask = ref('')
const filterQueue = ref('')
const sortBy = ref('health')

const selectedConsumer = ref(null)
const consumerToDelete = ref(null)
const deleteMetadata = ref(true)
const actionLoading = ref(false)

const showSeekModal = ref(false)
const seekConsumer = ref(null)
const seekTimestamp = ref('')

const showLaggingSection = ref(false)
const laggingPartitions = ref([])
const laggingLoading = ref(false)
const laggingLoaded = ref(false)  // first fetch completed → drives the count badge
const lagThreshold = ref(3600)

const skippingPartition = ref(null)

const sortOptions = [
  { value: 'health',  label: 'Worst first' },
  { value: 'lag',     label: 'Lag' },
  { value: 'members', label: 'Partitions' },  // sort key kept for back-compat with grid
  { value: 'name',    label: 'Name' },
]

// Wider time-range presets are kept for power users; the defaults
// (1m → 24h) cover everything from "subtle drift" to "stuck for a day".
const lagPresets = [
  { value: 60,    label: '1m'  },
  { value: 300,   label: '5m'  },
  { value: 1800,  label: '30m' },
  { value: 3600,  label: '1h'  },
  { value: 21600, label: '6h'  },
  { value: 86400, label: '24h' },
]

// ---------------------------------------------------------------------------
// Computed
// ---------------------------------------------------------------------------
const isLagging = (g) => {
  if (g.state === 'Lagging') return true
  return (g.maxTimeLag || 0) > 0 || (g.partitionsWithLag || 0) > 0
}

// Helper — pull the queue meta (namespace/task) for a consumer row.
// Returns an empty object when the queue isn't in the store yet, so
// undefined-namespace queues match an empty namespace filter.
const metaFor = (c) => queueMeta.value.get(c.queueName) || {}

// "Queue" dropdown is scoped to the active namespace/task filter, so it
// doesn't show every queue in the cluster when the user has narrowed.
// Also limited to queues that actually have consumer groups, so the
// dropdown isn't padded with idle queues.
const scopedQueueNames = computed(() => {
  const seen = new Set()
  for (const c of consumers.value) {
    const meta = metaFor(c)
    if (filterNamespace.value && meta.namespace !== filterNamespace.value) continue
    if (filterTask.value && meta.task !== filterTask.value) continue
    if (c.queueName) seen.add(c.queueName)
  }
  return [...seen].sort()
})

const filteredConsumers = computed(() => {
  let result = [...consumers.value]
  if (searchQuery.value) {
    const q = searchQuery.value.toLowerCase()
    result = result.filter(c =>
      (c.name || '').toLowerCase().includes(q) ||
      (c.queueName || '').toLowerCase().includes(q)
    )
  }
  if (filterNamespace.value) {
    result = result.filter(c => metaFor(c).namespace === filterNamespace.value)
  }
  if (filterTask.value) {
    result = result.filter(c => metaFor(c).task === filterTask.value)
  }
  if (filterQueue.value) {
    result = result.filter(c => c.queueName === filterQueue.value)
  }
  if (showLaggingOnly.value) {
    result = result.filter(isLagging)
  }
  return result
})

// ---------------------------------------------------------------------------
// Methods
// ---------------------------------------------------------------------------
const getStatusText = (g) => g.state || 'Unknown'

const fetchConsumers = async () => {
  if (!consumers.value.length) loading.value = true
  try {
    const r = await consumersApi.list()
    consumers.value = Array.isArray(r.data) ? r.data : (r.data?.consumer_groups || [])
  } catch (err) {
    console.error('Failed to fetch consumers:', err)
  } finally {
    loading.value = false
  }
}

// Pull queues into the shared store (cache-respecting). If another view
// already populated it within the TTL window, this is a no-op.
const ensureQueues = (force = false) => fetchQueues({ force })

// Refresh both consumers and queues on each tick. The auto-refresh
// interval is the right time to force-bust the cache because the user
// has chosen to receive fresh data.
const refreshAll = async () => {
  await Promise.all([fetchConsumers(), ensureQueues(true)])
}

// Clear queue filter when it falls out of the namespace/task scope, so
// the user doesn't end up with an empty result set after narrowing.
watch([filterNamespace, filterTask], () => {
  if (filterQueue.value && !scopedQueueNames.value.includes(filterQueue.value)) {
    filterQueue.value = ''
  }
})

const viewConsumer = (g) => { selectedConsumer.value = g }
const confirmDelete = (g) => { consumerToDelete.value = g }

const deleteConsumer = async () => {
  if (!consumerToDelete.value) return
  actionLoading.value = true
  try {
    if (consumerToDelete.value.queueName) {
      await consumersApi.deleteForQueue(
        consumerToDelete.value.name,
        consumerToDelete.value.queueName,
        deleteMetadata.value
      )
    } else {
      await consumersApi.delete(consumerToDelete.value.name, deleteMetadata.value)
    }
    consumerToDelete.value = null
    fetchConsumers()
  } catch (err) {
    console.error('Failed to delete consumer:', err)
  } finally {
    actionLoading.value = false
  }
}

const handleMoveToNow = async (g) => {
  if (!g.queueName) return
  const groupLabel = g.name === '__QUEUE_MODE__' ? '(queue mode)' : g.name
  if (!confirm(`Move cursor to now for "${groupLabel}" on queue "${g.queueName}"?\n\nThis will skip all pending messages and start consuming from the latest message.`)) {
    return
  }
  actionLoading.value = true
  try {
    await consumersApi.seek(g.name, g.queueName, { toEnd: true })
    fetchConsumers()
  } catch (err) {
    console.error('Failed to move to now:', err)
  } finally {
    actionLoading.value = false
  }
}

const handleSkipPartition = async (p) => {
  const key = `${p.consumer_group}-${p.queue_name}-${p.partition_name}`
  if (!confirm(`Skip to end for partition "${p.partition_name}" on queue "${p.queue_name}" (group: ${p.consumer_group})?\n\nThis advances the cursor to the latest message, skipping all pending messages on this partition.`)) {
    return
  }
  skippingPartition.value = key
  try {
    await consumersApi.seekPartition(p.consumer_group, p.queue_name, p.partition_name)
    await loadLaggingPartitions()
  } catch (err) {
    console.error('Failed to skip partition:', err)
    alert('Failed to skip partition: ' + (err.response?.data?.error || err.message))
  } finally {
    skippingPartition.value = null
  }
}

const openSeekModal = (g) => {
  seekConsumer.value = g
  const oneHourAgo = new Date(Date.now() - 60 * 60 * 1000)
  seekTimestamp.value = formatDateTimeLocal(oneHourAgo)
  showSeekModal.value = true
}

const handleSeekToTimestamp = async () => {
  if (!seekConsumer.value || !seekTimestamp.value) return
  actionLoading.value = true
  try {
    const isoTimestamp = new Date(seekTimestamp.value).toISOString()
    await consumersApi.seek(seekConsumer.value.name, seekConsumer.value.queueName, { timestamp: isoTimestamp })
    showSeekModal.value = false
    seekTimestamp.value = ''
    fetchConsumers()
  } catch (err) {
    console.error('Failed to seek:', err)
  } finally {
    actionLoading.value = false
  }
}

const formatDateTimeLocal = (date) => {
  const y = date.getFullYear()
  const mo = String(date.getMonth() + 1).padStart(2, '0')
  const d = String(date.getDate()).padStart(2, '0')
  const h = String(date.getHours()).padStart(2, '0')
  const mi = String(date.getMinutes()).padStart(2, '0')
  return `${y}-${mo}-${d}T${h}:${mi}`
}

const loadLaggingPartitions = async () => {
  laggingLoading.value = true
  try {
    const r = await consumersApi.getLagging(lagThreshold.value)
    laggingPartitions.value = r.data || []
  } catch (err) {
    console.error('Failed to load lagging partitions:', err)
    laggingPartitions.value = []
  } finally {
    laggingLoading.value = false
    laggingLoaded.value = true
  }
}

const getLagLabel = (seconds) => {
  if (seconds < 3600) {
    const m = Math.round(seconds / 60); return `${m} minute${m !== 1 ? 's' : ''}`
  } else if (seconds < 86400) {
    const h = Math.round(seconds / 3600); return `${h} hour${h !== 1 ? 's' : ''}`
  }
  const d = Math.round(seconds / 86400); return `${d} day${d !== 1 ? 's' : ''}`
}

const formatTimestamp = (ts) => {
  if (!ts) return '—'
  try {
    return new Date(ts).toLocaleString('en-US', {
      month: 'short', day: 'numeric',
      hour: '2-digit', minute: '2-digit', second: '2-digit',
    })
  } catch { return ts }
}

// When the user expands the section, refresh the lagging list — even if it's
// already loaded the threshold may have been adjusted while collapsed.
watch(showLaggingSection, (shown) => {
  if (shown) loadLaggingPartitions()
})

useRefresh(refreshAll)

onMounted(() => {
  if (route.query.search) searchQuery.value = route.query.search
  fetchConsumers()
  // Reuse the shared queue cache when possible — if the Queues page
  // recently populated it, this returns immediately without a network
  // round-trip.
  ensureQueues()
  // Auto-load with the default threshold so the count badge in the header
  // reflects reality without requiring the user to expand the section.
  loadLaggingPartitions()
})
</script>

<style scoped>
/* Toolbar — same shape and tokens as Queues.vue so the two pages read
   as siblings. The lagging-only checkbox is the only consumer-specific
   addition. */
.qtoolbar {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
  padding: 10px 14px;
  margin-bottom: 12px;
  background: var(--ink-2);
  border: 1px solid var(--bd);
  border-radius: 6px;
}
.qtoolbar-search {
  position: relative;
  flex: 1;
  min-width: 220px;
  max-width: 360px;
}
.qtoolbar-search .input { padding-left: 32px; width: 100%; }
.qtoolbar-search-icon {
  position: absolute;
  left: 10px; top: 50%;
  transform: translateY(-50%);
  width: 14px; height: 14px;
  color: var(--text-low);
  pointer-events: none;
}
.qtoolbar-check {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: var(--text-mid);
  cursor: pointer;
  padding: 0 4px;
  white-space: nowrap;
}
.qtoolbar-check input { width: 14px; height: 14px; accent-color: var(--accent); }
.qhg-legend {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 11px;
  color: var(--text-mid);
  margin-left: 4px;
}
.qhg-legend .ld { width: 7px; height: 7px; border-radius: 99px; }
@media (max-width: 880px) {
  .qhg-legend { display: none; }
}

/* Lagging Partitions card — placed at the top, collapsible. Shares the
   toolbar's surface tokens. When any partition is above threshold, the
   card carries a subtle warn tint along the left edge so the operator
   sees something is off without having to read the badge. */
.lp-card {
  background: var(--ink-2);
  border: 1px solid var(--bd);
  border-radius: 6px;
  overflow: hidden;
  margin-bottom: 12px;
  transition: border-color .15s var(--ease);
}
.lp-card-warn {
  border-color: rgba(230, 180, 80, .35);
  box-shadow: inset 3px 0 0 var(--warn-400);
}
.lp-head {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 8px 14px;
  background: transparent;
  border: none;
  border-bottom: 1px solid transparent;
  cursor: pointer;
  text-align: left;
  color: var(--text-mid);
  font-size: 12px;
  transition: background .12s var(--ease), border-color .12s var(--ease);
}
.lp-head:hover { background: rgba(255, 255, 255, .02); }
.lp-head-open { border-bottom-color: var(--bd); }
.lp-head-chevron {
  color: var(--text-low);
  transition: transform .15s var(--ease);
  flex-shrink: 0;
}
.lp-head-open .lp-head-chevron { transform: rotate(90deg); }
.lp-head-title {
  font-size: 12.5px;
  font-weight: 600;
  color: var(--text-hi);
  letter-spacing: -.005em;
}

/* Always-visible count badge — warn-toned when partitions are behind,
   muted-ok when none. The badge is the operator's first signal; the
   expanded body is the detail. */
.lp-head-badge {
  display: inline-flex;
  align-items: center;
  padding: 2px 8px;
  border-radius: 99px;
  font-family: 'JetBrains Mono', monospace;
  font-size: 11px;
  font-weight: 500;
  border: 1px solid transparent;
  white-space: nowrap;
}
.lp-head-badge-warn {
  color: var(--warn-400);
  background: rgba(230, 180, 80, .10);
  border-color: rgba(230, 180, 80, .26);
}
.lp-head-badge-ok {
  color: var(--text-low);
  background: rgba(255, 255, 255, .025);
  border-color: var(--bd);
}

.lp-head-hint {
  font-size: 11.5px;
  color: var(--text-low);
}
.lp-head-meta {
  display: inline-flex;
  align-items: center;
  gap: 8px;
}
.lp-body {
  padding: 6px 0;
}
.lp-empty {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 24px;
  color: var(--text-mid);
  font-size: 13px;
}
.lp-empty-ok { color: var(--text-low); }
.lp-table-wrap {
  overflow-x: auto;
}
.lp-table { width: 100%; }
.lp-cell-strong { color: var(--text-hi); font-weight: 500; }
.lp-cell-mid { color: var(--text-mid); }
.lp-cell-code {
  font-family: 'JetBrains Mono', monospace;
  font-size: 11.5px;
  padding: 1px 6px;
  border-radius: 3px;
  background: var(--ink-3);
  border: 1px solid var(--bd);
  color: var(--text-mid);
}

/* Modal scaffolding — kept compact so the existing modal contents
   render the same way as before. */
.modal-backdrop {
  position: fixed; inset: 0;
  z-index: 50;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 16px;
  background: rgba(7, 7, 10, .6);
  backdrop-filter: blur(12px);
}
.modal-card {
  width: 100%;
  max-height: 80vh;
  overflow: hidden;
}
.modal-queue-pill {
  padding: 12px;
  border-radius: 10px;
  border: 1px solid var(--bd);
  background: rgba(255, 255, 255, .02);
}
</style>
