// Chart theme — single source of truth for all Chart.js colors.
//
// Philosophy: monochrome by default. Only these semantic slots are colored:
//   - HEALTHY / COMPLETED / SUCCESS  → green
//   - DLQ / FAILED / ERROR           → pink (logo)
// Everything else (primary/secondary/tertiary series, row-index differentiation,
// "ingested vs processed") uses shades of grey so the eye isn't pulled toward
// anything that isn't a problem.

// Series colors — ordered by use priority.
// Index 0–2 are the three greys (use these for generic N-series charts).
// 3, 4 are reserved for semantic slots (healthy, danger) — use directly by meaning.
export const chartPalette = [
  { line: '#e6e6e6', fill: 'rgba(230,230,230,0.10)' }, // 0: primary series   (was cyan)
  { line: '#8a8a92', fill: 'rgba(138,138,146,0.10)' }, // 1: secondary series (was amber)
  { line: '#6a6a6a', fill: 'rgba(106,106,106,0.10)' }, // 2: tertiary series  (was violet)
  { line: '#4ade80', fill: 'rgba(74,222,128,0.12)'  }, // 3: healthy / ok    (semantic)
  { line: '#fb7185', fill: 'rgba(251,113,133,0.12)' }, // 4: danger / error  (semantic)
]

// Distribution / state color lookup by semantic label.
// Use for doughnut/pie charts where each slice has a named meaning.
// Everything that isn't a clear win/loss gets a grey.
export const stateColor = (label) => {
  const key = String(label || '').toLowerCase()
  if (key.includes('complet') || key === 'ok' || key === 'healthy' || key === 'success' || key === 'stable')
    return { line: '#4ade80', fill: 'rgba(74,222,128,0.75)' }
  if (key.includes('dlq') || key.includes('dead') || key.includes('fail') || key === 'error' || key === 'bad' || key === 'stuck')
    return { line: '#fb7185', fill: 'rgba(251,113,133,0.75)' }
  if (key === 'pending' || key.includes('queue') || key === 'ingested' || key === 'push' || key.includes('produc'))
    return { line: '#e6e6e6', fill: 'rgba(230,230,230,0.80)' }  // primary grey
  if (key === 'processing' || key === 'processed' || key === 'pop' || key.includes('consum'))
    return { line: '#8a8a92', fill: 'rgba(138,138,146,0.80)' }  // secondary grey
  if (key.includes('ack') || key === 'retrying' || key === 'warn' || key === 'lag' || key === 'lagging')
    return { line: '#e6b450', fill: 'rgba(230,180,80,0.75)' }   // muted warn amber
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
