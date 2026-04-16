<template>
  <div class="stat" :class="{ 'cursor-pointer': clickable }" @click="$emit('click')">
    <div class="stat-label">
      <component v-if="icon" :is="icon" class="w-3 h-3" />
      {{ label }}
    </div>

    <div v-if="loading" class="skeleton" style="width:80px; height:36px; margin-top:8px; border-radius:6px;" />
    <div v-else class="stat-value">
      {{ formattedValue }}<small v-if="unit">{{ unit }}</small>
    </div>

    <div class="stat-foot" v-if="subtext || trend !== null">
      <span v-if="trend !== null" class="trend" :class="trend >= 0 ? 'trend-up' : (trend < 0 ? 'trend-down' : 'trend-flat')">
        {{ trend >= 0 ? '▲' : '▼' }} {{ Math.abs(trend) }}%
      </span>
      <span v-if="subtext">{{ subtext }}</span>
    </div>

    <!-- Icon badge (top-right) -->
    <div v-if="icon && !loading" class="stat-icon-badge" :class="iconColorClass">
      <component :is="icon" class="w-5 h-5" />
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import { formatNumber } from '@/composables/useApi'

const props = defineProps({
  label: { type: String, required: true },
  value: { type: [Number, String], default: 0 },
  format: { type: String, default: 'number' },
  unit: { type: String, default: null },
  subtext: { type: String, default: null },
  icon: { type: [Object, Function], default: null },
  iconColor: { type: String, default: 'ice' },
  trend: { type: Number, default: null },
  loading: { type: Boolean, default: false },
  clickable: { type: Boolean, default: false },
})

defineEmits(['click'])

const formattedValue = computed(() => {
  if (props.value === null || props.value === undefined) return '-'
  if (props.format === 'raw') return props.value
  return formatNumber(props.value)
})

const iconColorClass = computed(() => {
  const map = {
    ice: 'badge-ice',
    crown: 'badge-crown',
    ember: 'badge-ember',
  }
  return map[props.iconColor] || 'badge-ice'
})
</script>

<style scoped>
.stat-icon-badge {
  position: absolute; top: 16px; right: 16px;
  width: 40px; height: 40px; border-radius: 10px;
  display: grid; place-items: center;
}
.badge-ice   { background: rgba(34,211,238,.12); color: #22d3ee; }
.badge-crown { background: rgba(251,191,36,.12); color: #fbbf24; }
.badge-ember { background: rgba(244,63,94,.12);  color: #fb7185; }
</style>
