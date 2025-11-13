# Color Customization Guide

## Quick Color Change

The entire app uses a **centralized color system**. To change the color scheme, edit **ONE FILE**:

### **File:** `src/utils/colors.js`

```javascript
export const colors = {
  // PRIMARY COLORS - Change these!
  primary: {
    name: 'Emerald',
    hex: '#059669',      // Change to your color
    rgb: 'rgb(5, 150, 105)',
    rgba: (opacity) => `rgba(5, 150, 105, ${opacity})`,
  },
  secondary: {
    name: 'Jade', 
    hex: '#0d9488',      // Change to your color
    rgb: 'rgb(13, 148, 136)',
    rgba: (opacity) => `rgba(13, 148, 136, ${opacity})`,
  },
  // ... rest of config
};
```

## Current Color Scheme

**Theme:** Sophisticated Emerald Green (matching documentation website)  
**Design Philosophy:** Natural, Elegant, Regal

**Primary (Charts - Ingested):** Emerald (#059669)  
**Secondary (Charts - Processed):** Jade (#0d9488)

### Color Palette

| Color | Hex | RGB | Usage |
|-------|-----|-----|-------|
| Rich Emerald | #059669 | rgb(5, 150, 105) | Primary brand, buttons, accents |
| Jade | #0d9488 | rgb(13, 148, 136) | Secondary accent, charts |
| Forest Green | #047857 | rgb(4, 120, 87) | Hover states, depth |
| Spring Green | #10b981 | rgb(16, 185, 129) | Success states |
| Light Emerald | #34d399 | rgb(52, 211, 153) | Dark mode highlights |

## Where Colors Are Used

### **Primary (Emerald)**
- Primary buttons and CTAs
- Dashboard throughput chart - "Ingested" line
- Analytics message flow - "Ingested" line
- Info badges and cards
- Focus rings and selection
- Active sidebar items
- Link accents
- Table hover highlights

### **Secondary (Jade)**
- Dashboard throughput chart - "Processed" line
- Analytics message flow - "Processed" line
- Top queues bar chart
- Secondary accent elements
- Gradients (primary → secondary)

### **Status Colors** (Standard - Keep for Clarity)
- Success/Completed: Green (#10b981)
- Warning/Pending: Yellow (#f59e0b)
- Danger/Failed: Red (#ef4444)
- Dead Letter: Gray (#9ca3af)

## Files Updated for Theme

1. ✅ `src/utils/colors.js` - Centralized color config
2. ✅ `tailwind.config.js` - Tailwind theme colors
3. ✅ `src/assets/styles/main.css` - Component styles
4. ✅ `src/assets/styles/professional.css` - Page styles

## How to Change to Different Colors

### Example: Switch to Blue & Cyan

```javascript
// In src/utils/colors.js
primary: {
  name: 'Blue',
  hex: '#3b82f6',
  rgb: 'rgb(59, 130, 246)',
  rgba: (opacity) => `rgba(59, 130, 246, ${opacity})`,
},
secondary: {
  name: 'Cyan', 
  hex: '#06b6d4',
  rgb: 'rgb(6, 182, 212)',
  rgba: (opacity) => `rgba(6, 182, 212, ${opacity})`,
},
```

### Example: Switch to Orange & Pink

```javascript
primary: {
  name: 'Orange',
  hex: '#f97316',
  rgb: 'rgb(249, 115, 22)',
  rgba: (opacity) => `rgba(249, 115, 22, ${opacity})`,
},
secondary: {
  name: 'Pink', 
  hex: '#ec4899',
  rgb: 'rgb(236, 72, 153)',
  rgba: (opacity) => `rgba(236, 72, 153, ${opacity})`,
},
```

### Example: Switch to Purple & Violet

```javascript
primary: {
  name: 'Purple',
  hex: '#a855f7',
  rgb: 'rgb(168, 85, 247)',
  rgba: (opacity) => `rgba(168, 85, 247, ${opacity})`,
},
secondary: {
  name: 'Violet', 
  hex: '#8b5cf6',
  rgb: 'rgb(139, 92, 246)',
  rgba: (opacity) => `rgba(139, 92, 246, ${opacity})`,
},
```

## After Changing Colors

Also update `tailwind.config.js` to match:

```javascript
colors: {
  'queen-primary': '#your-primary-color',
  'queen-secondary': '#your-secondary-color',
  'primary': '#your-primary-color',
  'secondary': '#your-secondary-color',
},
```

## Benefits of Centralized System

✅ **Single source of truth**: Change one file, update entire app  
✅ **Consistent**: All charts and components use same colors  
✅ **Type-safe**: Helper functions for opacity variations  
✅ **Easy maintenance**: No scattered color definitions  
✅ **Hot reload**: Changes apply immediately in dev mode  

## Current Theme Match

The webapp now uses the **same emerald green theme** as the documentation website for a cohesive brand experience across:
- Documentation site: https://smartpricing.github.io/queen/
- Web dashboard: http://localhost:6632/

Both use the sophisticated emerald green palette inspired by natural, regal colors.

Change `src/utils/colors.js` and all components update automatically! 🎨
