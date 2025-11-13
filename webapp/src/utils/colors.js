// Centralized color configuration for the entire app
// Emerald green theme matching the documentation website
// Design philosophy: Natural, elegant, regal

export const colors = {
  // Primary brand colors - Sophisticated emerald green palette
  primary: {
    name: 'Emerald',
    hex: '#059669',      // emerald-600 - Rich emerald
    rgb: 'rgb(5, 150, 105)',
    rgba: (opacity) => `rgba(5, 150, 105, ${opacity})`,
  },
  secondary: {
    name: 'Jade', 
    hex: '#0d9488',      // teal-600 - Jade accent
    rgb: 'rgb(13, 148, 136)',
    rgba: (opacity) => `rgba(13, 148, 136, ${opacity})`,
  },
  
  // Status colors (keep standard for clarity)
  success: {
    hex: '#10b981',      // green-500
    rgb: 'rgb(16, 185, 129)',
    rgba: (opacity) => `rgba(16, 185, 129, ${opacity})`,
  },
  warning: {
    hex: '#f59e0b',      // yellow-500
    rgb: 'rgb(245, 158, 11)',
    rgba: (opacity) => `rgba(245, 158, 11, ${opacity})`,
  },
  danger: {
    hex: '#ef4444',      // red-500
    rgb: 'rgb(239, 68, 68)',
    rgba: (opacity) => `rgba(239, 68, 68, ${opacity})`,
  },
  info: {
    hex: '#059669',      // emerald-600 - matches primary
    rgb: 'rgb(5, 150, 105)',
    rgba: (opacity) => `rgba(5, 150, 105, ${opacity})`,
  },
  
  // Chart colors - Emerald green palette
  charts: {
    // Main data series - emerald and jade tones
    ingested: {
      border: 'rgb(5, 150, 105)',           // Emerald
      background: 'rgba(5, 150, 105, 0.1)',
    },
    processed: {
      border: 'rgb(13, 148, 136)',          // Jade
      background: 'rgba(13, 148, 136, 0.1)',
    },
    
    // Status distribution (keep standard colors for clarity)
    pending: {
      bg: 'rgba(245, 158, 11, 0.8)',        // Yellow
      border: 'rgb(245, 158, 11)',
    },
    processing: {
      bg: 'rgba(5, 150, 105, 0.8)',         // Emerald
      border: 'rgb(5, 150, 105)',
    },
    completed: {
      bg: 'rgba(16, 185, 129, 0.8)',        // Green
      border: 'rgb(16, 185, 129)',
    },
    failed: {
      bg: 'rgba(239, 68, 68, 0.8)',         // Red
      border: 'rgb(239, 68, 68)',
    },
    deadLetter: {
      bg: 'rgba(156, 163, 175, 0.8)',       // Gray
      border: 'rgb(156, 163, 175)',
    },
  },
};

// Helper to get all chart colors for status distribution
export function getStatusColors() {
  return {
    backgrounds: [
      colors.charts.pending.bg,
      colors.charts.processing.bg,
      colors.charts.completed.bg,
      colors.charts.failed.bg,
      colors.charts.deadLetter.bg,
    ],
    borders: [
      colors.charts.pending.border,
      colors.charts.processing.border,
      colors.charts.completed.border,
      colors.charts.failed.border,
      colors.charts.deadLetter.border,
    ],
  };
}
