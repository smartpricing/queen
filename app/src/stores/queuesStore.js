// Shared queues store — module-level singleton.
//
// Multiple views (Queues, Consumers, Dashboard, …) all need the queue list
// and its derived data (namespaces, tasks, name → meta map). Fetching it
// once and sharing the result across views makes the typical session
// dramatically lighter on the network: switching between Queues and
// Consumers never refetches when the data is fresh, and the namespace
// filter on Consumers piggybacks on what Queues just loaded.
//
// Three things this store does that a per-view fetch can't:
//
//   1. TTL caching. fetchQueues() short-circuits if data was loaded less
//      than `ttlMs` ago. Force refresh with { force: true }.
//   2. In-flight de-duplication. If two views mount at the same time
//      and both call fetchQueues(), only one HTTP request is issued; the
//      second caller awaits the same promise.
//   3. Derived computeds (queueMeta, namespaces, tasks) live on the store
//      so each view doesn't have to roll its own.
//
// API:
//   const { queues, loading, fetchQueues, queueMeta, namespaces, tasks,
//           isFresh, invalidate } = useQueuesStore()
//
//   await fetchQueues()              // use cache if fresh, else fetch
//   await fetchQueues({ force: true }) // always fetch (e.g. after mutation)
//   invalidate()                     // mark cache stale; next fetch will refresh
import { ref, computed } from 'vue'
import { queues as queuesApi } from '@/api'

// Default cache TTL — long enough that incidental view-switching is free,
// short enough that a stale list doesn't deceive the operator. The auto-
// refresh ticker in App.vue (30s) will force-refresh anyway, so this is
// really just for navigation-driven calls.
const DEFAULT_TTL_MS = 30_000

// ---------------------------------------------------------------------------
// Module-level singletons. Every useQueuesStore() call shares these refs.
// ---------------------------------------------------------------------------
const queues = ref([])
const loading = ref(false)
const error = ref(null)
const lastFetched = ref(0)
let inflight = null  // pending Promise, used for de-duplication

// ---------------------------------------------------------------------------
// Fetch with TTL + de-duplication.
// ---------------------------------------------------------------------------
const fetchQueues = async ({ force = false, ttlMs = DEFAULT_TTL_MS } = {}) => {
  const fresh = queues.value.length > 0 && Date.now() - lastFetched.value < ttlMs
  if (!force && fresh) return queues.value
  if (inflight) return inflight

  loading.value = true
  error.value = null

  inflight = (async () => {
    try {
      const r = await queuesApi.list()
      const all = r.data?.queues || r.data || []
      // Filter out queues with empty/whitespace names — these are stale
      // entries from the partition_lookup table that should be cleaned
      // up server-side but show up in the API response today.
      queues.value = all.filter(q => q.name && q.name.trim() !== '')
      lastFetched.value = Date.now()
      return queues.value
    } catch (err) {
      error.value = err
      console.error('Failed to fetch queues:', err)
      return queues.value  // keep last-good data so UI doesn't blank out
    } finally {
      loading.value = false
      inflight = null
    }
  })()
  return inflight
}

const invalidate = () => { lastFetched.value = 0 }

// ---------------------------------------------------------------------------
// Derived data — computed once on the module level so every view sees the
// same Maps without re-deriving on each render.
// ---------------------------------------------------------------------------
const queueMeta = computed(() => {
  const m = new Map()
  for (const q of queues.value) m.set(q.name, q)
  return m
})

const namespaces = computed(() => {
  const set = new Set(
    queues.value.map(q => q.namespace).filter(ns => ns !== undefined)
  )
  return [...set].sort()
})

const tasks = computed(() => {
  const set = new Set(
    queues.value.map(q => q.task).filter(t => t !== undefined)
  )
  return [...set].sort()
})

const isFresh = computed(() =>
  queues.value.length > 0 && Date.now() - lastFetched.value < DEFAULT_TTL_MS
)

// ---------------------------------------------------------------------------
// Public composable. The same singleton refs are returned to every caller,
// so reactivity propagates across views naturally.
// ---------------------------------------------------------------------------
export function useQueuesStore() {
  return {
    queues,
    loading,
    error,
    lastFetched,
    isFresh,
    queueMeta,
    namespaces,
    tasks,
    fetchQueues,
    invalidate,
  }
}
