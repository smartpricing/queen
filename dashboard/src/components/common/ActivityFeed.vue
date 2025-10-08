<template>
  <div class="activity-feed">
    <ScrollPanel class="feed-container">
      <div v-if="events.length > 0" class="feed-items">
        <div 
          v-for="event in events" 
          :key="event.id"
          class="feed-item fade-in"
        >
          <div class="feed-icon" :class="getEventClass(event.event)">
            <i :class="getEventIcon(event.event)"></i>
          </div>
          
          <div class="feed-content">
            <div class="feed-title">{{ getEventTitle(event.event) }}</div>
            <div class="feed-details">
              <span v-if="event.data && event.data.queue" class="feed-queue">
                {{ event.data.queue }}
                <span v-if="event.data.partition && event.data.partition !== 'Default'">
                  /{{ event.data.partition }}
                </span>
              </span>
              <span v-if="event.data && event.data.transactionId" class="feed-id">
                {{ event.data.transactionId.substring(0, 8) }}...
              </span>
            </div>
            <div class="feed-time">{{ formatTime(event.timestamp) }}</div>
          </div>
        </div>
      </div>
      
      <div v-else class="feed-empty">
        <i class="pi pi-inbox empty-icon"></i>
        <span>No activity yet</span>
        <span class="empty-subtitle">Events will appear here in real-time</span>
      </div>
    </ScrollPanel>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import ScrollPanel from 'primevue/scrollpanel'
import { formatRelativeTime } from '../../utils/helpers.js'

const props = defineProps({
  events: {
    type: Array,
    default: () => []
  }
})

// Get event icon
const getEventIcon = (eventType) => {
  if (!eventType || typeof eventType !== 'string') return 'pi pi-info-circle'
  const icons = {
    'message.pushed': 'pi pi-plus',
    'message.processing': 'pi pi-spin pi-spinner',
    'message.completed': 'pi pi-check',
    'message.failed': 'pi pi-times',
    'queue.created': 'pi pi-inbox',
    'queue.depth': 'pi pi-chart-bar',
    'system.stats': 'pi pi-chart-line',
    'client.connected': 'pi pi-sign-in',
    'client.disconnected': 'pi pi-sign-out'
  }
  return icons[eventType] || 'pi pi-info-circle'
}

// Get event class for styling
const getEventClass = (eventType) => {
  if (!eventType || typeof eventType !== 'string') return 'event-default'
  if (eventType.includes('completed')) return 'event-success'
  if (eventType.includes('failed')) return 'event-danger'
  if (eventType.includes('processing')) return 'event-info'
  if (eventType.includes('pushed') || eventType.includes('created')) return 'event-primary'
  return 'event-default'
}

// Get event title
const getEventTitle = (eventType) => {
  if (!eventType || typeof eventType !== 'string') return 'Unknown Event'
  const titles = {
    'message.pushed': 'Message Pushed',
    'message.processing': 'Processing Message',
    'message.completed': 'Message Completed',
    'message.failed': 'Message Failed',
    'queue.created': 'Queue Created',
    'queue.depth': 'Queue Depth Update',
    'system.stats': 'System Stats Update',
    'client.connected': 'Client Connected',
    'client.disconnected': 'Client Disconnected'
  }
  return titles[eventType] || eventType
}

// Format time
const formatTime = (timestamp) => {
  return formatRelativeTime(timestamp)
}
</script>

<style scoped>
.activity-feed {
  height: 100%;
}

.feed-container {
  height: 350px;
}

:deep(.p-scrollpanel-content) {
  padding: 0.5rem;
}

.feed-items {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.feed-item {
  display: flex;
  align-items: flex-start;
  gap: 1rem;
  padding: 0.875rem;
  border-radius: 8px;
  background: var(--surface-0);
  border: 1px solid rgba(255, 255, 255, 0.03);
  transition: all 0.2s ease;
}

.feed-item:hover {
  background: rgba(236, 72, 153, 0.05);
  border-color: rgba(236, 72, 153, 0.1);
}

.feed-icon {
  width: 36px;
  height: 36px;
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  font-size: 0.875rem;
}

.event-primary {
  background: rgba(236, 72, 153, 0.15);
  color: var(--primary-500);
}

.event-success {
  background: rgba(16, 185, 129, 0.15);
  color: var(--success-color);
}

.event-info {
  background: rgba(6, 182, 212, 0.15);
  color: var(--info-color);
}

.event-danger {
  background: rgba(239, 68, 68, 0.15);
  color: var(--danger-color);
}

.event-default {
  background: rgba(148, 163, 184, 0.15);
  color: var(--surface-400);
}

.feed-content {
  flex: 1;
  min-width: 0;
}

.feed-title {
  font-size: 0.875rem;
  font-weight: 500;
  color: var(--surface-600);
  margin-bottom: 0.125rem;
}

.feed-details {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  font-size: 0.75rem;
  color: var(--surface-400);
  margin-bottom: 0.25rem;
}

.feed-queue {
  background: rgba(236, 72, 153, 0.1);
  color: var(--primary-500);
  padding: 0.125rem 0.5rem;
  border-radius: 4px;
  font-weight: 500;
}

.feed-id {
  font-family: 'Courier New', monospace;
  font-size: 0.75rem;
  color: var(--surface-400);
}

.feed-time {
  font-size: 0.75rem;
  color: var(--surface-300);
}

.feed-empty {
  height: 100%;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 0.5rem;
  color: var(--surface-400);
  padding: 2rem;
  text-align: center;
}

.empty-icon {
  font-size: 3rem;
  color: var(--surface-300);
  margin-bottom: 0.5rem;
}

.empty-subtitle {
  font-size: 0.875rem;
  color: var(--surface-300);
}

.fade-in {
  animation: fadeIn 0.3s ease;
}

@keyframes fadeIn {
  from {
    opacity: 0;
    transform: translateY(-10px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}
</style>
