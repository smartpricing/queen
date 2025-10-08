<template>
  <div class="metric-card-v3" :class="`metric-${color}`">
    <div class="metric-icon-wrapper">
      <i :class="icon"></i>
    </div>
    
    <div class="metric-content">
      <p class="metric-label">{{ title }}</p>
      
      <h3 class="metric-value" v-if="!loading">
        {{ formattedValue }}{{ suffix }}
      </h3>
      <Skeleton v-else height="2rem" width="80%" />
      
      <p class="metric-change" :class="changeClass" v-if="change !== null && !loading">
        <i :class="changeIcon"></i>
        <span>{{ Math.abs(change) }}%</span>
        <span class="metric-period">{{ period }}</span>
      </p>
    </div>
    
    <div class="metric-sparkline" v-if="sparklineData && sparklineData.length > 0">
      <svg viewBox="0 0 80 30" preserveAspectRatio="none">
        <polyline
          :points="sparklinePoints"
          fill="none"
          :stroke="sparklineColor"
          stroke-width="2"
          stroke-linecap="round"
          stroke-linejoin="round"
        />
      </svg>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'
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
    default: 'pi pi-chart-line'
  },
  color: {
    type: String,
    default: 'primary',
    validator: (value) => ['primary', 'success', 'warning', 'danger', 'info'].includes(value)
  },
  change: {
    type: Number,
    default: null
  },
  period: {
    type: String,
    default: 'vs last hour'
  },
  suffix: {
    type: String,
    default: ''
  },
  loading: {
    type: Boolean,
    default: false
  },
  sparklineData: {
    type: Array,
    default: () => []
  }
})

// Format large numbers
const formattedValue = computed(() => {
  const val = typeof props.value === 'string' ? parseFloat(props.value) : props.value
  if (isNaN(val)) return props.value
  
  if (val >= 1000000) {
    return (val / 1000000).toFixed(1) + 'M'
  } else if (val >= 1000) {
    return (val / 1000).toFixed(1) + 'K'
  }
  return val.toLocaleString()
})

// Change indicator
const changeClass = computed(() => ({
  positive: props.change > 0,
  negative: props.change < 0,
  neutral: props.change === 0
}))

const changeIcon = computed(() => {
  if (props.change > 0) return 'pi pi-arrow-up'
  if (props.change < 0) return 'pi pi-arrow-down'
  return 'pi pi-minus'
})

// Sparkline calculation
const sparklinePoints = computed(() => {
  if (!props.sparklineData || props.sparklineData.length === 0) return ''
  
  const data = props.sparklineData
  const max = Math.max(...data)
  const min = Math.min(...data)
  const range = max - min || 1
  
  return data.map((value, index) => {
    const x = (index / (data.length - 1)) * 80
    const y = 30 - ((value - min) / range) * 25
    return `${x},${y}`
  }).join(' ')
})

const sparklineColor = computed(() => {
  const colors = {
    primary: '#ec4899',
    success: '#10b981',
    warning: '#f59e0b',
    danger: '#ef4444',
    info: '#06b6d4'
  }
  return colors[props.color] || colors.primary
})
</script>

<style scoped>
.metric-card-v3 {
  background: transparent;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 12px;
  padding: 1.25rem;
  display: flex;
  align-items: flex-start;
  gap: 1rem;
  position: relative;
  overflow: hidden;
  transition: all var(--transition-base);
  min-height: auto;
}

/* Removed top gradient line */

.metric-card-v3:hover {
  border-color: rgba(236, 72, 153, 0.15);
}

.metric-icon-wrapper {
  width: 56px;
  height: 56px;
  border-radius: 14px;
  background: var(--gradient-primary);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.metric-success .metric-icon-wrapper {
  background: linear-gradient(135deg, #10b981 0%, #059669 100%);
}

.metric-warning .metric-icon-wrapper {
  background: linear-gradient(135deg, #f59e0b 0%, #d97706 100%);
}

.metric-danger .metric-icon-wrapper {
  background: linear-gradient(135deg, #ef4444 0%, #dc2626 100%);
}

.metric-info .metric-icon-wrapper {
  background: linear-gradient(135deg, #06b6d4 0%, #0891b2 100%);
}

.metric-icon-wrapper i {
  font-size: 1.375rem;
  color: white;
}

.metric-content {
  flex: 1;
  min-width: 0;
}

.metric-label {
  font-size: 0.625rem;
  color: #78716c;
  text-transform: uppercase;
  letter-spacing: 0.1em;
  margin: 0 0 0.5rem 0;
  font-weight: 500;
  opacity: 0.8;
}

.metric-value {
  font-size: 2rem;
  font-weight: 700;
  color: #fafaf9;
  line-height: 1;
  margin: 0;
}

.metric-change {
  display: inline-flex;
  align-items: center;
  gap: 0.25rem;
  font-size: 0.875rem;
  font-weight: 500;
  margin: 0;
}

.metric-change.positive {
  color: var(--success-color);
}

.metric-change.negative {
  color: var(--danger-color);
}

.metric-change.neutral {
  color: var(--surface-400);
}

.metric-change i {
  font-size: 0.75rem;
}

.metric-period {
  color: var(--surface-400);
  font-weight: 400;
  margin-left: 0.25rem;
}

.metric-sparkline {
  position: absolute;
  right: 1.25rem;
  bottom: 1.25rem;
  width: 100px;
  height: 40px;
  opacity: 0.4;
}

.metric-card-v3:hover .metric-sparkline {
  opacity: 0.6;
}

/* Responsive */
@media (max-width: 768px) {
  .metric-card-v3 {
    padding: 1rem;
    min-height: 100px;
  }
  
  .metric-icon-wrapper {
    width: 40px;
    height: 40px;
  }
  
  .metric-icon-wrapper i {
    font-size: 1rem;
  }
  
  .metric-value {
    font-size: 1.5rem;
  }
  
  .metric-label {
    font-size: 0.7rem;
  }
  
  .metric-change {
    font-size: 0.75rem;
  }
  
  .metric-sparkline {
    width: 60px;
    height: 20px;
  }
}

@media (max-width: 480px) {
  .metric-card-v3 {
    padding: 0.875rem;
  }
  
  .metric-value {
    font-size: 1.25rem;
  }
}
</style>