<template>
  <div class="view-container">

    <!-- Toolbar: search + filters + sort -->
    <div class="qtoolbar">
      <div class="qtoolbar-search">
        <svg class="qtoolbar-search-icon" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
          <path stroke-linecap="round" stroke-linejoin="round" d="M21 21l-5.197-5.197m0 0A7.5 7.5 0 105.196 5.196a7.5 7.5 0 0010.607 10.607z" />
        </svg>
        <input
          v-model="searchQuery"
          type="text"
          placeholder="Search queues..."
          class="input"
        />
      </div>

      <select v-model="filterNamespace" class="input" style="width:160px;">
        <option value="">All namespaces</option>
        <option v-for="ns in namespaces" :key="ns" :value="ns">
          {{ ns || '(empty)' }}
        </option>
      </select>

      <select v-model="filterTask" class="input" style="width:160px;">
        <option value="">All tasks</option>
        <option v-for="task in tasks" :key="task" :value="task">
          {{ task || '(empty)' }}
        </option>
      </select>

      <span style="flex:1;"></span>

      <span class="label-xs" style="color:var(--text-low);">Sort</span>
      <div class="seg">
        <button
          v-for="opt in sortOptions"
          :key="opt.value"
          :class="{ on: sortBy === opt.value }"
          @click="sortBy = opt.value"
        >
          {{ opt.label }}
        </button>
      </div>

      <span class="qhg-legend">
        <span class="ld" style="background:var(--ok-500);"></span> healthy
        <span class="ld" style="background:var(--ice-400);"></span> idle
        <span class="ld" style="background:var(--warn-400);"></span> elevated
        <span class="ld" style="background:var(--ember-400);"></span> falling behind
      </span>
    </div>

    <!-- Health grid -->
    <QueueHealthGrid
      :queues="filteredQueues"
      :loading="loading"
      :sort-by="sortBy"
      :show-hot="false"
      @select="viewQueue"
      @delete="confirmDelete"
    >
      <template #empty>
        <h3 style="font-size:13px; font-weight:600; color:var(--text-hi); margin:0 0 4px;">
          No queues found
        </h3>
        <p style="font-size:13px; color:var(--text-mid); margin:0;">
          {{ searchQuery || filterNamespace || filterTask ? 'Try adjusting your filters' : 'Create a queue to get started' }}
        </p>
      </template>
    </QueueHealthGrid>

    <!-- Delete confirmation modal -->
    <div
      v-if="showDeleteModal"
      style="position:fixed; inset:0; z-index:50; display:flex; align-items:center; justify-content:center; padding:16px; background:rgba(0,0,0,.5); backdrop-filter:blur(4px);"
      @click="showDeleteModal = false"
    >
      <div
        class="card animate-slide-up"
        style="padding:24px; width:100%; max-width:420px;"
        @click.stop
      >
        <h3 style="font-size:16px; font-weight:600; color:var(--text-hi); margin-bottom:8px;">
          Delete Queue
        </h3>
        <p style="color:var(--text-mid); margin-bottom:24px;">
          Are you sure you want to delete <strong>{{ queueToDelete?.name }}</strong>? This will permanently remove the queue and all its messages. This action cannot be undone.
        </p>
        <div style="display:flex; align-items:center; justify-content:flex-end; gap:12px;">
          <button @click="showDeleteModal = false" class="btn btn-ghost">Cancel</button>
          <button @click="deleteQueue" class="btn btn-danger">Delete Queue</button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { queues as queuesApi, system as systemApi } from '@/api'
import { useRefresh } from '@/composables/useRefresh'
import { useQueuesStore } from '@/stores/queuesStore'
import QueueHealthGrid from '@/components/QueueHealthGrid.vue'

const router = useRouter()

// Shared store — same singleton the Consumers page reads. Calling
// fetchQueues({ force: true }) on auto-refresh keeps it fresh; navigating
// to Consumers afterwards returns instantly from cache.
const queuesStore = useQueuesStore()
const { queues, loading, namespaces: storeNamespaces, tasks: storeTasks, fetchQueues: fetchQueuesShared, invalidate } = queuesStore

const opsByQueue = ref(new Map())
const searchQuery = ref('')
const filterNamespace = ref('')
const filterTask = ref('')
const sortBy = ref('health')

// Modal state
const showDeleteModal = ref(false)
const queueToDelete = ref(null)

const sortOptions = [
  { value: 'health', label: 'Worst first' },
  { value: 'avgLagMs', label: 'Lag' },
  { value: 'density', label: 'Density' },
  { value: 'partitions', label: 'Partitions' },
  { value: 'name', label: 'Name' },
]

// Re-export the store-derived lists with the names this template already
// uses, so the template doesn't need to change.
const namespaces = storeNamespaces
const tasks = storeTasks

/**
 * Merge each queue with its latest queue-ops aggregate and derive the
 * fields the grid needs (density, throughput, lag, hotCount).
 *
 *   density   = total / partitions — lifetime messages per partition.
 *               Stays meaningful even when the queue is currently empty
 *               (so a heavily-used queue still reads "loaded").
 *   pushPerSec / popPerSec = average over the last 15m
 *   avgLagMs  = max of maxLagMs across the window (p99-like indicator)
 *   hotCount  = null until backend exposes a hot-count procedure
 */
const enrichedQueues = computed(() => {
  return queues.value.map(q => {
    const partitions = q.partitions || 0
    const total = q.messages?.total || 0
    const pending = Math.max(0, q.messages?.pending || 0)
    const ops = opsByQueue.value.get(q.name)
    return {
      name: q.name,
      namespace: q.namespace,
      task: q.task,
      partitions,
      pending,
      processing: q.messages?.processing || 0,
      total,
      density: partitions > 0 ? total / partitions : 0,
      pushPerSec: Number(ops?.pushPerSecond) || 0,
      popPerSec: Number(ops?.popPerSecond) || 0,
      avgLagMs: Number(ops?.maxLagMs) || 0,
      hotCount: null,
    }
  })
})

// Filter (sort happens inside QueueHealthGrid)
const filteredQueues = computed(() => {
  let result = enrichedQueues.value

  if (searchQuery.value) {
    const query = searchQuery.value.toLowerCase()
    result = result.filter(q => q.name.toLowerCase().includes(query))
  }
  if (filterNamespace.value !== '') {
    result = result.filter(q => q.namespace === filterNamespace.value)
  }
  if (filterTask.value !== '') {
    result = result.filter(q => q.task === filterTask.value)
  }
  return result
})

// Methods — fetchQueues is now thin shim around the shared store.
// On mount we use the cache (instant if Consumers/Dashboard already loaded
// queues); on auto-refresh we force-bust so we get fresh data.
const fetchQueues = (force = false) => fetchQueuesShared({ force })

/* Pull last 15m of queue-ops and aggregate per queue across the window.
 *
 * We can't just take the most recent 1-min bucket: queues are often bursty,
 * so a quiet bucket right after heavy activity reads as "0 msg/s" even
 * though there's real recent throughput. Instead we sum push/pop messages
 * over the full window and divide by its duration to get an average rate,
 * and take the max of maxLagMs as a p99-like lag indicator.
 *
 * Failure here is non-fatal — the grid renders with throughput/lag = 0
 * (which the severity rules treat as 'mute'). */
const fetchQueueOps = async () => {
  try {
    const now = new Date()
    const windowMs = 15 * 60 * 1000
    const from = new Date(now.getTime() - windowMs)
    const r = await systemApi.getQueueOps({
      from: from.toISOString(),
      to: now.toISOString(),
    })
    const series = r.data?.series || []
    const windowSec = windowMs / 1000

    const agg = new Map()
    for (const row of series) {
      const name = row.queueName
      if (!name) continue
      const cur = agg.get(name) || { pushMessages: 0, popMessages: 0, maxLagMs: 0 }
      cur.pushMessages += Number(row.pushMessages) || 0
      cur.popMessages += Number(row.popMessages) || 0
      cur.maxLagMs = Math.max(cur.maxLagMs, Number(row.maxLagMs) || 0)
      agg.set(name, cur)
    }

    const result = new Map()
    for (const [name, a] of agg) {
      result.set(name, {
        pushPerSecond: a.pushMessages / windowSec,
        popPerSecond: a.popMessages / windowSec,
        maxLagMs: a.maxLagMs,
      })
    }
    opsByQueue.value = result
  } catch (err) {
    console.error('Failed to fetch queue-ops:', err)
  }
}

// Auto-refresh forces fresh queues; mount-time call reuses cache.
const refreshAll = async () => {
  await Promise.all([fetchQueues(true), fetchQueueOps()])
}

const viewQueue = (queue) => {
  router.push(`/queues/${queue.name}`)
}

const confirmDelete = (queue) => {
  queueToDelete.value = queue
  showDeleteModal.value = true
}

const deleteQueue = async () => {
  if (!queueToDelete.value) return
  try {
    await queuesApi.delete(queueToDelete.value.name)
    showDeleteModal.value = false
    queueToDelete.value = null
    invalidate()  // mutation happened — bust the cache so other views see it
    refreshAll()
  } catch (err) {
    console.error('Failed to delete queue:', err)
  }
}

useRefresh(refreshAll)

// On mount, hit the cache first. If the data is fresh (e.g. user just came
// from /consumers), the queue list shows instantly with zero network.
onMounted(() => {
  fetchQueues()       // cache-respecting
  fetchQueueOps()
})
</script>

<style scoped>
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
  left: 10px;
  top: 50%;
  transform: translateY(-50%);
  width: 14px;
  height: 14px;
  color: var(--text-low);
  pointer-events: none;
}

.qhg-legend {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 11px;
  color: var(--text-mid);
  margin-left: 4px;
}
.qhg-legend .ld {
  width: 7px;
  height: 7px;
  border-radius: 99px;
}

@media (max-width: 880px) {
  .qhg-legend { display: none; }
}
</style>
