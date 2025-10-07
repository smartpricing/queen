<template>
  <span>{{ displayValue }}</span>
</template>

<script setup>
import { ref, onMounted, watch } from 'vue'

const props = defineProps({
  start: {
    type: Number,
    default: 0
  },
  end: {
    type: Number,
    required: true
  },
  duration: {
    type: Number,
    default: 2 // seconds
  },
  decimals: {
    type: Number,
    default: 0
  },
  separator: {
    type: String,
    default: ','
  }
})

const displayValue = ref(props.start)

function easeOutQuart(t) {
  return 1 - Math.pow(1 - t, 4)
}

function formatNumber(num) {
  const parts = num.toFixed(props.decimals).split('.')
  parts[0] = parts[0].replace(/\B(?=(\d{3})+(?!\d))/g, props.separator)
  return parts.join('.')
}

function animateValue() {
  const startTime = Date.now()
  const startValue = displayValue.value
  const endValue = props.end
  const duration = props.duration * 1000

  function update() {
    const now = Date.now()
    const elapsed = now - startTime
    const progress = Math.min(elapsed / duration, 1)
    const easedProgress = easeOutQuart(progress)
    
    const currentValue = startValue + (endValue - startValue) * easedProgress
    displayValue.value = formatNumber(currentValue)
    
    if (progress < 1) {
      requestAnimationFrame(update)
    } else {
      displayValue.value = formatNumber(endValue)
    }
  }
  
  requestAnimationFrame(update)
}

onMounted(() => {
  animateValue()
})

watch(() => props.end, () => {
  animateValue()
})
</script>
