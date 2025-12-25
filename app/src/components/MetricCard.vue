<template>
  <div 
    class="metric-card group cursor-pointer"
    :class="{ 'hover:shadow-glow-queen': clickable }"
    @click="$emit('click')"
  >
    <div class="flex items-start justify-between">
      <div>
        <p class="metric-label">{{ label }}</p>
        <p class="metric-value mt-1" :class="valueClass">
          <span v-if="loading" class="skeleton w-20 h-8" />
          <span v-else>{{ formattedValue }}</span>
        </p>
        <p v-if="subtext" class="text-xs text-light-500 dark:text-light-500 mt-1">
          {{ subtext }}
        </p>
      </div>
      
      <!-- Icon or trend -->
      <div 
        v-if="icon || trend !== null"
        class="flex-shrink-0"
      >
        <div 
          v-if="icon"
          class="w-12 h-12 rounded-xl flex items-center justify-center"
          :class="iconBgClass"
        >
          <component :is="icon" class="w-6 h-6" :class="iconClass" />
        </div>
        
        <div 
          v-else-if="trend !== null"
          class="flex items-center gap-1 metric-change"
          :class="trend >= 0 ? 'metric-change-up' : 'metric-change-down'"
        >
          <svg v-if="trend >= 0" class="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
            <path stroke-linecap="round" stroke-linejoin="round" d="M7 11l5-5m0 0l5 5m-5-5v12" />
          </svg>
          <svg v-else class="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
            <path stroke-linecap="round" stroke-linejoin="round" d="M17 13l-5 5m0 0l-5-5m5 5V6" />
          </svg>
          <span>{{ Math.abs(trend) }}%</span>
        </div>
      </div>
    </div>
    
    <!-- Progress bar (optional) -->
    <div v-if="progress !== null" class="mt-4">
      <div class="flex items-center justify-between text-xs text-light-500 mb-1">
        <span>{{ progressLabel }}</span>
        <span>{{ progress }}%</span>
      </div>
      <div class="h-1.5 bg-light-200 dark:bg-dark-100 rounded-full overflow-hidden">
        <div 
          class="h-full rounded-full transition-all duration-500"
          :class="progressClass"
          :style="{ width: `${progress}%` }"
        />
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import { formatNumber } from '@/composables/useApi'

const props = defineProps({
  label: { type: String, required: true },
  value: { type: [Number, String], default: 0 },
  format: { type: String, default: 'number' }, // number, percent, duration, raw
  subtext: { type: String, default: null },
  icon: { type: [Object, Function], default: null },
  iconColor: { type: String, default: 'queen' }, // queen, cyber, crown
  trend: { type: Number, default: null },
  progress: { type: Number, default: null },
  progressLabel: { type: String, default: 'Progress' },
  progressColor: { type: String, default: 'queen' },
  loading: { type: Boolean, default: false },
  clickable: { type: Boolean, default: false },
})

defineEmits(['click'])

const formattedValue = computed(() => {
  if (props.value === null || props.value === undefined) return '-'
  
  switch (props.format) {
    case 'number':
      return formatNumber(props.value)
    case 'percent':
      return `${props.value}%`
    case 'duration':
      return props.value
    default:
      return props.value
  }
})

const valueClass = computed(() => {
  const colors = {
    queen: 'text-queen-600 dark:text-queen-400',
    cyber: 'text-cyber-600 dark:text-cyber-400',
    crown: 'text-crown-600 dark:text-crown-400',
  }
  return props.iconColor ? colors[props.iconColor] : ''
})

const iconBgClass = computed(() => {
  const colors = {
    queen: 'bg-queen-100 dark:bg-queen-500/20',
    cyber: 'bg-cyber-100 dark:bg-cyber-500/20',
    crown: 'bg-crown-100 dark:bg-crown-500/20',
  }
  return colors[props.iconColor] || colors.queen
})

const iconClass = computed(() => {
  const colors = {
    queen: 'text-queen-600 dark:text-queen-400',
    cyber: 'text-cyber-600 dark:text-cyber-400',
    crown: 'text-crown-600 dark:text-crown-400',
  }
  return colors[props.iconColor] || colors.queen
})

const progressClass = computed(() => {
  const colors = {
    queen: 'bg-gradient-to-r from-queen-500 to-queen-600',
    cyber: 'bg-gradient-to-r from-cyber-500 to-cyber-600',
    crown: 'bg-gradient-to-r from-crown-500 to-crown-600',
  }
  return colors[props.progressColor] || colors.queen
})
</script>

