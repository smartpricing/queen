import { ref, watch } from 'vue';

// Available color themes
export const availableThemes = {
  emerald: {
    name: 'emerald',
    label: 'Emerald',
    description: 'Fresh and natural',
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
    label: 'Blue',
    description: 'Professional and trustworthy',
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
    label: 'Purple',
    description: 'Royal and creative',
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
    label: 'Rose',
    description: 'Elegant and warm',
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
    label: 'Orange',
    description: 'Vibrant and energetic',
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
    label: 'Indigo',
    description: 'Deep and sophisticated',
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
    label: 'Cyan',
    description: 'Cool and modern',
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
    label: 'Pink',
    description: 'Playful and bright',
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

// Get initial theme from localStorage or default to emerald
const getInitialTheme = () => {
  if (typeof window === 'undefined') return 'emerald';
  const saved = localStorage.getItem('queen-theme');
  return saved && availableThemes[saved] ? saved : 'emerald';
};

// Reactive theme state
const currentTheme = ref(getInitialTheme());

// Helper function to convert hex to RGB
function hexToRgb(hex) {
  const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
  return result ? {
    r: parseInt(result[1], 16),
    g: parseInt(result[2], 16),
    b: parseInt(result[3], 16)
  } : null;
}

// Apply theme CSS variables
function applyThemeColors(themeName) {
  if (typeof document === 'undefined') return;
  
  const scheme = availableThemes[themeName];
  if (!scheme) return;
  
  const root = document.documentElement;
  Object.entries(scheme.primary).forEach(([shade, hex]) => {
    const rgb = hexToRgb(hex);
    if (rgb) {
      root.style.setProperty(`--color-primary-${shade}`, `${rgb.r} ${rgb.g} ${rgb.b}`);
    }
  });
}

// Watch for theme changes and apply them
watch(currentTheme, (newTheme) => {
  applyThemeColors(newTheme);
  if (typeof window !== 'undefined') {
    localStorage.setItem('queen-theme', newTheme);
  }
}, { immediate: true });

export function useTheme() {
  const setTheme = (themeName) => {
    if (availableThemes[themeName]) {
      currentTheme.value = themeName;
    }
  };

  const getCurrentTheme = () => {
    return availableThemes[currentTheme.value];
  };

  return {
    currentTheme,
    availableThemes,
    setTheme,
    getCurrentTheme,
  };
}

export default useTheme;

