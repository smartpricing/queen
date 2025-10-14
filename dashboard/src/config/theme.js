// Centralized theme configuration
// Theme is now managed via useTheme composable and localStorage
// This file provides backward compatibility

import { useTheme } from '../composables/useTheme';

// For backward compatibility, we create a reactive proxy
let themeProxy = null;

function getTheme() {
  if (!themeProxy) {
    const { getCurrentTheme } = useTheme();
    const currentTheme = getCurrentTheme();
    
    themeProxy = {
      name: currentTheme.name,
      primary: currentTheme.primary,
      colors: {
        success: '#10b981',
        warning: '#f59e0b',
        error: '#ef4444',
        info: '#3b82f6',
      },
    };
  }
  return themeProxy;
}

// Export the theme configuration (dynamic)
export const theme = new Proxy({}, {
  get(target, prop) {
    const currentTheme = getTheme();
    return currentTheme[prop];
  }
});

export default theme;

