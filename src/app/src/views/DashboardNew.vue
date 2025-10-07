<template>
  <div class="dashboard animate-fade-in">
    <!-- Page Header with Gradient -->
    <div class="dashboard-header">
      <div class="header-content">
        <h1 class="page-title glow-text">Dashboard Overview</h1>
        <p class="page-subtitle">Real-time message queue monitoring and analytics</p>
      </div>
      <div class="header-actions">
        <button class="action-btn" @click="refreshData">
          <i class="pi pi-refresh" :class="{ 'pi-spin': loading }"></i>
          <span>Refresh</span>
        </button>
        <Dropdown 
          v-model="selectedTimeRange" 
          :options="timeRanges" 
          optionLabel="label"
          optionValue="value"
          class="time-dropdown"
        />
      </div>
    </div>

    <!-- Key Metrics Cards with Glass Effect -->
    <div class="metrics-grid">
      <div class="metric-card glass-card animate-slide-up" style="animation-delay: 0.1s">
        <div class="metric-icon" style="background: linear-gradient(135deg, #667eea 0%, #764ba2 100%)">
          <i class="pi pi-inbox"></i>
        </div>
        <div class="metric-content">
          <span class="metric-label">Total Messages</span>
          <div class="metric-value">
            <CountUp :end="stats.total" :duration="1.5" />
          </div>
          <div class="metric-trend positive">
            <i class="pi pi-arrow-up"></i>
            <span>12% from last hour</span>
          </div>
        </div>
        <div class="metric-sparkline">
          <svg viewBox="0 0 100 40" class="sparkline">
            <polyline
              points="0,35 10,30 20,32 30,25 40,20 50,22 60,15 70,18 80,10 90,12 100,5"
              fill="none"
              stroke="url(#gradient-purple)"
              stroke-width="2"
            />
            <defs>
              <linearGradient id="gradient-purple" x1="0%" y1="0%" x2="100%" y2="0%">
                <stop offset="0%" style="stop-color:#667eea;stop-opacity:0.8" />
                <stop offset="100%" style="stop-color:#764ba2;stop-opacity:1" />
              </linearGradient>
            </defs>
          </svg>
        </div>
      </div>

      <div class="metric-card glass-card animate-slide-up" style="animation-delay: 0.2s">
        <div class="metric-icon" style="background: linear-gradient(135deg, #4facfe 0%, #00f2fe 100%)">
          <i class="pi pi-clock"></i>
        </div>
        <div class="metric-content">
          <span class="metric-label">Pending</span>
          <div class="metric-value">
            <CountUp :end="stats.pending" :duration="1.5" />
          </div>
          <div class="metric-trend neutral">
            <i class="pi pi-minus"></i>
            <span>No change</span>
          </div>
        </div>
        <div class="metric-progress">
          <div class="progress-bar">
            <div class="progress-fill" :style="{ width: pendingPercentage + '%' }"></div>
          </div>
        </div>
      </div>

      <div class="metric-card glass-card animate-slide-up" style="animation-delay: 0.3s">
        <div class="metric-icon" style="background: linear-gradient(135deg, #fa709a 0%, #fee140 100%)">
          <i class="pi pi-spin pi-spinner"></i>
        </div>
        <div class="metric-content">
          <span class="metric-label">Processing</span>
          <div class="metric-value">
            <CountUp :end="stats.processing" :duration="1.5" />
          </div>
          <div class="metric-trend negative">
            <i class="pi pi-arrow-down"></i>
            <span>5% from last hour</span>
          </div>
        </div>
        <div class="metric-activity">
          <div class="activity-dots">
            <span class="dot" style="animation-delay: 0s"></span>
            <span class="dot" style="animation-delay: 0.2s"></span>
            <span class="dot" style="animation-delay: 0.4s"></span>
          </div>
        </div>
      </div>

      <div class="metric-card glass-card animate-slide-up" style="animation-delay: 0.4s">
        <div class="metric-icon" style="background: linear-gradient(135deg, #30cfd0 0%, #330867 100%)">
          <i class="pi pi-check-circle"></i>
        </div>
        <div class="metric-content">
          <span class="metric-label">Completed</span>
          <div class="metric-value">
            <CountUp :end="stats.completed" :duration="1.5" />
          </div>
          <div class="metric-trend positive">
            <i class="pi pi-arrow-up"></i>
            <span>23% from last hour</span>
          </div>
        </div>
        <div class="metric-chart">
          <div class="mini-donut">
            <svg viewBox="0 0 42 42" class="donut">
              <circle cx="21" cy="21" r="16" fill="none" stroke="rgba(255,255,255,0.1)" stroke-width="3"></circle>
              <circle cx="21" cy="21" r="16" fill="none" stroke="url(#gradient-green)" stroke-width="3"
                      :stroke-dasharray="`${completedPercentage} ${100 - completedPercentage}`"
                      stroke-dashoffset="25"
                      stroke-linecap="round"></circle>
              <defs>
                <linearGradient id="gradient-green" x1="0%" y1="0%" x2="100%" y2="100%">
                  <stop offset="0%" style="stop-color:#30cfd0;stop-opacity:1" />
                  <stop offset="100%" style="stop-color:#330867;stop-opacity:1" />
                </linearGradient>
              </defs>
            </svg>
            <span class="donut-text">{{ Math.round(completedPercentage) }}%</span>
          </div>
        </div>
      </div>
    </div>

    <!-- Charts Section -->
    <div class="charts-section">
      <!-- Throughput Chart with Glass Effect -->
      <div class="chart-container glass-card animate-slide-up" style="animation-delay: 0.5s">
        <div class="chart-header">
          <h3 class="chart-title">Message Throughput</h3>
          <div class="chart-controls">
            <ButtonGroup>
              <Button label="1H" :class="{ active: chartRange === '1h' }" @click="chartRange = '1h'" />
              <Button label="6H" :class="{ active: chartRange === '6h' }" @click="chartRange = '6h'" />
              <Button label="24H" :class="{ active: chartRange === '24h' }" @click="chartRange = '24h'" />
              <Button label="7D" :class="{ active: chartRange === '7d' }" @click="chartRange = '7d'" />
            </ButtonGroup>
          </div>
        </div>
        <div class="chart-body">
          <MultiMetricThroughputChart :data="throughputData" :showLag="false" />
        </div>
      </div>

      <!-- Queue Distribution -->
      <div class="chart-container glass-card animate-slide-up" style="animation-delay: 0.6s">
        <div class="chart-header">
          <h3 class="chart-title">Queue Distribution</h3>
          <button class="icon-btn">
            <i class="pi pi-ellipsis-v"></i>
          </button>
        </div>
        <div class="chart-body">
          <QueueDepthChart :data="queueDepthData" />
        </div>
      </div>
    </div>

    <!-- Active Queues Table with Modern Design -->
    <div class="table-section glass-card animate-slide-up" style="animation-delay: 0.7s">
      <div class="section-header">
        <h3 class="section-title">Active Queues</h3>
        <div class="section-actions">
          <span class="queue-count">{{ topQueues.length }} queues</span>
          <router-link to="/queues" class="view-all-link">
            View all <i class="pi pi-arrow-right"></i>
          </router-link>
        </div>
      </div>
      
      <div class="modern-table">
        <div class="table-header">
          <div class="table-cell">Queue Name</div>
          <div class="table-cell">Priority</div>
          <div class="table-cell">Status</div>
          <div class="table-cell">Messages</div>
          <div class="table-cell">Performance</div>
          <div class="table-cell">Actions</div>
        </div>
        
        <TransitionGroup name="table-row">
          <div v-for="queue in topQueues" :key="queue.queue" class="table-row">
            <div class="table-cell">
              <div class="queue-name">
                <span class="queue-icon">
                  <i class="pi pi-folder"></i>
                </span>
                <router-link :to="`/queues/${encodeURIComponent(queue.queue)}`" class="queue-link">
                  {{ queue.queue }}
                </router-link>
              </div>
            </div>
            <div class="table-cell">
              <span class="priority-badge" :class="`priority-${getPriorityLevel(queue.priority)}`">
                <i class="pi pi-flag-fill"></i>
                {{ queue.priority || 0 }}
              </span>
            </div>
            <div class="table-cell">
              <div class="status-indicator">
                <span class="status-dot" :class="queue.stats?.processing > 0 ? 'active' : 'idle'"></span>
                <span>{{ queue.stats?.processing > 0 ? 'Active' : 'Idle' }}</span>
              </div>
            </div>
            <div class="table-cell">
              <div class="message-stats">
                <div class="stat-item">
                  <span class="stat-value">{{ formatNumber(queue.stats?.pending || 0) }}</span>
                  <span class="stat-label">pending</span>
                </div>
                <div class="stat-divider"></div>
                <div class="stat-item">
                  <span class="stat-value">{{ formatNumber(queue.stats?.completed || 0) }}</span>
                  <span class="stat-label">completed</span>
                </div>
              </div>
            </div>
            <div class="table-cell">
              <div class="performance-bar">
                <div class="performance-fill" :style="{ width: getSuccessRate(queue) + '%' }"></div>
                <span class="performance-text">{{ getSuccessRate(queue) }}%</span>
              </div>
            </div>
            <div class="table-cell">
              <div class="action-buttons">
                <button class="action-icon" v-tooltip="'View Details'">
                  <i class="pi pi-eye"></i>
                </button>
                <button class="action-icon" v-tooltip="'Configure'">
                  <i class="pi pi-cog"></i>
                </button>
                <button class="action-icon danger" v-tooltip="'Clear Queue'">
                  <i class="pi pi-trash"></i>
                </button>
              </div>
            </div>
          </div>
        </TransitionGroup>
      </div>
    </div>

    <!-- Real-time Activity Feed -->
    <div class="activity-section glass-card animate-slide-up" style="animation-delay: 0.8s">
      <div class="section-header">
        <h3 class="section-title">
          <span class="live-indicator">
            <span class="live-dot"></span>
            Live Activity
          </span>
        </h3>
        <router-link to="/activity" class="view-all-link">
          View all <i class="pi pi-arrow-right"></i>
        </router-link>
      </div>
      
      <ActivityFeed :activities="recentActivities" :compact="true" />
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import Dropdown from 'primevue/dropdown'
import Button from 'primevue/button'
import ButtonGroup from 'primevue/buttongroup'
import CountUp from '../components/common/CountUp.vue'
import MultiMetricThroughputChart from '../components/charts/MultiMetricThroughputChart.vue'
import QueueDepthChart from '../components/charts/QueueDepthChart.vue'
import ActivityFeed from '../components/dashboard/ActivityFeed.vue'
import { useQueuesStore } from '../stores/queues'
import { useMessagesStore } from '../stores/messages'
import { api } from '../utils/api'

const router = useRouter()
const queuesStore = useQueuesStore()
const messagesStore = useMessagesStore()

// State
const loading = ref(false)
const selectedTimeRange = ref('1h')
const chartRange = ref('24h')
const throughputData = ref([])
const queueDepthData = ref([])
let refreshInterval = null

// Time range options
const timeRanges = [
  { label: 'Last Hour', value: '1h' },
  { label: 'Last 6 Hours', value: '6h' },
  { label: 'Last 24 Hours', value: '24h' },
  { label: 'Last 7 Days', value: '7d' }
]

// Computed
const stats = computed(() => queuesStore.globalStats)

const topQueues = computed(() => {
  return [...queuesStore.queues]
    .sort((a, b) => (b.stats?.pending || 0) - (a.stats?.pending || 0))
    .slice(0, 5)
})

const recentActivities = computed(() => messagesStore.recentActivity.slice(0, 5))

const pendingPercentage = computed(() => {
  const total = stats.value.total || 1
  return Math.round((stats.value.pending / total) * 100)
})

const completedPercentage = computed(() => {
  const total = stats.value.total || 1
  return Math.round((stats.value.completed / total) * 100)
})

// Methods
async function refreshData() {
  loading.value = true
  try {
    await Promise.all([
      queuesStore.fetchQueues(),
      fetchThroughput(),
      fetchQueueDepths()
    ])
  } catch (error) {
    console.error('Failed to refresh dashboard:', error)
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

async function fetchQueueDepths() {
  try {
    const response = await api.analytics.getQueueDepths()
    queueDepthData.value = response.depths || []
  } catch (error) {
    console.error('Failed to fetch queue depths:', error)
  }
}

function formatNumber(num) {
  if (num >= 1000000) return `${(num / 1000000).toFixed(1)}M`
  if (num >= 1000) return `${(num / 1000).toFixed(1)}K`
  return num.toString()
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

// Lifecycle
onMounted(() => {
  refreshData()
  // Auto-refresh every 30 seconds
  refreshInterval = setInterval(refreshData, 30000)
})

onUnmounted(() => {
  if (refreshInterval) {
    clearInterval(refreshInterval)
  }
})
</script>

<style scoped>
.dashboard {
  padding: 0;
  max-width: 1600px;
  margin: 0 auto;
}

/* Dashboard Header */
.dashboard-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 2rem;
  padding: 2rem;
  background: linear-gradient(135deg, rgba(168, 85, 247, 0.1) 0%, rgba(168, 85, 247, 0.05) 100%);
  border-radius: var(--radius-2xl);
  border: 1px solid var(--glass-border);
  backdrop-filter: blur(10px);
}

.header-content {
  flex: 1;
}

.page-title {
  font-size: 2.5rem;
  font-weight: 800;
  margin: 0;
  background: linear-gradient(135deg, var(--primary-400) 0%, var(--primary-600) 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}

.page-subtitle {
  color: var(--neutral-400);
  margin-top: 0.5rem;
  font-size: 1rem;
}

.header-actions {
  display: flex;
  gap: 1rem;
  align-items: center;
}

.action-btn {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.75rem 1.25rem;
  background: var(--glass-bg);
  border: 1px solid var(--glass-border);
  border-radius: var(--radius-lg);
  color: white;
  font-weight: 500;
  cursor: pointer;
  transition: all var(--transition-base);
}

.action-btn:hover {
  background: rgba(168, 85, 247, 0.1);
  border-color: rgba(168, 85, 247, 0.3);
  transform: translateY(-2px);
}

/* Metrics Grid */
.metrics-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
  gap: 1.5rem;
  margin-bottom: 2rem;
}

.metric-card {
  padding: 1.5rem;
  position: relative;
  overflow: hidden;
  display: flex;
  gap: 1rem;
}

.metric-icon {
  width: 56px;
  height: 56px;
  border-radius: var(--radius-lg);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 1.5rem;
  color: white;
  flex-shrink: 0;
}

.metric-content {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.metric-label {
  font-size: 0.875rem;
  color: var(--neutral-400);
  font-weight: 500;
}

.metric-value {
  font-size: 2rem;
  font-weight: 700;
  color: white;
}

.metric-trend {
  display: flex;
  align-items: center;
  gap: 0.25rem;
  font-size: 0.75rem;
}

.metric-trend.positive {
  color: var(--success);
}

.metric-trend.negative {
  color: var(--danger);
}

.metric-trend.neutral {
  color: var(--neutral-400);
}

.metric-sparkline {
  position: absolute;
  bottom: 0;
  right: 0;
  width: 100px;
  height: 40px;
  opacity: 0.3;
}

.metric-progress {
  position: absolute;
  bottom: 0;
  left: 0;
  right: 0;
  height: 3px;
  background: rgba(255, 255, 255, 0.1);
}

.progress-fill {
  height: 100%;
  background: linear-gradient(90deg, var(--accent-blue), var(--accent-cyan));
  transition: width var(--transition-base);
}

.metric-activity {
  position: absolute;
  top: 1.5rem;
  right: 1.5rem;
}

.activity-dots {
  display: flex;
  gap: 0.25rem;
}

.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--warning);
  animation: pulse 1.5s ease-in-out infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 0.3; transform: scale(0.8); }
  50% { opacity: 1; transform: scale(1.2); }
}

.mini-donut {
  position: absolute;
  top: 1rem;
  right: 1rem;
  width: 42px;
  height: 42px;
}

.donut {
  transform: rotate(-90deg);
  width: 100%;
  height: 100%;
}

.donut-text {
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  font-size: 0.625rem;
  font-weight: 600;
  color: white;
}

/* Charts Section */
.charts-section {
  display: grid;
  grid-template-columns: 2fr 1fr;
  gap: 1.5rem;
  margin-bottom: 2rem;
}

.chart-container {
  padding: 1.5rem;
}

.chart-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1.5rem;
}

.chart-title {
  font-size: 1.125rem;
  font-weight: 600;
  color: white;
  margin: 0;
}

.chart-controls {
  display: flex;
  gap: 0.5rem;
}

.chart-body {
  height: 300px;
}

/* Modern Table */
.table-section {
  padding: 1.5rem;
  margin-bottom: 2rem;
}

.section-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1.5rem;
}

.section-title {
  font-size: 1.125rem;
  font-weight: 600;
  color: white;
  margin: 0;
}

.section-actions {
  display: flex;
  align-items: center;
  gap: 1.5rem;
}

.queue-count {
  color: var(--neutral-400);
  font-size: 0.875rem;
}

.view-all-link {
  color: var(--primary-400);
  text-decoration: none;
  font-size: 0.875rem;
  font-weight: 500;
  display: flex;
  align-items: center;
  gap: 0.25rem;
  transition: color var(--transition-base);
}

.view-all-link:hover {
  color: var(--primary-300);
}

.modern-table {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.table-header {
  display: grid;
  grid-template-columns: 2fr 1fr 1fr 2fr 1.5fr 1fr;
  gap: 1rem;
  padding: 1rem;
  font-size: 0.75rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  color: var(--neutral-500);
}

.table-row {
  display: grid;
  grid-template-columns: 2fr 1fr 1fr 2fr 1.5fr 1fr;
  gap: 1rem;
  padding: 1rem;
  background: rgba(255, 255, 255, 0.02);
  border: 1px solid var(--glass-border);
  border-radius: var(--radius-lg);
  transition: all var(--transition-base);
}

.table-row:hover {
  background: rgba(168, 85, 247, 0.05);
  border-color: rgba(168, 85, 247, 0.2);
  transform: translateX(4px);
}

.table-cell {
  display: flex;
  align-items: center;
}

.queue-name {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.queue-icon {
  width: 32px;
  height: 32px;
  background: rgba(168, 85, 247, 0.1);
  border-radius: var(--radius-md);
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--primary-400);
}

.queue-link {
  color: white;
  text-decoration: none;
  font-weight: 500;
  transition: color var(--transition-base);
}

.queue-link:hover {
  color: var(--primary-400);
}

.priority-badge {
  display: flex;
  align-items: center;
  gap: 0.25rem;
  padding: 0.25rem 0.75rem;
  border-radius: var(--radius-full);
  font-size: 0.75rem;
  font-weight: 600;
}

.priority-high {
  background: rgba(239, 68, 68, 0.1);
  color: var(--danger);
}

.priority-medium {
  background: rgba(245, 158, 11, 0.1);
  color: var(--warning);
}

.priority-low {
  background: rgba(59, 130, 246, 0.1);
  color: var(--info);
}

.status-indicator {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  font-size: 0.875rem;
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--neutral-500);
}

.status-dot.active {
  background: var(--success);
  animation: pulse-glow 2s ease-in-out infinite;
}

.status-dot.idle {
  background: var(--neutral-500);
}

.message-stats {
  display: flex;
  align-items: center;
  gap: 1rem;
}

.stat-item {
  display: flex;
  flex-direction: column;
  align-items: center;
}

.stat-value {
  font-weight: 600;
  color: white;
}

.stat-label {
  font-size: 0.625rem;
  color: var(--neutral-500);
  text-transform: uppercase;
}

.stat-divider {
  width: 1px;
  height: 24px;
  background: var(--glass-border);
}

.performance-bar {
  flex: 1;
  height: 24px;
  background: rgba(255, 255, 255, 0.05);
  border-radius: var(--radius-full);
  position: relative;
  overflow: hidden;
}

.performance-fill {
  height: 100%;
  background: linear-gradient(90deg, var(--success), var(--accent-teal));
  transition: width var(--transition-base);
}

.performance-text {
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  font-size: 0.75rem;
  font-weight: 600;
  color: white;
}

.action-buttons {
  display: flex;
  gap: 0.5rem;
}

.action-icon {
  width: 32px;
  height: 32px;
  border-radius: var(--radius-md);
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid var(--glass-border);
  color: var(--neutral-300);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all var(--transition-base);
}

.action-icon:hover {
  background: rgba(168, 85, 247, 0.1);
  border-color: rgba(168, 85, 247, 0.3);
  color: var(--primary-400);
  transform: scale(1.1);
}

.action-icon.danger:hover {
  background: rgba(239, 68, 68, 0.1);
  border-color: rgba(239, 68, 68, 0.3);
  color: var(--danger);
}

/* Activity Section */
.activity-section {
  padding: 1.5rem;
}

.live-indicator {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.live-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--success);
  animation: pulse-glow 2s ease-in-out infinite;
}

/* Icon Button */
.icon-btn {
  width: 32px;
  height: 32px;
  border-radius: var(--radius-md);
  background: transparent;
  border: 1px solid var(--glass-border);
  color: var(--neutral-400);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all var(--transition-base);
}

.icon-btn:hover {
  background: rgba(255, 255, 255, 0.05);
  color: white;
}

/* Animations */
.table-row-enter-active,
.table-row-leave-active {
  transition: all var(--transition-base);
}

.table-row-enter-from {
  opacity: 0;
  transform: translateX(-20px);
}

.table-row-leave-to {
  opacity: 0;
  transform: translateX(20px);
}

/* Responsive */
@media (max-width: 1200px) {
  .charts-section {
    grid-template-columns: 1fr;
  }
  
  .metrics-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 768px) {
  .dashboard-header {
    flex-direction: column;
    align-items: flex-start;
    gap: 1rem;
  }
  
  .metrics-grid {
    grid-template-columns: 1fr;
  }
  
  .table-header,
  .table-row {
    grid-template-columns: 1fr;
    gap: 0.5rem;
  }
  
  .table-cell {
    padding: 0.5rem 0;
  }
}
</style>
