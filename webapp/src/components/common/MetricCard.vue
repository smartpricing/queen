<template>
  <div class="metric-card animate-fade-in">
    <p class="metric-label">{{ title }}</p>
    <p class="metric-value">{{ formattedValue }}</p>
    <p v-if="subtitle || (secondaryValue !== undefined && secondaryValue !== null)" class="text-xs text-gray-500 dark:text-gray-400 mt-1.5">
      <span v-if="subtitle">{{ subtitle }}</span>
      <span v-else>
        {{ secondaryLabel }}: <span class="font-semibold">{{ formattedSecondary }}</span>
      </span>
    </p>
  </div>
</template>

<script setup>
import { computed } from 'vue';
import { formatNumber } from '../../utils/formatters';

const props = defineProps({
  title: String,
  value: [String, Number],
  subtitle: String,
  secondaryValue: [String, Number],
  secondaryLabel: String,
});

const formattedValue = computed(() => {
  if (typeof props.value === 'number') {
    return formatNumber(props.value);
  }
  return props.value;
});

const formattedSecondary = computed(() => {
  if (props.secondaryValue !== undefined && props.secondaryValue !== null) {
    if (typeof props.secondaryValue === 'number') {
      return formatNumber(props.secondaryValue);
    }
    return props.secondaryValue;
  }
  return '';
});
</script>

