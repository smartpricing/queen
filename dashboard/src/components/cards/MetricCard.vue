<template>
  <Card class="metric-card" :class="`metric-card-${color}`">
    <template #content>
      <div class="metric-content">
      <div class="metric-header">
        <i :class="icon" class="metric-icon"></i>
        <div class="metric-trend" v-if="trend">
          <i 
            :class="trendIcon" 
            :style="{ color: trendColor }"
          ></i>
        </div>
      </div>
      
      <div class="metric-body">
        <div class="metric-value" v-if="!loading">
          {{ value }}{{ suffix }}
        </div>
        <Skeleton v-else height="2rem" />
        
        <div class="metric-title">{{ title }}</div>
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
  title: {
    type: String,
    required: true
  },
  value: {
    type: [String, Number],
    required: true
  },
  icon: {
    type: String,
    default: 'pi pi-info-circle'
  },
  color: {
    type: String,
    default: 'primary',
    validator: (value) => ['primary', 'success', 'warning', 'danger', 'info'].includes(value)
  },
  trend: {
    type: String,
    default: null,
    validator: (value) => !value || ['up', 'down', 'neutral'].includes(value)
  },
  suffix: {
    type: String,
    default: ''
  },
  loading: {
    type: Boolean,
    default: false
  }
})

const trendIcon = computed(() => {
  if (!props.trend) return ''
  return {
    up: 'pi pi-arrow-up',
    down: 'pi pi-arrow-down',
    neutral: 'pi pi-minus'
  }[props.trend]
})

const trendColor = computed(() => {
  if (!props.trend) return ''
  return {
    up: 'var(--success-color)',
    down: 'var(--danger-color)',
    neutral: 'var(--gray-500)'
  }[props.trend]
})
</script>

<style scoped>
.metric-card {
  position: relative;
  overflow: hidden;
  transition: box-shadow 0.2s ease;
  background: white;
  border: 1px solid var(--gray-200);
}

.metric-card:hover {
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.08);
}

.metric-card::before {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 3px;
  background: var(--card-color, var(--primary-color));
}

.metric-card-primary::before { --card-color: var(--primary-color); }
.metric-card-success::before { --card-color: var(--success-color); }
.metric-card-warning::before { --card-color: var(--warning-color); }
.metric-card-danger::before { --card-color: var(--danger-color); }
.metric-card-info::before { --card-color: var(--info-color); }

:deep(.p-card-body) {
  padding: 1.25rem;
}

.metric-content {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.metric-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
}

.metric-icon {
  font-size: 1.5rem;
  color: var(--card-color, var(--primary-color));
  opacity: 0.8;
}

.metric-card-primary .metric-icon { color: var(--primary-color); }
.metric-card-success .metric-icon { color: var(--success-color); }
.metric-card-warning .metric-icon { color: var(--warning-color); }
.metric-card-danger .metric-icon { color: var(--danger-color); }
.metric-card-info .metric-icon { color: var(--info-color); }

.metric-trend {
  font-size: 0.875rem;
}

.metric-body {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.metric-value {
  font-size: 1.5rem;
  font-weight: 600;
  color: var(--gray-900);
  line-height: 1;
}

.metric-title {
  font-size: 0.75rem;
  color: var(--gray-600);
  font-weight: 500;
  text-transform: uppercase;
  letter-spacing: 0.025em;
}

/* Responsive */
@media (max-width: 768px) {
  .metric-value {
    font-size: 1.25rem;
  }
  
  .metric-title {
    font-size: 0.625rem;
  }
  
  .metric-icon {
    font-size: 1.25rem;
  }
  
  :deep(.p-card-body) {
    padding: 0.75rem;
  }
}

@media (max-width: 480px) {
  .metric-value {
    font-size: 1.125rem;
  }
  
  :deep(.p-card-body) {
    padding: 0.625rem;
  }
}
</style>
