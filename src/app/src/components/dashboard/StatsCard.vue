<template>
  <Card class="stats-card" :class="`stats-card--${color}`">
    <template #content>
      <div class="stats-content">
        <div class="stats-icon">
          <i :class="icon"></i>
        </div>
        <div class="stats-info">
          <p class="stats-title">{{ title }}</p>
          <h2 class="stats-value">
            <Skeleton v-if="loading" width="80px" height="2rem" />
            <span v-else>{{ formattedValue }}</span>
          </h2>
        </div>
      </div>
    </template>
  </Card>
</template>

<script setup>
import { computed } from 'vue'
import Card from 'primevue/card'
import Skeleton from 'primevue/skeleton'

const props = defineProps({
  title: String,
  value: {
    type: Number,
    default: 0
  },
  icon: String,
  color: {
    type: String,
    default: 'primary'
  },
  loading: Boolean
})

const formattedValue = computed(() => {
  if (props.value >= 1000000) {
    return `${(props.value / 1000000).toFixed(1)}M`
  }
  if (props.value >= 1000) {
    return `${(props.value / 1000).toFixed(1)}K`
  }
  return props.value.toLocaleString()
})
</script>

<style scoped>
.stats-card {
  background: var(--surface-100);
  border: 1px solid var(--surface-300);
  transition: transform 0.2s, box-shadow 0.2s;
}

.stats-card:hover {
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
}

.stats-content {
  display: flex;
  align-items: center;
  gap: 1rem;
}

.stats-icon {
  width: 48px;
  height: 48px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 1.5rem;
}

.stats-card--purple .stats-icon {
  background: rgba(168, 85, 247, 0.1);
  color: #a855f7;
}

.stats-card--blue .stats-icon {
  background: rgba(59, 130, 246, 0.1);
  color: #3b82f6;
}

.stats-card--yellow .stats-icon {
  background: rgba(245, 158, 11, 0.1);
  color: #f59e0b;
}

.stats-card--green .stats-icon {
  background: rgba(16, 185, 129, 0.1);
  color: #10b981;
}

.stats-card--red .stats-icon {
  background: rgba(239, 68, 68, 0.1);
  color: #ef4444;
}

.stats-info {
  flex: 1;
}

.stats-title {
  margin: 0;
  color: #a3a3a3;
  font-size: 0.875rem;
  margin-bottom: 0.25rem;
}

.stats-value {
  margin: 0;
  color: white;
  font-size: 1.75rem;
  font-weight: 600;
}
</style>
