<template>
  <span class="metric-value">{{ displayValue }}</span>
</template>

<script setup>
import { ref, watch, onMounted } from 'vue';

const props = defineProps({
  value: {
    type: [Number, String],
    required: true
  },
  duration: {
    type: Number,
    default: 800
  }
});

const displayValue = ref(formatValue(props.value));
const numericValue = ref(parseNumericValue(props.value));

function parseNumericValue(value) {
  if (typeof value === 'number') return value;
  const parsed = parseFloat(String(value).replace(/[^0-9.-]/g, ''));
  return isNaN(parsed) ? 0 : parsed;
}

function formatValue(value) {
  if (typeof value === 'number') {
    const absValue = Math.abs(value);
    if (absValue >= 1000000000) {
      return (value / 1000000000).toFixed(1) + 'B';
    } else if (absValue >= 1000000) {
      return (value / 1000000).toFixed(1) + 'M';
    } else if (absValue >= 10000) {
      return (value / 1000).toFixed(1) + 'K';
    } else {
      return value.toLocaleString();
    }
  }
  return value;
}

// Animation disabled for better performance
watch(() => props.value, (newVal) => {
  displayValue.value = formatValue(newVal);
});

onMounted(() => {
  // No animation - instant display
  displayValue.value = formatValue(props.value);
});
</script>

