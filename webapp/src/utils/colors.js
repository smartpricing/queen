// Centralized color configuration for the entire app
// Change these values to update the color scheme everywhere

export const colors = {
  // Primary brand colors (used for charts, accents, etc.)
  primary: {
    name: 'Rose',
    hex: '#f43f5e',      // rose-500
    rgb: 'rgb(244, 63, 94)',
    rgba: (opacity) => `rgba(244, 63, 94, ${opacity})`,
  },
  secondary: {
    name: 'Purple', 
    hex: '#a855f7',      // purple-500
    rgb: 'rgb(168, 85, 247)',
    rgba: (opacity) => `rgba(168, 85, 247, ${opacity})`,
  },
  
  // Status colors
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
    hex: '#3b82f6',      // blue-500
    rgb: 'rgb(59, 130, 246)',
    rgba: (opacity) => `rgba(59, 130, 246, ${opacity})`,
  },
  
  // Chart colors (for consistency)
  charts: {
    // Main data series
    ingested: {
      border: 'rgb(244, 63, 94)',      // Rose
      background: 'rgba(244, 63, 94, 0.1)',
    },
    processed: {
      border: 'rgb(168, 85, 247)',     // Purple
      background: 'rgba(168, 85, 247, 0.1)',
    },
    
    // Status distribution
    pending: {
      bg: 'rgba(245, 158, 11, 0.8)',   // Yellow
      border: 'rgb(245, 158, 11)',
    },
    processing: {
      bg: 'rgba(59, 130, 246, 0.8)',   // Blue
      border: 'rgb(59, 130, 246)',
    },
    completed: {
      bg: 'rgba(16, 185, 129, 0.8)',   // Green
      border: 'rgb(16, 185, 129)',
    },
    failed: {
      bg: 'rgba(239, 68, 68, 0.8)',    // Red
      border: 'rgb(239, 68, 68)',
    },
    deadLetter: {
      bg: 'rgba(156, 163, 175, 0.8)',  // Gray
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

