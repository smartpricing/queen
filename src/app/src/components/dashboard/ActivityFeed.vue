<template>
  <div class="activity-feed">
    <ScrollPanel style="width: 100%; height: 400px">
      <div v-if="activities.length === 0" class="empty-state">
        <i class="pi pi-inbox text-4xl text-gray-500"></i>
        <p>No recent activity</p>
      </div>
      
      <Timeline v-else :value="activities" class="activity-timeline">
        <template #marker="{ item }">
          <div class="activity-marker" :class="`activity-marker--${getEventType(item.type)}`">
            <i :class="getEventIcon(item.type)"></i>
          </div>
        </template>
        
        <template #content="{ item }">
          <div class="activity-item">
            <div class="activity-header">
              <span class="activity-type">{{ formatEventType(item.type) }}</span>
              <span class="activity-time">{{ formatTime(item.timestamp) }}</span>
            </div>
            <div class="activity-details">
              <template v-if="item.type === 'push'">
                <Tag severity="info">{{ item.count }} messages</Tag>
              </template>
              <template v-else-if="item.type === 'pop'">
                <span class="text-sm">{{ item.queue }}</span>
                <Tag severity="success">{{ item.count }} messages</Tag>
              </template>
              <template v-else-if="item.type === 'ack'">
                <Tag :severity="item.status === 'completed' ? 'success' : 'danger'">
                  {{ item.status }}
                </Tag>
              </template>
              <template v-else-if="item.data">
                <span class="text-sm text-gray-400">
                  {{ JSON.stringify(item.data).substring(0, 100) }}...
                </span>
              </template>
            </div>
          </div>
        </template>
      </Timeline>
    </ScrollPanel>
  </div>
</template>

<script setup>
import ScrollPanel from 'primevue/scrollpanel'
import Timeline from 'primevue/timeline'
import Tag from 'primevue/tag'
import { formatDistanceToNow } from 'date-fns'

defineProps({
  activities: {
    type: Array,
    default: () => []
  }
})

function getEventType(type) {
  if (type.includes('completed')) return 'success'
  if (type.includes('failed')) return 'error'
  if (type.includes('processing')) return 'warning'
  if (type.includes('push')) return 'info'
  if (type.includes('pop')) return 'success'
  return 'default'
}

function getEventIcon(type) {
  if (type.includes('push')) return 'pi pi-upload'
  if (type.includes('pop')) return 'pi pi-download'
  if (type.includes('completed')) return 'pi pi-check-circle'
  if (type.includes('failed')) return 'pi pi-times-circle'
  if (type.includes('processing')) return 'pi pi-spin pi-spinner'
  if (type.includes('ack')) return 'pi pi-check'
  return 'pi pi-circle'
}

function formatEventType(type) {
  return type.replace(/[._]/g, ' ').replace(/\b\w/g, l => l.toUpperCase())
}

function formatTime(timestamp) {
  if (!timestamp) return ''
  const date = timestamp instanceof Date ? timestamp : new Date(timestamp)
  return formatDistanceToNow(date, { addSuffix: true })
}
</script>

<style scoped>
.activity-feed {
  height: 100%;
}

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 300px;
  color: #737373;
}

.activity-timeline :deep(.p-timeline-event-opposite) {
  display: none;
}

.activity-marker {
  width: 32px;
  height: 32px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 0.875rem;
}

.activity-marker--success {
  background: rgba(16, 185, 129, 0.2);
  color: #10b981;
}

.activity-marker--error {
  background: rgba(239, 68, 68, 0.2);
  color: #ef4444;
}

.activity-marker--warning {
  background: rgba(245, 158, 11, 0.2);
  color: #f59e0b;
}

.activity-marker--info {
  background: rgba(99, 102, 241, 0.2);
  color: #6366f1;
}

.activity-marker--default {
  background: var(--surface-200);
  color: #a3a3a3;
}

.activity-item {
  padding: 0.5rem 0;
}

.activity-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 0.5rem;
}

.activity-type {
  font-weight: 500;
  color: white;
  font-size: 0.875rem;
}

.activity-time {
  font-size: 0.75rem;
  color: #737373;
}

.activity-details {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  flex-wrap: wrap;
}
</style>
