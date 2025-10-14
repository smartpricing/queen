# Frontend Dashboard Refactor Plan

## Current State Analysis

### Existing Structure
```
dashboard/src/
├── services/
│   ├── api.js          ← Uses OLD endpoints (/resources/* and /analytics/*)
│   └── websocket.js
├── views/
│   ├── Dashboard.vue    ← Main dashboard (metrics, charts, activity)
│   ├── Queues.vue       ← Queue list view
│   ├── QueueDetail.vue  ← Single queue detail
│   ├── Analytics.vue    ← Analytics charts
│   └── Messages.vue     ← Message browser
├── components/
│   ├── charts/          ← Chart components (reusable)
│   ├── cards/           ← Metric cards
│   ├── common/          ← Common components
│   └── layout/          ← Layout components
```

### Current API Endpoints Being Used (OLD)

#### From `api.js`:
1. **Resources (need to replace)**
   - `GET /resources/overview` → System overview
   - `GET /resources/queues` → List queues
   - `GET /resources/queues/:name` → Queue detail
   - `GET /resources/partitions` → List partitions
   - `GET /resources/namespaces` → List namespaces
   - `GET /resources/tasks` → List tasks

2. **Analytics (need to replace)**
   - `GET /analytics/queue/:queue` → Queue analytics
   - `GET /analytics/queues` → All queues analytics
   - `GET /analytics/queue-depths` → Queue depths
   - `GET /analytics/throughput` → Throughput data
   - `GET /analytics/queue-stats` → Queue stats
   - `GET /analytics/queue-lag` → Queue lag

3. **Messages (keep, but may need adjustments)**
   - `GET /messages` → List messages
   - `GET /messages/:id` → Get message
   - etc.

4. **Other (keep)**
   - Health, metrics, push, pop, ack endpoints

---

## New API Endpoints (IMPLEMENTED)

We created 5 new unified endpoints under `/api/v1/status`:

1. **`GET /api/v1/status`** - Dashboard overview
   - Replaces: `/resources/overview`, `/analytics/throughput`, `/analytics/queue-depths`
   - Returns: throughput, queues, messages, leases, DLQ stats

2. **`GET /api/v1/status/queues`** - List queues with stats
   - Replaces: `/resources/queues`, `/analytics/queues`
   - Returns: queues with message stats, lag, performance

3. **`GET /api/v1/status/queues/:queue`** - Queue detail
   - Replaces: `/resources/queues/:queue`, `/analytics/queue/:queue`
   - Returns: queue config, partition breakdown, cursors, leases

4. **`GET /api/v1/status/queues/:queue/messages`** - Browse messages
   - Complements: `/messages` endpoint
   - Returns: messages with filtering by status/partition

5. **`GET /api/v1/status/analytics`** - Advanced analytics
   - Replaces: `/analytics/throughput`, `/analytics/queue-lag`, custom aggregations
   - Returns: time-series data, percentiles, top queues, DLQ trends

---

## Changes Required by File

### 1. `/dashboard/src/services/api.js`

**Actions:**
- ✅ Keep existing methods for backward compatibility (messages, push, pop, ack)
- ✅ ADD new methods for status endpoints
- ⚠️ DEPRECATE old resource/analytics methods (or alias to new endpoints)

**New Methods to Add:**
```javascript
// Status API
async getStatus(params = {}) {
  return this.get('/status', params)
}

async getStatusQueues(params = {}) {
  return this.get('/status/queues', params)
}

async getStatusQueueDetail(queueName, params = {}) {
  return this.get(`/status/queues/${encodeURIComponent(queueName)}`, params)
}

async getStatusQueueMessages(queueName, params = {}) {
  return this.get(`/status/queues/${encodeURIComponent(queueName)}/messages`, params)
}

async getStatusAnalytics(params = {}) {
  return this.get('/status/analytics', params)
}
```

**Methods to Deprecate/Replace:**
```javascript
// OLD - Mark as deprecated or remove
async getSystemOverview() { ... }
async getQueues(params = {}) { ... }
async getQueueDetail(queueName) { ... }
async getQueueAnalytics(queueName, params = {}) { ... }
async getAllQueuesAnalytics(params = {}) { ... }
async getQueueDepths() { ... }
async getThroughput() { ... }
async getQueueStats(params = {}) { ... }
async getQueueLag(params = {}) { ... }
```

---

### 2. `/dashboard/src/views/Dashboard.vue`

**Current Issues:**
- Uses multiple separate API calls: `getSystemOverview()`, `getThroughput()`, `getQueueDepths()`, `getQueues()`
- Data processing happens in component (should be in API response)
- Mixes real and mock data

**Changes:**
```javascript
// OLD (lines 405-410)
const [overview, throughput, depths, queues] = await Promise.all([
  api.getSystemOverview(),
  api.getThroughput(filters),
  api.getQueueDepths(filters),
  api.getQueues(filters)
])

// NEW (single unified call)
const status = await api.getStatus(filters)

// Access data:
// status.messages → { total, pending, processing, completed, failed, deadLetter }
// status.throughput → [{ timestamp, ingested, processed, ... }]
// status.queues → [{ id, name, namespace, partitions, totalConsumed }]
// status.leases → { active, partitionsWithLeases }
// status.deadLetterQueue → { totalMessages, affectedPartitions, topErrors }
```

**Specific Changes:**
1. Replace `fetchData()` method (lines 360-448)
2. Simplify `processThroughputData()` - data already formatted
3. Remove mock data generation
4. Update metrics mapping
5. Add DLQ top errors display

**Data Mapping:**
```javascript
// OLD
metrics.value = {
  totalMessages: overview.messages?.total || 0,
  pending: overview.messages?.pending || 0,
  ...
}

// NEW (data already matches!)
metrics.value = status.messages
```

---

### 3. `/dashboard/src/views/Queues.vue`

**Current Issues:**
- Calls `/resources/queues` which returns different format
- Manually calculates completed count
- Missing lag and performance data

**Changes:**
```javascript
// OLD (lines 120-157)
const fetchQueues = async () => {
  const data = await api.getQueues()
  queues.value = data.queues.map(queue => ({
    // manual mapping
  }))
}

// NEW
const fetchQueues = async () => {
  const data = await api.getStatusQueues(filters)
  queues.value = data.queues // Already properly formatted!
  // data.queues contains: id, name, namespace, task, priority, 
  //   partitions, messages: {total, pending, processing, completed, failed, deadLetter},
  //   lag: {seconds, formatted}, performance: {avgProcessingTime, ...}
}
```

**Add to Template:**
- Display lag information (already in API response)
- Display average processing time
- Add time range filter

---

### 4. `/dashboard/src/views/QueueDetail.vue`

**Current Issues:**
- Calls 3 separate endpoints: `getQueueDetail()`, `getQueueAnalytics()`, `getPartitions()`
- Manually processes and combines data
- Missing cursor and lease information

**Changes:**
```javascript
// OLD (lines 143-209)
const [queueData, analyticsData, partitionData] = await Promise.all([
  api.getQueueDetail(queueName.value),
  api.getQueueAnalytics(queueName.value, filters),
  api.getPartitions({ queue: queueName.value })
])
// Then manually combine and process...

// NEW (single call with all data)
const queueDetail = await api.getStatusQueueDetail(queueName.value, filters)

// Access:
// queueDetail.queue → { id, name, namespace, task, priority, config, createdAt }
// queueDetail.totals → { messages: {...}, partitions, consumed, batches }
// queueDetail.partitions → [{ 
//   id, name, messages: {...}, 
//   cursor: { totalConsumed, batchesConsumed, lastConsumedAt },
//   lease: { active, expiresAt, batchSize, ackedCount },
//   lag: { oldestPending, lagSeconds }
// }]
```

**Template Updates:**
- Display cursor information (consumption progress)
- Display lease information (active leases)
- Display per-partition lag
- Show last activity per partition

---

### 5. `/dashboard/src/views/Analytics.vue`

**Current Issues:**
- Complex time-series data processing
- Multiple API calls for different metrics
- Missing percentile data

**Changes:**
```javascript
// OLD
const fetchAnalytics = async () => {
  // Multiple calls to different analytics endpoints
  const throughput = await api.getThroughput(filters)
  const lag = await api.getQueueLag(filters)
  // ... process and combine
}

// NEW (single call)
const fetchAnalytics = async () => {
  const analytics = await api.getStatusAnalytics(filters)
  
  // Data already includes:
  // analytics.throughput → { timeSeries: [...], totals: {...} }
  // analytics.latency → { timeSeries: [...], overall: {p50, p95, p99, avg} }
  // analytics.errorRates → { timeSeries: [...], overall: {...} }
  // analytics.queueDepths → { timeSeries: [...], current: {...} }
  // analytics.topQueues → [...]
  // analytics.deadLetterQueue → { timeSeries: [...], total, topErrors }
}
```

**Add to Template:**
- Latency percentile charts (p50, p95, p99)
- Error rate trends
- Top queues by volume
- DLQ trends with top errors

---

### 6. `/dashboard/src/views/Messages.vue`

**Current Issues:**
- Calls generic `/messages` endpoint
- Could benefit from queue-specific endpoint

**Changes:**
```javascript
// OPTION 1: Keep using /messages endpoint (current)
const messages = await api.getMessages({ queue, status, ...filters })

// OPTION 2: Use new queue-specific endpoint (recommended)
const messages = await api.getStatusQueueMessages(selectedQueue.value, {
  status: selectedStatus.value,
  partition: selectedPartition.value,
  limit: 20,
  offset: currentPage * 20
})

// NEW endpoint provides:
// - Better filtering (status, partition)
// - Queue-scoped results
// - Consistent format with other status endpoints
```

**Add to Template:**
- Partition filter dropdown
- Better status badges
- Age/lag indicators for pending messages

---

## Component Updates

### Chart Components

Most chart components can stay mostly the same, but should expect cleaner data format:

#### `ThroughputChart.vue`
**Before:** Received data with nested structure
**After:** Receives already formatted arrays

```javascript
// Props stay the same, but data is cleaner:
props.data = {
  labels: ['10:00', '10:01', '10:02', ...],
  datasets: [
    { label: 'Ingested', data: [120, 130, 125, ...] },
    { label: 'Processed', data: [118, 128, 123, ...] },
    ...
  ]
}
```

#### `QueueDepthChart.vue`
**No changes needed** - data format stays the same

#### `QueueLagChart.vue`
**May need updates** if we want to show percentile data (p50, p95, p99)

---

## Migration Strategy

### Phase 1: Update API Service (Non-Breaking)
1. Add new status methods to `api.js`
2. Keep old methods but mark as deprecated
3. Test new methods work correctly

### Phase 2: Update Dashboard View
1. Replace API calls in `Dashboard.vue`
2. Simplify data processing
3. Remove mock data
4. Test thoroughly

### Phase 3: Update Queues Views
1. Update `Queues.vue` to use new endpoint
2. Update `QueueDetail.vue` to use new endpoint
3. Add new features (lag, cursors, leases)

### Phase 4: Update Analytics & Messages
1. Update `Analytics.vue` with new endpoint
2. Update `Messages.vue` to use queue-specific endpoint
3. Add new charts (percentiles, error rates)

### Phase 5: Cleanup
1. Remove deprecated API methods
2. Remove old endpoint calls
3. Update documentation

---

## Data Format Changes

### OLD Format (inconsistent across endpoints)

**System Overview:**
```json
{
  "queues": 5,
  "partitions": 12,
  "messages": {
    "total": 1000,
    "pending": 50,
    ...
  }
}
```

**Queues:**
```json
{
  "queues": [{
    "name": "queue1",
    "namespace": "ns1",
    "messages": { "total": 100, "pending": 10 }
  }]
}
```

### NEW Format (consistent)

**Status:**
```json
{
  "timeRange": { "from": "...", "to": "..." },
  "throughput": [{ "timestamp": "...", "ingested": 120, "processed": 118 }],
  "queues": [{ "id": "...", "name": "...", "namespace": "...", "partitions": 3 }],
  "messages": { "total": 1000, "pending": 50, "processing": 10, "completed": 900, "failed": 20, "deadLetter": 20 },
  "leases": { "active": 5, "partitionsWithLeases": 5 },
  "deadLetterQueue": { "totalMessages": 20, "affectedPartitions": 3, "topErrors": [...] }
}
```

---

## Benefits of New API

1. **Fewer API Calls**
   - Dashboard: 4 calls → 1 call
   - Queue Detail: 3 calls → 1 call
   - Analytics: Multiple calls → 1 call

2. **Consistent Data Format**
   - All endpoints return same structure
   - Time ranges handled server-side
   - No need for client-side aggregation

3. **Better Performance**
   - Server-side aggregation
   - Optimized SQL queries
   - Less data transfer

4. **More Features**
   - Partition cursors (consumption tracking)
   - Active leases information
   - Latency percentiles
   - DLQ error analysis
   - Top errors by occurrence

5. **Easier Development**
   - Less data processing in components
   - Cleaner component code
   - Better type safety
   - Easier testing

---

## Testing Checklist

### For Each View:
- [ ] Data loads correctly from new endpoint
- [ ] Filters work (time range, queue, namespace, task)
- [ ] Charts render correctly
- [ ] Real-time updates work (WebSocket)
- [ ] Loading states work
- [ ] Error handling works
- [ ] No console errors
- [ ] UI matches design

### Specific Tests:
- [ ] Dashboard shows correct metrics
- [ ] Throughput chart displays time-series data
- [ ] Queue list shows all queues with stats
- [ ] Queue detail shows partition breakdown
- [ ] Cursor and lease information displays
- [ ] Analytics shows percentiles
- [ ] Messages browser filters work
- [ ] DLQ errors display correctly

---

## Files to Modify Summary

1. ✅ **`services/api.js`** - Add new methods
2. ✅ **`views/Dashboard.vue`** - Replace API calls, simplify logic
3. ✅ **`views/Queues.vue`** - Use new endpoint, add features
4. ✅ **`views/QueueDetail.vue`** - Use new endpoint, add cursor/lease info
5. ✅ **`views/Analytics.vue`** - Use new endpoint, add percentile charts
6. ✅ **`views/Messages.vue`** - Optionally use queue-specific endpoint
7. ⚠️ **`components/charts/QueueLagChart.vue`** - May need updates for percentiles
8. ⚠️ **`utils/helpers.js`** - May need new formatting functions

---

## Backward Compatibility

**Option 1: Keep Old Endpoints (Recommended for gradual migration)**
- Keep old API methods
- Add deprecation warnings
- Migrate views one at a time
- Remove old methods after all views updated

**Option 2: Break Immediately**
- Replace all API methods
- Update all views at once
- Higher risk but faster cleanup

**Recommendation:** Use Option 1 for safer migration.

---

## Implementation Order

1. **Day 1:** Update `api.js` with new methods (non-breaking)
2. **Day 2:** Update `Dashboard.vue` (highest impact)
3. **Day 3:** Update `Queues.vue` and `QueueDetail.vue`
4. **Day 4:** Update `Analytics.vue` and `Messages.vue`
5. **Day 5:** Test everything, fix bugs, cleanup

---

## Next Steps

1. Review this plan
2. Decide on migration strategy (gradual vs. immediate)
3. Start with `api.js` updates
4. Update views one by one
5. Test thoroughly
6. Deploy to production

---

**Total Estimated Effort:** 2-3 days of focused work
**Risk Level:** Medium (if gradual) / High (if immediate)
**Impact:** High - Much cleaner, faster, more maintainable dashboard

