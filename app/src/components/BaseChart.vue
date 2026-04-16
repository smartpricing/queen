<template>
  <div ref="container" :style="{ height: height || '280px' }" class="relative">
    <canvas ref="canvas" />
  </div>
</template>

<script setup>
import { ref, watch, onMounted, onUnmounted, nextTick } from 'vue'
import { useTheme } from '@/composables/useTheme'
import {
  Chart, LineController, BarController, DoughnutController,
  CategoryScale, LinearScale, PointElement, LineElement,
  BarElement, ArcElement, Filler, Tooltip, Legend
} from 'chart.js'

Chart.register(
  LineController, BarController, DoughnutController,
  CategoryScale, LinearScale, PointElement, LineElement,
  BarElement, ArcElement, Filler, Tooltip, Legend
)

const props = defineProps({
  type: { type: String, default: 'line' },
  data: { type: Object, required: true },
  options: { type: Object, default: () => ({}) },
  height: { type: String, default: '280px' },
})

const { isDark } = useTheme()
const canvas = ref(null)
const container = ref(null)
let chart = null

const COLORS = {
  ice: { line: '#22d3ee', fill: 'rgba(34,211,238,0.15)' },
  crown: { line: '#fbbf24', fill: 'rgba(251,191,36,0.12)' },
  ember: { line: '#f43f5e', fill: 'rgba(244,63,94,0.12)' },
  ok: { line: '#34d399', fill: 'rgba(52,211,153,0.12)' },
}
const PALETTE = [COLORS.ice, COLORS.crown, COLORS.ember, COLORS.ok]

function buildChartData(data) {
  const datasets = (data.datasets || []).map((ds, i) => {
    const c = PALETTE[i % PALETTE.length]
    return {
      ...ds,
      borderColor: ds.borderColor || c.line,
      backgroundColor: ds.fill ? c.fill : (ds.backgroundColor || c.fill),
      borderWidth: ds.borderWidth || 1.6,
      pointRadius: 0,
      pointHoverRadius: 4,
      pointHoverBackgroundColor: ds.borderColor || c.line,
      tension: 0.4,
      fill: ds.fill !== false,
    }
  })
  return { labels: data.labels, datasets }
}

function getThemeOptions() {
  const gridColor = isDark.value ? 'rgba(255,255,255,0.04)' : 'rgba(10,10,10,0.06)'
  const tickColor = isDark.value ? '#6a6a62' : '#8a8a86'
  const tooltipBg = isDark.value ? '#17171e' : '#fff'
  const tooltipText = isDark.value ? '#f5f5f1' : '#0a0a0a'
  const tooltipBorder = isDark.value ? 'rgba(255,255,255,0.08)' : 'rgba(10,10,10,0.08)'

  return {
    responsive: true,
    maintainAspectRatio: false,
    interaction: { mode: 'index', intersect: false },
    plugins: {
      legend: { display: false },
      tooltip: {
        backgroundColor: tooltipBg,
        titleColor: tooltipText,
        bodyColor: tickColor,
        borderColor: tooltipBorder,
        borderWidth: 1,
        cornerRadius: 8,
        padding: { top: 8, bottom: 8, left: 12, right: 12 },
        titleFont: { family: 'Inter', size: 12, weight: 500 },
        bodyFont: { family: 'JetBrains Mono', size: 11 },
        displayColors: true,
        boxWidth: 8, boxHeight: 8, boxPadding: 4,
      },
    },
    scales: props.type === 'line' || props.type === 'bar' ? {
      x: {
        grid: { color: gridColor, drawBorder: false },
        ticks: { color: tickColor, font: { family: 'JetBrains Mono', size: 10 }, maxRotation: 0, autoSkipPadding: 20 },
        border: { display: false },
      },
      y: {
        beginAtZero: true,
        min: 0,
        grid: { color: gridColor, drawBorder: false },
        ticks: { color: tickColor, font: { family: 'JetBrains Mono', size: 10 }, padding: 8 },
        border: { display: false },
        ...(props.options?.scales?.y || {}),
      },
    } : undefined,
  }
}

function createChart() {
  if (!canvas.value) return
  if (chart) { chart.destroy(); chart = null }

  const ctx = canvas.value.getContext('2d')
  const mergedOpts = { ...getThemeOptions() }
  if (props.options?.plugins) mergedOpts.plugins = { ...mergedOpts.plugins, ...props.options.plugins }
  if (props.options?.scales?.y?.title) {
    mergedOpts.scales.y.title = { ...props.options.scales.y.title, color: isDark.value ? '#6a6a62' : '#8a8a86' }
  }

  chart = new Chart(ctx, {
    type: props.type,
    data: buildChartData(props.data),
    options: mergedOpts,
  })
}

watch(() => props.data, () => {
  if (chart) {
    chart.data = buildChartData(props.data)
    chart.update('none')
  } else {
    createChart()
  }
}, { deep: true })

watch(isDark, () => { createChart() })

onMounted(() => { nextTick(createChart) })
onUnmounted(() => { if (chart) { chart.destroy(); chart = null } })
</script>
