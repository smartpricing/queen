<template>
  <svg
    class="sparkline"
    :class="`sparkline-${tone}`"
    :viewBox="`0 0 ${w} ${h}`"
    width="100%"
    :height="h"
    preserveAspectRatio="none"
    role="img"
    :aria-label="ariaLabel"
  >
    <template v-if="hasData">
      <path v-if="fill" class="sparkline-area" :d="areaPath" />
      <path class="sparkline-line" :d="linePath" />
      <circle
        v-if="lastPoint"
        class="sparkline-end"
        :cx="lastPoint[0]"
        :cy="lastPoint[1]"
        r="1.6"
      />
    </template>
    <template v-else-if="hasAnyData">
      <!-- All-zero across window — render a flat baseline at the bottom so
           the row reads as "quiet, no events" rather than "no data". -->
      <line
        class="sparkline-flat"
        :x1="0" :y1="h - 2" :x2="w" :y2="h - 2"
      />
    </template>
    <line
      v-else
      class="sparkline-empty"
      :x1="0" :y1="h / 2" :x2="w" :y2="h / 2"
    />
  </svg>
</template>

<script setup>
import { computed } from 'vue'

// Compact inline SVG sparkline. No axes, no labels, no grid — just shape.
// Severity tone colors the line/area; use 'mute' as the default. The end
// dot anchors the eye on "now" so the row reads left-to-right.
const props = defineProps({
  data: { type: Array, default: () => [] },
  // Severity tone drives line color via CSS class, matching .num.warn etc.
  tone: { type: String, default: 'mute' }, // 'mute' | 'ok' | 'warn' | 'bad'
  fill: { type: Boolean, default: true },
  width: { type: Number, default: 120 },
  height: { type: Number, default: 26 },
  ariaLabel: { type: String, default: 'trend' },
})

const w = props.width
const h = props.height

const cleaned = computed(() =>
  (props.data || []).map(v => Number(v)).filter(v => Number.isFinite(v))
)

const hasAnyData = computed(() => cleaned.value.length >= 2)
const hasData = computed(() => {
  const d = cleaned.value
  if (d.length < 2) return false
  return d.some(v => v !== 0)
})

const points = computed(() => {
  const d = cleaned.value
  if (d.length < 2) return []
  const min = Math.min(...d, 0)
  const max = Math.max(...d, min + 1e-9)
  const range = max - min || 1
  const stepX = w / (d.length - 1)
  return d.map((v, i) => {
    const x = i * stepX
    const y = h - ((v - min) / range) * (h - 3) - 1.5
    return [x, y]
  })
})

const linePath = computed(() => {
  const pts = points.value
  if (!pts.length) return ''
  return pts
    .map((p, i) => `${i === 0 ? 'M' : 'L'} ${p[0].toFixed(2)} ${p[1].toFixed(2)}`)
    .join(' ')
})

const areaPath = computed(() => {
  const pts = points.value
  if (!pts.length) return ''
  const first = pts[0]
  const last = pts[pts.length - 1]
  return (
    `M ${first[0].toFixed(2)} ${h} ` +
    pts.map(p => `L ${p[0].toFixed(2)} ${p[1].toFixed(2)}`).join(' ') +
    ` L ${last[0].toFixed(2)} ${h} Z`
  )
})

const lastPoint = computed(() => {
  const pts = points.value
  return pts.length ? pts[pts.length - 1] : null
})
</script>

<style scoped>
.sparkline {
  display: block;
  overflow: visible;
  /* The SVG stretches horizontally (preserveAspectRatio="none") so we must
     keep stroke widths from scaling with the X axis — without this, a
     wide row would render a chunky 3px line. vector-effect on each
     stroked element decouples stroke width from the SVG transform. */
}
.sparkline-line {
  fill: none;
  stroke-width: 1.25;
  stroke-linejoin: round;
  stroke-linecap: round;
  vector-effect: non-scaling-stroke;
}
.sparkline-area { stroke: none; }
.sparkline-end { stroke: none; }
.sparkline-flat {
  stroke: var(--text-low);
  stroke-width: 1;
  opacity: .35;
  vector-effect: non-scaling-stroke;
}
.sparkline-empty {
  stroke: var(--text-low);
  stroke-width: 1;
  stroke-dasharray: 2 3;
  opacity: .35;
  vector-effect: non-scaling-stroke;
}

.sparkline-mute .sparkline-line { stroke: var(--text-mid); opacity: .85; }
.sparkline-mute .sparkline-area { fill: var(--text-mid); opacity: .10; }
.sparkline-mute .sparkline-end  { fill: var(--text-hi); }

.sparkline-ok .sparkline-line { stroke: var(--ok-500); }
.sparkline-ok .sparkline-area { fill: var(--ok-500); opacity: .12; }
.sparkline-ok .sparkline-end  { fill: var(--ok-500); }

.sparkline-warn .sparkline-line { stroke: var(--warn-400); }
.sparkline-warn .sparkline-area { fill: var(--warn-400); opacity: .14; }
.sparkline-warn .sparkline-end  { fill: var(--warn-400); }

.sparkline-bad .sparkline-line { stroke: var(--ember-400); }
.sparkline-bad .sparkline-area { fill: var(--ember-400); opacity: .14; }
.sparkline-bad .sparkline-end  { fill: var(--ember-400); }
</style>
