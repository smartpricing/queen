// Chart theme — single source of truth for all Chart.js colors.
//
// Philosophy: monochrome by default. Red / green / yellow are reserved for
// status: error / failure / DLQ → red, healthy / success / completed →
// green, warning / threshold-breach → yellow. They are NEVER assigned by
// series index, only by data semantics. Charts that just need to tell N
// series apart use the grey ramp below; the legend / tooltip carries the
// rest of the meaning.

// Cycling series palette — all greys, no semantic colors. Use chartColor(i)
// or chartColors[i] for generic N-series charts where index has no health
// meaning (per-replica, per-queue, push/pop/ack split, etc).
export const chartPalette = [
  { line: '#e6e6e6', fill: 'rgba(230,230,230,0.10)' }, // 0: primary
  { line: '#8a8a92', fill: 'rgba(138,138,146,0.10)' }, // 1: secondary
  { line: '#6a6a6a', fill: 'rgba(106,106,106,0.10)' }, // 2: tertiary
  { line: '#b8b8b8', fill: 'rgba(184,184,184,0.10)' }, // 3: lighter
  { line: '#4a4a4f', fill: 'rgba(74,74,79,0.18)'    }, // 4: darker
]

// Semantic colors — call by name when the data itself carries a meaning.
// Do not cycle through these; pick the one that matches what the series
// represents (see stateColor() for label-driven lookup).
export const semanticColors = {
  ok:    { line: '#4ade80', fill: 'rgba(74,222,128,0.12)'  }, // healthy / success
  warn:  { line: '#e6b450', fill: 'rgba(230,180,80,0.12)'  }, // warning / lag
  bad:   { line: '#fb7185', fill: 'rgba(251,113,133,0.12)' }, // error / DLQ
  badStrong: { line: '#f43f5e', fill: 'rgba(244,63,94,0.18)' }, // hard error (db errors)
}

// Distribution / state color lookup by semantic label.
// Use for doughnut/pie charts where each slice has a named meaning.
// Only labels that genuinely encode status get a colored slot; neutral
// operations (push, pop, ack, ingested, processed, retention, …) all get
// greys so the eye is only drawn to actual problems.
export const stateColor = (label) => {
  const key = String(label || '').toLowerCase()
  if (key.includes('complet') || key === 'ok' || key === 'healthy' || key === 'success' || key === 'stable')
    return { line: '#4ade80', fill: 'rgba(74,222,128,0.75)' }
  if (key.includes('dlq') || key.includes('dead') || key.includes('fail') || key === 'error' || key === 'bad' || key === 'stuck')
    return { line: '#fb7185', fill: 'rgba(251,113,133,0.75)' }
  if (key === 'warn' || key === 'warning' || key === 'lag' || key === 'lagging' || key === 'evicted' || key === 'eviction')
    return { line: '#e6b450', fill: 'rgba(230,180,80,0.75)' }
  if (key === 'pending' || key.includes('queue') || key === 'ingested' || key === 'push' || key.includes('produc'))
    return { line: '#e6e6e6', fill: 'rgba(230,230,230,0.80)' }  // primary grey
  if (key === 'processing' || key === 'processed' || key === 'pop' || key.includes('consum'))
    return { line: '#8a8a92', fill: 'rgba(138,138,146,0.80)' }  // secondary grey
  return { line: '#6a6a6a', fill: 'rgba(106,106,106,0.75)' }    // fallback tertiary grey
}

// Convenience accessors used throughout views / Chart.js configs.
export const chartColor = (i) => chartPalette[i % chartPalette.length]

// Grid / tick / tooltip / axis colors for Chart.js options — match the new
// Cursor palette tokens used in style.css.
export const chartTheme = {
  grid:         '#26262a',
  tick:         '#6a6a6a',
  axisTitle:    '#6a6a6a',
  tooltipBg:    '#232325',
  tooltipBorder:'#35353a',
  tooltipText:  '#e6e6e6',
  tooltipBody:  '#9a9a9a',
  fontFamily:   "'JetBrains Mono', ui-monospace, monospace",
  fontFamilyUI: "'Inter', ui-sans-serif, system-ui, sans-serif",
}

// Shortcut palettes for common chart shapes (keep legacy call sites happy).
export const chartColors = chartPalette.map((c) => c.line)   // ['#e6e6e6', '#8a8a92', …]
export const chartFills  = chartPalette.map((c) => c.fill)

// Helper: build a backgroundColor array for bar/doughnut series by label.
export const seriesBackgrounds = (labels, variant = 'fill') =>
  labels.map((l) => stateColor(l)[variant])

export const seriesBorders = (labels) =>
  labels.map((l) => stateColor(l).line)
