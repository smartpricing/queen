<template>
  <div class="dashboard">
    <!-- Page Header - Compact -->
    <div class="page-header">
      <div>
        <h1 class="page-title">Dashboard</h1>
        <p class="page-subtitle">Real-time queue monitoring</p>
      </div>
      <div class="header-actions">
        <select class="select-input" v-model="timeRange">
          <option value="1h">Last Hour</option>
          <option value="6h">Last 6 Hours</option>
          <option value="24h">Last 24 Hours</option>
          <option value="7d">Last 7 Days</option>
        </select>
        <button class="btn btn-primary btn-sm" @click="refreshData">
          <i class="pi pi-refresh" :class="{ 'pi-spin': loading }"></i>
          Refresh
        </button>
      </div>
    </div>

    <!-- Metrics Grid - Compact Cards -->
    <div class="metrics-grid">
      <div class="metric-card">
        <div class="metric-header">
          <span class="metric-label">Total Messages</span>
          <i class="pi pi-inbox metric-icon"></i>
        </div>
        <div class="metric-value">{{ formatNumber(stats.total) }}</div>
        <div class="metric-change">
          <span class="change-value positive">+12%</span>
          <span class="change-label">from last period</span>
        </div>
      </div>

      <div class="metric-card">
        <div class="metric-header">
          <span class="metric-label">Pending</span>
          <i class="pi pi-clock metric-icon"></i>
        </div>
        <div class="metric-value">{{ formatNumber(stats.pending) }}</div>
        <div class="metric-footer">
          <div class="mini-progress">
            <div class="progress-fill" :style="{ width: pendingPercent + '%' }"></div>
          </div>
        </div>
      </div>

      <div class="metric-card">
        <div class="metric-header">
          <span class="metric-label">Processing</span>
          <i class="pi pi-spin pi-spinner metric-icon"></i>
        </div>
        <div class="metric-value">{{ formatNumber(stats.processing) }}</div>
        <div class="metric-change">
          <span class="change-value neutral">0</span>
          <span class="change-label">no change</span>
        </div>
      </div>

      <div class="metric-card">
        <div class="metric-header">
          <span class="metric-label">Completed</span>
          <i class="pi pi-check-circle metric-icon"></i>
        </div>
        <div class="metric-value">{{ formatNumber(stats.completed) }}</div>
        <div class="metric-change">
          <span class="change-value positive">+23%</span>
          <span class="change-label">from last period</span>
        </div>
      </div>
    </div>

    <!-- Charts Row -->
    <div class="charts-row">
      <!-- Throughput Chart -->
      <div class="chart-card">
        <div class="card-header">
          <h3 class="card-title">Throughput</h3>
          <div class="chart-tabs">
            <button 
              v-for="range in ['1h', '6h', '24h']" 
              :key="range"
              class="tab-btn"
              :class="{ active: chartRange === range }"
              @click="chartRange = range"
            >
              {{ range }}
            </button>
          </div>
        </div>
        <div class="chart-container">
          <MultiMetricThroughputChart :data="throughputData" :showLag="false" />
        </div>
      </div>

      <!-- Queue Status -->
      <div class="status-card">
        <div class="card-header">
          <h3 class="card-title">Queue Status</h3>
        </div>
        <div class="status-list">
          <div class="status-item">
            <span class="status-label">Active Queues</span>
            <span class="status-value">{{ activeQueues }}</span>
          </div>
          <div class="status-item">
            <span class="status-label">Success Rate</span>
            <span class="status-value">{{ successRate }}%</span>
          </div>
          <div class="status-item">
            <span class="status-label">Avg Processing</span>
            <span class="status-value">{{ avgProcessingTime }}ms</span>
          </div>
          <div class="status-item">
            <span class="status-label">Failed Today</span>
            <span class="status-value text-danger">{{ failedToday }}</span>
          </div>
        </div>
      </div>
    </div>

    <!-- Queues Table -->
    <div class="table-card">
      <div class="card-header">
        <h3 class="card-title">Active Queues</h3>
        <router-link to="/queues" class="link-btn">
          View all <i class="pi pi-arrow-right"></i>
        </router-link>
      </div>
      
      <table class="data-table">
        <thead>
          <tr>
            <th>Queue</th>
            <th>Priority</th>
            <th>Pending</th>
            <th>Processing</th>
            <th>Completed</th>
            <th>Success Rate</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="queue in topQueues" :key="queue.queue" class="table-row">
            <td>
              <router-link :to="`/queues/${encodeURIComponent(queue.queue)}`" class="queue-link">
                {{ queue.queue }}
              </router-link>
            </td>
            <td>
              <span class="priority-badge" :class="`priority-${getPriorityLevel(queue.priority)}`">
                {{ queue.priority || 0 }}
              </span>
            </td>
            <td class="text-number">{{ formatNumber(queue.stats?.pending || 0) }}</td>
            <td class="text-number">{{ formatNumber(queue.stats?.processing || 0) }}</td>
            <td class="text-number">{{ formatNumber(queue.stats?.completed || 0) }}</td>
            <td>
              <div class="success-rate">
                <div class="rate-bar">
                  <div class="rate-fill" :style="{ width: getSuccessRate(queue) + '%' }"></div>
                </div>
                <span class="rate-text">{{ getSuccessRate(queue) }}%</span>
              </div>
            </td>
            <td>
              <button class="icon-btn" v-tooltip="'View details'">
                <i class="pi pi-eye"></i>
              </button>
            </td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- Activity Feed -->
    <div class="activity-card">
      <div class="card-header">
        <h3 class="card-title">
          <span class="live-dot"></span>
          Recent Activity
        </h3>
        <router-link to="/activity" class="link-btn">
          View all <i class="pi pi-arrow-right"></i>
        </router-link>
      </div>
      
      <div class="activity-list">
        <div v-for="activity in recentActivities" :key="activity.id" class="activity-item">
          <div class="activity-icon" :class="`type-${activity.type}`">
            <i :class="getActivityIcon(activity.type)"></i>
          </div>
          <div class="activity-content">
            <p class="activity-text">{{ activity.description }}</p>
            <span class="activity-time">{{ formatTime(activity.timestamp) }}</span>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useQueuesStore } from '../stores/queues'
import { useMessagesStore } from '../stores/messages'
import MultiMetricThroughputChart from '../components/charts/MultiMetricThroughputChart.vue'
import { api } from '../utils/api'
import { formatDistanceToNow } from 'date-fns'

const queuesStore = useQueuesStore()
const messagesStore = useMessagesStore()

// State
const loading = ref(false)
const timeRange = ref('24h')
const chartRange = ref('24h')
const throughputData = ref([])
let refreshInterval = null

// Computed
const stats = computed(() => queuesStore.globalStats)

const topQueues = computed(() => {
  return [...queuesStore.queues]
    .sort((a, b) => (b.stats?.pending || 0) - (a.stats?.pending || 0))
    .slice(0, 5)
})

const recentActivities = computed(() => {
  return messagesStore.recentActivity.slice(0, 5).map(activity => ({
    ...activity,
    description: getActivityDescription(activity)
  }))
})

const pendingPercent = computed(() => {
  const total = stats.value.total || 1
  return Math.round((stats.value.pending / total) * 100)
})

const activeQueues = computed(() => {
  return queuesStore.queues.filter(q => 
    q.stats && (q.stats.pending > 0 || q.stats.processing > 0)
  ).length
})

const successRate = computed(() => {
  const total = stats.value.total || 1
  const completed = stats.value.completed || 0
  return Math.round((completed / total) * 100)
})

const avgProcessingTime = ref(0)
const failedToday = ref(0)

// Methods
async function refreshData() {
  loading.value = true
  try {
    await Promise.all([
      queuesStore.fetchQueues(),
      fetchThroughput()
    ])
  } catch (error) {
    console.error('Failed to refresh:', error)
  } finally {
    loading.value = false
  }
}

async function fetchThroughput() {
  try {
    const response = await api.analytics.getThroughput()
    throughputData.value = response.throughput || []
  } catch (error) {
    console.error('Failed to fetch throughput:', error)
  }
}

function formatNumber(num) {
  if (!num) return '0'
  if (num >= 1000000) return `${(num / 1000000).toFixed(1)}M`
  if (num >= 1000) return `${(num / 1000).toFixed(1)}K`
  return num.toLocaleString()
}

function formatTime(timestamp) {
  return formatDistanceToNow(new Date(timestamp), { addSuffix: true })
}

function getPriorityLevel(priority) {
  if (priority >= 10) return 'high'
  if (priority >= 5) return 'medium'
  return 'low'
}

function getSuccessRate(queue) {
  const total = queue.stats?.total || 0
  const completed = queue.stats?.completed || 0
  return total > 0 ? Math.round((completed / total) * 100) : 0
}

function getActivityIcon(type) {
  const icons = {
    push: 'pi pi-upload',
    pop: 'pi pi-download',
    ack: 'pi pi-check',
    completed: 'pi pi-check-circle',
    failed: 'pi pi-times-circle'
  }
  return icons[type] || 'pi pi-circle'
}

function getActivityDescription(activity) {
  if (activity.type === 'push') return `Pushed ${activity.count || 1} messages`
  if (activity.type === 'pop') return `Popped from ${activity.queue}`
  if (activity.type === 'ack') return `Acknowledged ${activity.status}`
  return activity.type
}

// Lifecycle
onMounted(() => {
  refreshData()
  refreshInterval = setInterval(refreshData, 30000)
})

onUnmounted(() => {
  if (refreshInterval) clearInterval(refreshInterval)
})
</script>

<style scoped>
.dashboard {
  max-width: 1400px;
  margin: 0 auto;
}

/* Page Header */
.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: var(--space-6);
}

.page-title {
  font-size: var(--text-2xl);
  font-weight: 600;
  color: var(--text-primary);
  margin: 0;
}

.page-subtitle {
  font-size: var(--text-sm);
  color: var(--text-tertiary);
  margin-top: var(--space-1);
}

.header-actions {
  display: flex;
  gap: var(--space-3);
  align-items: center;
}

.select-input {
  padding: var(--space-2) var(--space-3);
  background: var(--bg-tertiary);
  border: 1px solid var(--border-default);
  border-radius: var(--radius-md);
  color: var(--text-primary);
  font-size: var(--text-sm);
  cursor: pointer;
}

/* Metrics Grid */
.metrics-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: var(--space-4);
  margin-bottom: var(--space-6);
}

.metric-card {
  background: var(--bg-secondary);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-lg);
  padding: var(--space-4);
}

.metric-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: var(--space-3);
}

.metric-label {
  font-size: var(--text-xs);
  font-weight: 500;
  color: var(--text-tertiary);
  text-transform: uppercase;
  letter-spacing: 0.05em;
}

.metric-icon {
  font-size: var(--text-sm);
  color: var(--text-tertiary);
}

.metric-value {
  font-size: var(--text-2xl);
  font-weight: 600;
  color: var(--text-primary);
  margin-bottom: var(--space-2);
}

.metric-change {
  display: flex;
  align-items: center;
  gap: var(--space-2);
  font-size: var(--text-xs);
}

.change-value {
  font-weight: 500;
}

.change-value.positive { color: var(--success); }
.change-value.negative { color: var(--danger); }
.change-value.neutral { color: var(--text-tertiary); }

.change-label {
  color: var(--text-tertiary);
}

.mini-progress {
  height: 2px;
  background: var(--bg-tertiary);
  border-radius: 1px;
  overflow: hidden;
  margin-top: var(--space-2);
}

.progress-fill {
  height: 100%;
  background: var(--primary);
  transition: width var(--transition);
}

/* Charts Row */
.charts-row {
  display: grid;
  grid-template-columns: 2fr 1fr;
  gap: var(--space-4);
  margin-bottom: var(--space-6);
}

.chart-card,
.status-card {
  background: var(--bg-secondary);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-lg);
  padding: var(--space-4);
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: var(--space-4);
}

.card-title {
  font-size: var(--text-lg);
  font-weight: 600;
  color: var(--text-primary);
  margin: 0;
}

.chart-tabs {
  display: flex;
  gap: var(--space-1);
}

.tab-btn {
  padding: var(--space-1) var(--space-3);
  background: transparent;
  border: none;
  color: var(--text-tertiary);
  font-size: var(--text-xs);
  font-weight: 500;
  cursor: pointer;
  border-radius: var(--radius-sm);
  transition: all var(--transition);
}

.tab-btn:hover {
  background: var(--bg-tertiary);
  color: var(--text-secondary);
}

.tab-btn.active {
  background: var(--primary-soft);
  color: var(--primary);
}

.chart-container {
  height: 200px;
}

/* Status List */
.status-list {
  display: flex;
  flex-direction: column;
  gap: var(--space-3);
}

.status-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: var(--space-2) 0;
  border-bottom: 1px solid var(--border-subtle);
}

.status-item:last-child {
  border-bottom: none;
}

.status-label {
  font-size: var(--text-sm);
  color: var(--text-secondary);
}

.status-value {
  font-size: var(--text-sm);
  font-weight: 600;
  color: var(--text-primary);
}

.text-danger {
  color: var(--danger);
}

/* Table */
.table-card {
  background: var(--bg-secondary);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-lg);
  padding: var(--space-4);
  margin-bottom: var(--space-6);
}

.data-table {
  width: 100%;
  border-collapse: separate;
  border-spacing: 0;
}

.data-table th {
  padding: var(--space-2) var(--space-3);
  text-align: left;
  font-size: var(--text-xs);
  font-weight: 500;
  color: var(--text-tertiary);
  text-transform: uppercase;
  letter-spacing: 0.05em;
  border-bottom: 1px solid var(--border-subtle);
}

.data-table td {
  padding: var(--space-3);
  font-size: var(--text-sm);
  color: var(--text-secondary);
  border-bottom: 1px solid var(--border-subtle);
}

.table-row:hover {
  background: var(--bg-tertiary);
}

.queue-link {
  color: var(--primary);
  text-decoration: none;
  font-weight: 500;
}

.queue-link:hover {
  text-decoration: underline;
}

.priority-badge {
  display: inline-block;
  padding: var(--space-1) var(--space-2);
  font-size: var(--text-xs);
  font-weight: 500;
  border-radius: var(--radius-sm);
}

.priority-high {
  background: var(--danger-soft);
  color: var(--danger);
}

.priority-medium {
  background: var(--warning-soft);
  color: var(--warning);
}

.priority-low {
  background: var(--info-soft);
  color: var(--info);
}

.text-number {
  font-family: var(--font-mono);
}

.success-rate {
  display: flex;
  align-items: center;
  gap: var(--space-2);
}

.rate-bar {
  width: 60px;
  height: 4px;
  background: var(--bg-tertiary);
  border-radius: 2px;
  overflow: hidden;
}

.rate-fill {
  height: 100%;
  background: var(--success);
}

.rate-text {
  font-size: var(--text-xs);
  font-weight: 500;
}

.icon-btn {
  width: 28px;
  height: 28px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: transparent;
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-sm);
  color: var(--text-tertiary);
  cursor: pointer;
  transition: all var(--transition);
}

.icon-btn:hover {
  background: var(--bg-tertiary);
  border-color: var(--border-default);
  color: var(--text-secondary);
}

/* Activity */
.activity-card {
  background: var(--bg-secondary);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-lg);
  padding: var(--space-4);
}

.live-dot {
  display: inline-block;
  width: 6px;
  height: 6px;
  background: var(--success);
  border-radius: 50%;
  margin-right: var(--space-2);
  animation: pulse 2s infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.5; }
}

.activity-list {
  display: flex;
  flex-direction: column;
  gap: var(--space-3);
}

.activity-item {
  display: flex;
  gap: var(--space-3);
}

.activity-icon {
  width: 32px;
  height: 32px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: var(--radius-md);
  font-size: var(--text-sm);
}

.activity-icon.type-push {
  background: var(--info-soft);
  color: var(--info);
}

.activity-icon.type-pop {
  background: var(--success-soft);
  color: var(--success);
}

.activity-icon.type-failed {
  background: var(--danger-soft);
  color: var(--danger);
}

.activity-content {
  flex: 1;
}

.activity-text {
  font-size: var(--text-sm);
  color: var(--text-primary);
  margin: 0 0 var(--space-1);
}

.activity-time {
  font-size: var(--text-xs);
  color: var(--text-tertiary);
}

.link-btn {
  display: inline-flex;
  align-items: center;
  gap: var(--space-1);
  color: var(--primary);
  text-decoration: none;
  font-size: var(--text-sm);
  font-weight: 500;
  transition: color var(--transition);
}

.link-btn:hover {
  color: var(--primary-hover);
}

.link-btn i {
  font-size: var(--text-xs);
}

/* Responsive */
@media (max-width: 1024px) {
  .charts-row {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 768px) {
  .page-header {
    flex-direction: column;
    align-items: flex-start;
    gap: var(--space-3);
  }
  
  .metrics-grid {
    grid-template-columns: 1fr;
  }
}
</style>
