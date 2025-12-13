<template>
  <div class="panel-wrapper">
    <!-- Summary Stats Row -->
    <div class="summary-row">
      <div class="summary-stat">
        <div class="summary-value text-orange-500">{{ formatNumber(summary?.totalPushMessages || 0) }}</div>
        <div class="summary-label">Total Push</div>
      </div>
      <div class="summary-stat">
        <div class="summary-value text-indigo-500">{{ formatNumber(summary?.totalPopMessages || 0) }}</div>
        <div class="summary-label">Total Pop</div>
      </div>
      <div class="summary-stat">
        <div class="summary-value text-green-500">{{ formatNumber(summary?.totalAckMessages || 0) }}</div>
        <div class="summary-label">Total Ack</div>
      </div>
      <div class="summary-stat">
        <div :class="['summary-value', pendingClass]">{{ formatNumber(summary?.pendingMessages || 0) }}</div>
        <div class="summary-label">Pending</div>
      </div>
      <div class="summary-stat">
        <div :class="['summary-value', errorsClass]">{{ formatNumber(totalErrors) }}</div>
        <div class="summary-label">Errors</div>
      </div>
    </div>

    <!-- Workers Grid -->
    <div class="section-header">
      <span class="section-title">Active Workers</span>
      <span class="worker-count">{{ workers?.length || 0 }}</span>
    </div>
    <div v-if="workers?.length" class="workers-grid">
      <div v-for="w in workers" :key="w.hostname + w.workerId" class="worker-card">
        <div class="worker-header">
          <div class="worker-name">{{ w.hostname }}/{{ w.workerId }}</div>
          <div :class="['worker-status', getHealthClass(w)]">
            {{ getHealthLabel(w) }}
          </div>
        </div>
        <div class="worker-stats">
          <div class="worker-stat">
            <span class="worker-stat-label">Event Loop</span>
            <span :class="['worker-stat-value', getElClass(w.avgEventLoopLagMs)]">{{ w.avgEventLoopLagMs }}ms</span>
          </div>
          <div class="worker-stat">
            <span class="worker-stat-label">Free Slots</span>
            <span :class="['worker-stat-value', getSlotsClass(w.freeSlots, w.dbConnections)]">{{ w.freeSlots }}/{{ w.dbConnections }}</span>
          </div>
          <div class="worker-stat">
            <span class="worker-stat-label">Job Queue</span>
            <span :class="['worker-stat-value', getQueueClass(w.jobQueueSize)]">{{ w.jobQueueSize }}</span>
          </div>
          <div class="worker-stat">
            <span class="worker-stat-label">Backoff</span>
            <span :class="['worker-stat-value', getBackoffClass(w.backoffSize)]">{{ w.backoffSize || 0 }}</span>
          </div>
        </div>
      </div>
    </div>
    <div v-else class="empty-message">
      No active workers
    </div>

    <!-- Per-Queue Lag -->
    <div class="section-header mt-4">
      <span class="section-title">Queue Lag</span>
      <span class="queue-count">{{ queues?.length || 0 }} queues</span>
    </div>
    <div v-if="queues?.length" class="queues-list">
      <div v-for="q in topQueues" :key="q.queueName" class="queue-row">
        <div class="queue-name">{{ q.queueName }}</div>
        <div class="queue-stats">
          <span class="queue-stat">
            <span class="queue-stat-label">pops:</span>
            <span class="queue-stat-value">{{ formatNumber(q.popCount) }}</span>
          </span>
          <span class="queue-stat">
            <span class="queue-stat-label">avg:</span>
            <span :class="['queue-stat-value', getLagClass(q.avgLagMs)]">{{ formatDuration(q.avgLagMs) }}</span>
          </span>
          <span class="queue-stat">
            <span class="queue-stat-label">max:</span>
            <span :class="['queue-stat-value', getLagClass(q.maxLagMs)]">{{ formatDuration(q.maxLagMs) }}</span>
          </span>
        </div>
      </div>
    </div>
    <div v-else class="empty-message">
      No queue data
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue';

const props = defineProps({
  data: Object,
});

const workers = computed(() => props.data?.workers || []);
const queues = computed(() => props.data?.queues || []);
const summary = computed(() => props.data?.summary || {});
const topQueues = computed(() => queues.value.slice(0, 8));

const totalErrors = computed(() => {
  const s = summary.value;
  return (s.totalDbErrors || 0) + (s.totalAckFailed || 0) + (s.totalDlq || 0);
});

const pendingClass = computed(() => {
  const pending = summary.value?.pendingMessages || 0;
  if (pending > 10000) return 'text-red-500';
  if (pending > 1000) return 'text-amber-500';
  return 'text-cyan-500';
});

const errorsClass = computed(() => {
  return totalErrors.value > 0 ? 'text-red-500' : 'text-gray-500';
});

function formatNumber(n) {
  if (n >= 1000000) return (n / 1000000).toFixed(1) + 'M';
  if (n >= 1000) return (n / 1000).toFixed(1) + 'K';
  return String(n);
}

function formatDuration(ms) {
  if (!ms || ms === 0) return '0ms';
  if (ms < 1000) return ms + 'ms';
  if (ms < 60000) return (ms / 1000).toFixed(1) + 's';
  if (ms < 3600000) return (ms / 60000).toFixed(1) + 'm';
  return (ms / 3600000).toFixed(1) + 'h';
}

function getHealthClass(w) {
  const el = w.avgEventLoopLagMs || 0;
  if (el > 1000) return 'status-red';
  if (el > 100) return 'status-yellow';
  return 'status-green';
}

function getHealthLabel(w) {
  const el = w.avgEventLoopLagMs || 0;
  if (el > 1000) return 'Degraded';
  if (el > 100) return 'Slow';
  return 'Healthy';
}

function getElClass(ms) {
  if (ms > 1000) return 'text-red-500';
  if (ms > 100) return 'text-amber-500';
  return 'text-green-500';
}

function getSlotsClass(free, total) {
  if (!total) return 'text-gray-500';
  const util = 1 - (free / total);
  if (util > 0.9) return 'text-red-500';
  if (util > 0.7) return 'text-amber-500';
  return 'text-cyan-500';
}

function getQueueClass(size) {
  if (size > 100) return 'text-red-500';
  if (size > 10) return 'text-amber-500';
  return 'text-green-500';
}

function getBackoffClass(size) {
  if (size > 50) return 'text-amber-500';  // Many idle consumers
  if (size > 0) return 'text-violet-500';   // Some idle
  return 'text-gray-500';                   // None idle
}

function getLagClass(ms) {
  if (ms > 60000) return 'text-red-500';
  if (ms > 1000) return 'text-amber-500';
  return 'text-green-500';
}
</script>

<style scoped>
.summary-row {
  @apply flex items-center justify-between gap-4 pb-3 border-b border-gray-200/60 dark:border-gray-700/60;
}
.summary-stat {
  @apply text-center flex-1;
}
.summary-value {
  @apply text-lg font-bold font-mono;
}
.summary-label {
  @apply text-[10px] text-gray-500 dark:text-gray-400 uppercase tracking-wide;
}

.section-header {
  @apply flex items-center justify-between py-2;
}
.section-title {
  @apply text-xs font-semibold text-gray-700 dark:text-gray-300;
}
.worker-count, .queue-count {
  @apply text-[10px] text-gray-500 dark:text-gray-400 font-mono;
}

.workers-grid {
  @apply grid grid-cols-1 sm:grid-cols-2 gap-2;
}
.worker-card {
  @apply p-2 rounded-lg border border-gray-200/60 dark:border-gray-700/60 bg-gray-50/50 dark:bg-gray-800/30;
}
.worker-header {
  @apply flex items-center justify-between mb-1.5;
}
.worker-name {
  @apply text-xs font-semibold text-gray-800 dark:text-gray-200 font-mono truncate;
}
.worker-status {
  @apply text-[10px] font-semibold px-1.5 py-0.5 rounded;
}
.status-green {
  @apply bg-green-100 text-green-700 dark:bg-green-900/30 dark:text-green-400;
}
.status-yellow {
  @apply bg-amber-100 text-amber-700 dark:bg-amber-900/30 dark:text-amber-400;
}
.status-red {
  @apply bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-400;
}
.worker-stats {
  @apply grid grid-cols-4 gap-1;
}
.worker-stat {
  @apply flex flex-col;
}
.worker-stat-label {
  @apply text-[9px] text-gray-500 dark:text-gray-500;
}
.worker-stat-value {
  @apply text-[11px] font-mono font-semibold;
}

.queues-list {
  @apply space-y-1;
}
.queue-row {
  @apply flex items-center justify-between py-1 px-2 rounded bg-gray-50/50 dark:bg-gray-800/30;
}
.queue-name {
  @apply text-xs font-mono text-gray-700 dark:text-gray-300 truncate max-w-[180px];
}
.queue-stats {
  @apply flex items-center gap-3;
}
.queue-stat {
  @apply flex items-center gap-0.5;
}
.queue-stat-label {
  @apply text-[9px] text-gray-500 dark:text-gray-500;
}
.queue-stat-value {
  @apply text-[10px] font-mono font-semibold;
}

.empty-message {
  @apply text-xs text-gray-500 dark:text-gray-400 italic text-center py-3;
}
</style>

