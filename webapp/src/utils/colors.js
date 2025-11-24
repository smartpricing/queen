// Centralized color configuration for the entire app
// Orange/Pink theme matching the new punk rock logo
// Design philosophy: Bold, energetic, vibrant

export const colors = {
  // Primary brand colors - Vibrant orange/pink palette
  primary: {
    name: 'Orange',
    hex: '#FF6B00',      // Vibrant orange
    rgb: 'rgb(255, 107, 0)',
    rgba: (opacity) => `rgba(255, 107, 0, ${opacity})`,
  },
  secondary: {
    name: 'Pink', 
    hex: '#E91E63',      // Hot pink
    rgb: 'rgb(233, 30, 99)',
    rgba: (opacity) => `rgba(233, 30, 99, ${opacity})`,
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
    hex: '#FF6B00',      // orange - matches primary
    rgb: 'rgb(255, 107, 0)',
    rgba: (opacity) => `rgba(255, 107, 0, ${opacity})`,
  },
  
  // Chart colors - Orange/Pink palette
  charts: {
    // Main data series - orange and pink tones
    ingested: {
      border: 'rgb(255, 107, 0)',           // Orange
      background: 'rgba(255, 107, 0, 0.1)',
    },
    processed: {
      border: 'rgb(233, 30, 99)',           // Pink
      background: 'rgba(233, 30, 99, 0.1)',
    },
    
    // Status distribution (keep standard colors for clarity)
    pending: {
      bg: 'rgba(245, 158, 11, 0.8)',        // Yellow
      border: 'rgb(245, 158, 11)',
    },
    processing: {
      bg: 'rgba(255, 107, 0, 0.8)',         // Orange
      border: 'rgb(255, 107, 0)',
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
