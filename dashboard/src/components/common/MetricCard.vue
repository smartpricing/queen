<template>
  <div class="bg-white dark:bg-gray-900 rounded-lg border border-gray-200 dark:border-gray-800 p-6 hover:shadow-lg transition-shadow">
    <div class="flex items-start justify-between">
      <div class="flex-1">
        <p class="text-sm font-medium text-gray-600 dark:text-gray-400">
          {{ title }}
        </p>
        <div class="mt-2 flex items-baseline">
          <p class="text-3xl font-semibold text-gray-900 dark:text-white">
            {{ formattedValue }}
          </p>
          <p v-if="unit" class="ml-2 text-sm text-gray-500 dark:text-gray-400">
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
      <div v-if="icon" :class="`p-3 rounded-lg ${iconBgClass}`">
        <component :is="icon" :class="`w-6 h-6 ${iconColorClass}`" />
      </div>
    </div>
    
    <!-- Trend indicator -->
    <div v-if="trend" class="mt-4 flex items-center">
      <svg 
        v-if="trend > 0" 
        class="w-4 h-4 text-green-500"
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
      <span :class="trend > 0 ? 'text-green-600 dark:text-green-400' : 'text-red-600 dark:text-red-400'" class="ml-1 text-sm font-medium">
        {{ Math.abs(trend) }}%
      </span>
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
    return props.value.toLocaleString();
  }
  return props.value;
});

const iconBgClass = computed(() => {
  const colors = {
    green: 'bg-green-100 dark:bg-green-900/30',
    blue: 'bg-blue-100 dark:bg-blue-900/30',
    yellow: 'bg-yellow-100 dark:bg-yellow-900/30',
    red: 'bg-red-100 dark:bg-red-900/30',
    purple: 'bg-purple-100 dark:bg-purple-900/30'
  };
  return colors[props.iconColor] || colors.green;
});

const iconColorClass = computed(() => {
  const colors = {
    green: 'text-green-600 dark:text-green-400',
    blue: 'text-blue-600 dark:text-blue-400',
    yellow: 'text-yellow-600 dark:text-yellow-400',
    red: 'text-red-600 dark:text-red-400',
    purple: 'text-purple-600 dark:text-purple-400'
  };
  return colors[props.iconColor] || colors.green;
});
</script>

