# Color Customization Guide

## Quick Color Change

To change the entire app's color scheme, edit **ONE FILE**:

### **File:** `src/utils/colors.js`

```javascript
export const colors = {
  // PRIMARY COLORS - Change these!
  primary: {
    name: 'Rose',
    hex: '#f43f5e',      // Change to your color
    rgb: 'rgb(244, 63, 94)',
    rgba: (opacity) => `rgba(244, 63, 94, ${opacity})`,
  },
  secondary: {
    name: 'Purple', 
    hex: '#a855f7',      // Change to your color
    rgb: 'rgb(168, 85, 247)',
    rgba: (opacity) => `rgba(168, 85, 247, ${opacity})`,
  },
  // ... rest of config
};
```

## Current Color Scheme

**Primary (Charts - Ingested):** Rose (#f43f5e)  
**Secondary (Charts - Processed):** Purple (#a855f7)

## Where Colors Are Used

### **Primary (Rose)**
- Dashboard throughput chart - "Ingested" line
- Analytics message flow - "Ingested" line
- Accent colors

### **Secondary (Purple)**
- Dashboard throughput chart - "Processed" line
- Analytics message flow - "Processed" line
- Top queues bar chart
- Accent elements

### **Status Colors** (Fixed)
- Success/Completed: Green
- Warning/Pending: Yellow
- Danger/Failed: Red
- Info: Blue
- Dead Letter: Gray

## How to Change

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

## Tailwind Config

Also update `tailwind.config.js` for consistency:

```javascript
colors: {
  'queen-primary': '#f43f5e',    // Match primary
  'queen-secondary': '#a855f7',  // Match secondary
  'queen-accent': '#ec4899',     // Optional third color
},
```

## Current Implementation

✅ **Centralized**: All chart colors in one place  
✅ **Easy to change**: Edit one file  
✅ **Consistent**: Used across all charts  
✅ **Type-safe**: Helper functions for opacity  

## Files Using Color System

1. `src/components/dashboard/ThroughputChart.vue`
2. `src/components/analytics/MessageFlowChart.vue`
3. `src/components/analytics/TopQueuesChart.vue`
4. `src/components/analytics/MessageDistributionChart.vue`
5. `tailwind.config.js` (for CSS utilities)

Change `src/utils/colors.js` and all charts update automatically! 🎨

