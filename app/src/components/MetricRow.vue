<template>
  <!--
    Outer wrap owns the bottom border and the optional expansion panel,
    so the grid row above stays a clean 6-column flex unit.
  -->
  <div class="mr-wrap" :class="{ 'mr-wrap-expanded': expanded }">
    <div
      class="mr"
      :class="{ 'mr-clickable': clickable, 'mr-loading': loading, [`mr-sev-${severity}`]: severity }"
      @click="onRowClick"
      :title="tooltip || undefined"
    >
      <span class="mr-dot" :class="dotClass" />

      <span class="mr-label">{{ label }}</span>

      <span class="mr-value">
        <slot name="value">
          <template v-if="loading">
            <span class="skeleton" style="display:inline-block; width:60px; height:14px; vertical-align:middle;" />
          </template>
          <template v-else>
            <span class="num" :class="severity">{{ formattedValue }}</span><i v-if="unit" class="mr-unit">{{ unit }}</i>
          </template>
        </slot>
      </span>

      <span class="mr-context">
        <slot name="context">{{ context }}</slot>
      </span>

      <span class="mr-spark">
        <RowChart
          :data="sparkline"
          :series="series"
          :labels="labels"
          :tone="sparkTone"
          :value-format="valueFormat"
          variant="compact"
        />
      </span>

      <button
        class="mr-expand-toggle"
        :class="{ 'is-expanded': expanded }"
        :aria-label="expanded ? 'Collapse chart' : 'Expand chart'"
        :title="expanded ? 'Collapse chart' : 'Expand chart'"
        @click.stop="$emit('toggle-expand')"
      >
        <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
          <path d="M4 6l4 4 4-4" />
        </svg>
      </button>
    </div>

    <!--
      Expansion panel — shown when `expanded === true`. The full-variant
      RowChart renders the SAME data as the compact one above, but with
      visible axes, gridlines, legend, and a y-axis title (when `unit` is
      provided). Clicking the chevron above toggles this panel.
    -->
    <div v-if="expanded" class="mr-expand">
      <RowChart
        :data="sparkline"
        :series="series"
        :labels="labels"
        :tone="sparkTone"
        :value-format="valueFormat"
        :unit="expandUnit"
        variant="full"
      />
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import RowChart from './RowChart.vue'

const props = defineProps({
  label: { type: String, required: true },
  value: { type: [String, Number], default: '' },
  unit: { type: String, default: '' },
  context: { type: String, default: '' },
  // Single-series shorthand. Used when `series` is not provided.
  sparkline: { type: Array, default: () => [] },
  // Multi-series. When given, RowChart renders one line per entry and
  // the tooltip shows all values at the hovered timestamp.
  // Shape: [{ label, data, color? }]
  series: { type: Array, default: null },
  // Time-axis labels (one per data point) used by the tooltip title.
  labels: { type: Array, default: () => [] },
  // Optional formatter for tooltip values, e.g. v => v.toFixed(1) + ' /s'.
  valueFormat: { type: Function, default: null },
  // Optional y-axis title for the expanded chart (defaults to `unit`).
  expandUnit: { type: String, default: '' },
  // 'mute' (default) | 'ok' | 'warn' | 'bad' for the value + dot
  severity: { type: String, default: '' },
  // Optional override; falls back to severity tone.
  sparklineTone: { type: String, default: '' },
  loading: { type: Boolean, default: false },
  clickable: { type: Boolean, default: false },
  tooltip: { type: String, default: '' },
  // Controlled-component pattern: parent owns expand state so the
  // dashboard can implement Expand all / Collapse all without each
  // row having to know about its siblings.
  expanded: { type: Boolean, default: false },
})

const emit = defineEmits(['click', 'toggle-expand'])

const dotClass = computed(() => {
  if (props.severity === 'bad')  return 'status-dot-danger'
  if (props.severity === 'warn') return 'status-dot-warning'
  if (props.severity === 'ok')   return 'status-dot-success'
  return 'mr-dot-mute'
})

const sparkTone = computed(() => props.sparklineTone || (props.severity || 'mute'))

const formattedValue = computed(() => {
  const v = props.value
  if (v === null || v === undefined || v === '') return '—'
  return v
})

const onRowClick = () => {
  if (props.clickable) emit('click')
}
</script>

<style scoped>
/* Outer wrap — owns the bottom border so the grid row stays a clean
   single-purpose flex unit, and so the expansion panel can sit beneath
   the row visually as part of the same "logical row". */
.mr-wrap {
  border-bottom: 1px solid var(--bd);
}
.mr-wrap:last-child { border-bottom: none; }
:global(.dark) .mr-wrap-expanded { background: rgba(255, 255, 255, .015); }
:global(.light) .mr-wrap-expanded { background: rgba(10, 10, 10, .015); }

.mr {
  display: grid;
  /* dot · label · value · context · spark · expand-toggle.
     The 24px trailing column hosts the chevron button — small but
     always present so the affordance is discoverable. */
  grid-template-columns: 14px 150px 160px 1fr 260px 24px;
  align-items: center;
  gap: 16px;
  padding: 10px 16px;
}

.mr-clickable {
  cursor: pointer;
  transition: background .12s var(--ease);
}
:global(.dark) .mr-clickable:hover { background: rgba(255, 255, 255, .025); }
:global(.light) .mr-clickable:hover { background: rgba(10, 10, 10, .025); }

.mr-dot {
  width: 6px;
  height: 6px;
  border-radius: 99px;
  justify-self: center;
}
.mr-dot-mute { background: var(--text-low); opacity: .45; }

.mr-label {
  font-size: 12.5px;
  font-weight: 500;
  color: var(--text-mid);
  letter-spacing: -.005em;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.mr-value {
  font-family: 'JetBrains Mono', monospace;
  font-size: 14px;
  font-variant-numeric: tabular-nums;
  text-align: right;
  white-space: nowrap;
  color: var(--text-hi);
}
.mr-unit {
  font-style: normal;
  color: var(--text-low);
  margin-left: 4px;
  font-size: 11px;
  font-weight: 400;
}

.mr-context {
  font-family: 'JetBrains Mono', monospace;
  font-size: 11.5px;
  color: var(--text-low);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.mr-spark {
  display: block;
  width: 100%;
}

/* Chevron toggle — small, muted at rest, brightens on hover. Rotates
   180° when its row is expanded so the affordance reads "more below"
   collapsed and "less below" expanded. */
.mr-expand-toggle {
  width: 24px;
  height: 24px;
  padding: 0;
  background: transparent;
  border: none;
  border-radius: 4px;
  color: var(--text-low);
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  transition: color .12s var(--ease), background .12s var(--ease);
}
.mr-expand-toggle:hover { color: var(--text-hi); background: var(--ink-3); }
.mr-expand-toggle svg { transition: transform .15s var(--ease); }
.mr-expand-toggle.is-expanded svg { transform: rotate(180deg); }

.mr-expand {
  /* Indent under the label column so the expanded chart visually
     aligns with the metric it belongs to, with a thin top divider so
     the row and its expansion read as a single unit. */
  padding: 6px 16px 16px 32px;
  border-top: 1px dashed var(--bd);
}

@media (max-width: 1100px) {
  .mr {
    grid-template-columns: 14px 140px 140px 1fr 180px 24px;
    gap: 12px;
  }
}
@media (max-width: 900px) {
  .mr {
    grid-template-columns: 14px 1fr auto 110px 24px;
    row-gap: 4px;
  }
  .mr-context { display: none; }
  .mr-expand { padding: 6px 12px 14px; }
}
</style>
