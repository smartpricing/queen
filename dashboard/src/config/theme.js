// Centralized theme configuration with predefined color schemes

// ============================================
// 🎨 CHANGE THIS TO SWITCH THE ENTIRE THEME
// ============================================
const ACTIVE_THEME = 'rose'; // Options: 'emerald', 'blue', 'purple', 'rose', 'orange', 'indigo', 'cyan', 'pink'

// Predefined color schemes
const colorSchemes = {
  emerald: {
    name: 'emerald',
    primary: {
      50: '#ecfdf5',
      100: '#d1fae5',
      200: '#a7f3d0',
      300: '#6ee7b7',
      400: '#34d399',
      500: '#10b981',
      600: '#059669',
      700: '#047857',
      800: '#065f46',
      900: '#064e3b',
    }
  },
  blue: {
    name: 'blue',
    primary: {
      50: '#eff6ff',
      100: '#dbeafe',
      200: '#bfdbfe',
      300: '#93c5fd',
      400: '#60a5fa',
      500: '#3b82f6',
      600: '#2563eb',
      700: '#1d4ed8',
      800: '#1e40af',
      900: '#1e3a8a',
    }
  },
  purple: {
    name: 'purple',
    primary: {
      50: '#faf5ff',
      100: '#f3e8ff',
      200: '#e9d5ff',
      300: '#d8b4fe',
      400: '#c084fc',
      500: '#a855f7',
      600: '#9333ea',
      700: '#7e22ce',
      800: '#6b21a8',
      900: '#581c87',
    }
  },
  rose: {
    name: 'rose',
    primary: {
      50: '#fff1f2',
      100: '#ffe4e6',
      200: '#fecdd3',
      300: '#fda4af',
      400: '#fb7185',
      500: '#f43f5e',
      600: '#e11d48',
      700: '#be123c',
      800: '#9f1239',
      900: '#881337',
    }
  },
  orange: {
    name: 'orange',
    primary: {
      50: '#fff7ed',
      100: '#ffedd5',
      200: '#fed7aa',
      300: '#fdba74',
      400: '#fb923c',
      500: '#f97316',
      600: '#ea580c',
      700: '#c2410c',
      800: '#9a3412',
      900: '#7c2d12',
    }
  },
  indigo: {
    name: 'indigo',
    primary: {
      50: '#eef2ff',
      100: '#e0e7ff',
      200: '#c7d2fe',
      300: '#a5b4fc',
      400: '#818cf8',
      500: '#6366f1',
      600: '#4f46e5',
      700: '#4338ca',
      800: '#3730a3',
      900: '#312e81',
    }
  },
  cyan: {
    name: 'cyan',
    primary: {
      50: '#ecfeff',
      100: '#cffafe',
      200: '#a5f3fc',
      300: '#67e8f9',
      400: '#22d3ee',
      500: '#06b6d4',
      600: '#0891b2',
      700: '#0e7490',
      800: '#155e75',
      900: '#164e63',
    }
  },
  pink: {
    name: 'pink',
    primary: {
      50: '#fdf2f8',
      100: '#fce7f3',
      200: '#fbcfe8',
      300: '#f9a8d4',
      400: '#f472b6',
      500: '#ec4899',
      600: '#db2777',
      700: '#be185d',
      800: '#9d174d',
      900: '#831843',
    }
  },
};

// Get the active theme
const activeScheme = colorSchemes[ACTIVE_THEME] || colorSchemes.emerald;

// Export the theme configuration
export const theme = {
  // Primary brand color
  primary: activeScheme.primary,
  
  // Semantic colors (fixed across all themes)
  colors: {
    success: '#10b981',
    warning: '#f59e0b',
    error: '#ef4444',
    info: '#3b82f6',
  },
  
  // Tailwind class helpers (dynamically generated from active theme)
  classes: {
    primary: activeScheme.name,
    primaryGradient: `from-${activeScheme.name}-500 to-${activeScheme.name}-600`,
    primaryHover: `hover:bg-${activeScheme.name}-50 dark:hover:bg-${activeScheme.name}-900/20`,
    primaryText: `text-${activeScheme.name}-600 dark:text-${activeScheme.name}-400`,
    primaryBg: `bg-${activeScheme.name}-600`,
    primaryBorder: `border-${activeScheme.name}-200 dark:border-${activeScheme.name}-800`,
    primaryShadow: `shadow-${activeScheme.name}-500/30`,
    primaryHoverShadow: `hover:shadow-${activeScheme.name}-500/10`,
  }
};

// Export helper to get Tailwind classes dynamically
export function getPrimaryClass(type = 'bg', shade = 600) {
  const color = theme.classes.primary;
  return `${type}-${color}-${shade}`;
}

export function getPrimaryGradient() {
  return theme.classes.primaryGradient;
}

export default theme;

