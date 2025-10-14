<template>
  <div class="metric-card-v2 group bg-gradient-to-br from-white to-gray-50/50 dark:from-gray-900 dark:to-gray-900/50 rounded-2xl border border-gray-200/80 dark:border-gray-800/80 p-5 hover:shadow-xl hover:border-primary-300/50 dark:hover:border-primary-700/50 cursor-default overflow-hidden relative">
    <!-- Background gradient accent - removed animation -->
    
    <div class="relative z-10">
      <!-- Header -->
      <div class="flex items-start justify-between gap-3 mb-4">
        <div class="flex-1 min-w-0">
          <div class="flex items-center gap-2 mb-1">
            <p class="text-xs font-bold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
              {{ title }}
            </p>
            <div v-if="isLive" class="flex items-center gap-1.5">
              <div class="w-1.5 h-1.5 rounded-full animate-pulse" :class="pulseColorClass"></div>
              <span class="text-[10px] font-semibold text-gray-400 dark:text-gray-500 uppercase">Live</span>
            </div>
          </div>
          
          <!-- Value with animation -->
          <div class="flex items-baseline gap-2 mb-2">
            <NumberCounter 
              :value="value" 
              :class="`text-3xl font-black tracking-tight ${valueColorClass}`"
            />
            <p v-if="unit" class="text-sm text-gray-500 dark:text-gray-400 font-bold">
              {{ unit }}
            </p>
          </div>
          
          <!-- Trend indicator -->
          <div v-if="trend !== null && trend !== undefined" class="flex items-center gap-1.5">
            <div :class="trendBgClass" class="flex items-center gap-1 px-2 py-0.5 rounded-full">
              <svg 
                v-if="trend > 0" 
                class="w-3 h-3"
                :class="trendColorClass"
                fill="none" 
                stroke="currentColor" 
                viewBox="0 0 24 24"
              >
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M5 10l7-7m0 0l7 7m-7-7v18" />
              </svg>
              <svg 
                v-else-if="trend < 0" 
                class="w-3 h-3"
                :class="trendColorClass"
                fill="none" 
                stroke="currentColor" 
                viewBox="0 0 24 24"
              >
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M19 14l-7 7m0 0l-7-7m7 7V3" />
              </svg>
              <svg 
                v-else 
                class="w-3 h-3 text-gray-400"
                fill="none" 
                stroke="currentColor" 
                viewBox="0 0 24 24"
              >
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M5 12h14" />
              </svg>
              <span :class="trendColorClass" class="text-xs font-bold">
                {{ trend === 0 ? '0' : Math.abs(trend) }}%
              </span>
            </div>
            <span class="text-[10px] text-gray-500 dark:text-gray-400 font-medium">vs last period</span>
          </div>
        </div>
        
        <!-- Icon with enhanced styling -->
        <div 
          v-if="icon" 
          :class="`flex-shrink-0 p-3 rounded-xl ${iconBgClass}`"
        >
          <component :is="icon" :class="`w-6 h-6 ${iconColorClass}`" />
        </div>
      </div>
      
      <!-- Sparkline -->
      <div v-if="sparklineData && sparklineData.length > 0" class="h-12 -mx-2">
        <svg class="w-full h-full" preserveAspectRatio="none" :viewBox="`0 0 ${sparklineData.length * 4} 100`">
          <defs>
            <linearGradient :id="`gradient-${title}`" x1="0%" y1="0%" x2="0%" y2="100%">
              <stop offset="0%" :stop-color="sparklineColor" stop-opacity="0.3"/>
              <stop offset="100%" :stop-color="sparklineColor" stop-opacity="0.05"/>
            </linearGradient>
          </defs>
          
          <!-- Area fill -->
          <path
            :d="sparklineAreaPath"
            :fill="`url(#gradient-${title})`"
          />
          
          <!-- Line -->
          <path
            :d="sparklinePath"
            :stroke="sparklineColor"
            stroke-width="2.5"
            fill="none"
            stroke-linecap="round"
            stroke-linejoin="round"
          />
          
          <!-- Dots on hover -->
          <g class="opacity-0 group-hover:opacity-100">
            <circle
              v-for="(point, i) in sparklinePoints"
              :key="i"
              :cx="point.x"
              :cy="point.y"
              r="2.5"
              :fill="sparklineColor"
              class="drop-shadow-lg"
            />
          </g>
        </svg>
      </div>
      
      <!-- Subtitle -->
      <p v-if="subtitle" class="text-xs text-gray-600 dark:text-gray-400 font-medium mt-3">
        {{ subtitle }}
      </p>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue';
import NumberCounter from './NumberCounter.vue';

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
    default: 'blue'
  },
  trend: {
    type: Number,
    default: null
  },
  sparklineData: {
    type: Array,
    default: () => []
  },
  isLive: {
    type: Boolean,
    default: false
  }
});

// Color mappings
const colorMap = {
  blue: {
    value: 'text-blue-900 dark:text-blue-100',
    icon: 'text-blue-600 dark:text-blue-400',
    iconBg: 'bg-gradient-to-br from-blue-500/10 to-blue-600/5 dark:from-blue-500/20 dark:to-blue-600/10 ring-1 ring-blue-500/20',
    gradient: 'from-blue-500/5 to-transparent',
    sparkline: 'rgb(59, 130, 246)',
    pulse: 'bg-blue-500'
  },
  green: {
    value: 'text-emerald-900 dark:text-emerald-100',
    icon: 'text-emerald-600 dark:text-emerald-400',
    iconBg: 'bg-gradient-to-br from-emerald-500/10 to-emerald-600/5 dark:from-emerald-500/20 dark:to-emerald-600/10 ring-1 ring-emerald-500/20',
    gradient: 'from-emerald-500/5 to-transparent',
    sparkline: 'rgb(16, 185, 129)',
    pulse: 'bg-emerald-500'
  },
  yellow: {
    value: 'text-amber-900 dark:text-amber-100',
    icon: 'text-amber-600 dark:text-amber-500',
    iconBg: 'bg-gradient-to-br from-amber-500/10 to-amber-600/5 dark:from-amber-500/20 dark:to-amber-600/10 ring-1 ring-amber-500/20',
    gradient: 'from-amber-500/5 to-transparent',
    sparkline: 'rgb(245, 158, 11)',
    pulse: 'bg-amber-500'
  },
  red: {
    value: 'text-red-900 dark:text-red-100',
    icon: 'text-red-600 dark:text-red-400',
    iconBg: 'bg-gradient-to-br from-red-500/10 to-red-600/5 dark:from-red-500/20 dark:to-red-600/10 ring-1 ring-red-500/20',
    gradient: 'from-red-500/5 to-transparent',
    sparkline: 'rgb(239, 68, 68)',
    pulse: 'bg-red-500'
  },
  purple: {
    value: 'text-purple-900 dark:text-purple-100',
    icon: 'text-purple-600 dark:text-purple-400',
    iconBg: 'bg-gradient-to-br from-purple-500/10 to-purple-600/5 dark:from-purple-500/20 dark:to-purple-600/10 ring-1 ring-purple-500/20',
    gradient: 'from-purple-500/5 to-transparent',
    sparkline: 'rgb(168, 85, 247)',
    pulse: 'bg-purple-500'
  }
};

const colorScheme = computed(() => colorMap[props.iconColor] || colorMap.blue);
const valueColorClass = computed(() => colorScheme.value.value);
const iconColorClass = computed(() => colorScheme.value.icon);
const iconBgClass = computed(() => colorScheme.value.iconBg);
const gradientClass = computed(() => colorScheme.value.gradient);
const sparklineColor = computed(() => colorScheme.value.sparkline);
const pulseColorClass = computed(() => colorScheme.value.pulse);

const trendColorClass = computed(() => {
  if (props.trend > 0) return 'text-emerald-600 dark:text-emerald-400';
  if (props.trend < 0) return 'text-red-600 dark:text-red-400';
  return 'text-gray-400 dark:text-gray-500';
});

const trendBgClass = computed(() => {
  if (props.trend > 0) return 'bg-emerald-100/80 dark:bg-emerald-900/30';
  if (props.trend < 0) return 'bg-red-100/80 dark:bg-red-900/30';
  return 'bg-gray-100/80 dark:bg-gray-800/30';
});

// Sparkline calculations
const sparklinePoints = computed(() => {
  if (!props.sparklineData || props.sparklineData.length === 0) return [];
  
  const max = Math.max(...props.sparklineData, 1);
  const min = Math.min(...props.sparklineData, 0);
  const range = max - min || 1;
  
  return props.sparklineData.map((value, index) => ({
    x: index * 4 + 2,
    y: 100 - ((value - min) / range) * 80 - 10
  }));
});

const sparklinePath = computed(() => {
  if (sparklinePoints.value.length === 0) return '';
  
  return sparklinePoints.value.map((point, i) => 
    `${i === 0 ? 'M' : 'L'} ${point.x} ${point.y}`
  ).join(' ');
});

const sparklineAreaPath = computed(() => {
  if (sparklinePoints.value.length === 0) return '';
  
  const points = sparklinePoints.value;
  const path = points.map((point, i) => 
    `${i === 0 ? 'M' : 'L'} ${point.x} ${point.y}`
  ).join(' ');
  
  return `${path} L ${points[points.length - 1].x} 100 L ${points[0].x} 100 Z`;
});
</script>

<style scoped>
.metric-card-v2 {
  backdrop-filter: blur(8px);
}
</style>

