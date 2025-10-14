<template>
  <div class="group bg-white dark:bg-gray-900 rounded-2xl border border-gray-200 dark:border-gray-800 p-6 card-elevation-1 hover:card-elevation-3 hover:border-emerald-200 dark:hover:border-emerald-800 transition-all duration-300 hover:-translate-y-0.5">
    <div class="flex items-start justify-between gap-4">
      <div class="flex-1 min-w-0">
        <p class="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider mb-3">
          {{ title }}
        </p>
        <div class="flex items-baseline gap-2">
          <p class="text-4xl font-bold gradient-text metric-value">
            {{ formattedValue }}
          </p>
          <p v-if="unit" class="text-base text-gray-500 dark:text-gray-400 font-semibold">
            {{ unit }}
          </p>
        </div>
        <div v-if="subtitle" class="mt-2 flex items-center">
          <span class="text-sm text-gray-600 dark:text-gray-400">
            {{ subtitle }}
          </span>
        </div>
      </div>
      
      <!-- Icon -->
      <div v-if="icon" :class="`flex-shrink-0 p-3 rounded-xl transition-all duration-300 group-hover:scale-110 ${iconBgClass}`">
        <component :is="icon" :class="`w-6 h-6 ${iconColorClass}`" />
      </div>
    </div>
    
    <!-- Trend indicator -->
    <div v-if="trend !== null && trend !== undefined" class="mt-4 flex items-center">
      <svg 
        v-if="trend > 0" 
        class="w-4 h-4 text-emerald-500"
        fill="none" 
        stroke="currentColor" 
        viewBox="0 0 24 24"
      >
        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 10l7-7m0 0l7 7m-7-7v18" />
      </svg>
      <svg 
        v-else-if="trend < 0" 
        class="w-4 h-4 text-red-500"
        fill="none" 
        stroke="currentColor" 
        viewBox="0 0 24 24"
      >
        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 14l-7 7m0 0l-7-7m7 7V3" />
      </svg>
      <span :class="trend > 0 ? 'text-emerald-600 dark:text-emerald-400' : 'text-red-600 dark:text-red-400'" class="ml-1 text-sm font-semibold">
        {{ Math.abs(trend) }}%
      </span>
      <span class="ml-1 text-xs text-gray-500 dark:text-gray-400">vs last period</span>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue';

const props = defineProps({
  title: {
    type: String,
    required: true
  },
  value: {
    type: [Number, String],
    required: true
  },
  unit: {
    type: String,
    default: ''
  },
  subtitle: {
    type: String,
    default: ''
  },
  icon: {
    type: Object,
    default: null
  },
  iconColor: {
    type: String,
    default: 'green'
  },
  trend: {
    type: Number,
    default: null
  }
});

const formattedValue = computed(() => {
  if (typeof props.value === 'number') {
    // Format large numbers with suffixes (K, M, B)
    const absValue = Math.abs(props.value);
    if (absValue >= 1000000000) {
      return (props.value / 1000000000).toFixed(1) + 'B';
    } else if (absValue >= 1000000) {
      return (props.value / 1000000).toFixed(1) + 'M';
    } else if (absValue >= 10000) {
      return (props.value / 1000).toFixed(1) + 'K';
    } else {
      return props.value.toLocaleString();
    }
  }
  return props.value;
});

const iconBgClass = computed(() => {
  const colors = {
    green: 'bg-gradient-to-br from-emerald-100 to-emerald-50 dark:from-emerald-900/30 dark:to-emerald-900/20',
    blue: 'bg-gradient-to-br from-blue-100 to-blue-50 dark:from-blue-900/30 dark:to-blue-900/20',
    yellow: 'bg-gradient-to-br from-yellow-100 to-yellow-50 dark:from-yellow-900/30 dark:to-yellow-900/20',
    red: 'bg-gradient-to-br from-red-100 to-red-50 dark:from-red-900/30 dark:to-red-900/20',
    purple: 'bg-gradient-to-br from-purple-100 to-purple-50 dark:from-purple-900/30 dark:to-purple-900/20'
  };
  return colors[props.iconColor] || colors.green;
});

const iconColorClass = computed(() => {
  const colors = {
    green: 'text-emerald-600 dark:text-emerald-400',
    blue: 'text-blue-600 dark:text-blue-400',
    yellow: 'text-yellow-600 dark:text-yellow-400',
    red: 'text-red-600 dark:text-red-400',
    purple: 'text-purple-600 dark:text-purple-400'
  };
  return colors[props.iconColor] || colors.green;
});
</script>

