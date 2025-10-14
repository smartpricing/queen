import { ref, watch } from 'vue';

const colorMode = ref('light');
const preferredMode = ref('system');

export function useColorMode() {
  const initColorMode = () => {
    // Get saved preference or default to system
    const saved = localStorage.getItem('queen-color-mode');
    preferredMode.value = saved || 'system';
    
    // Apply the mode
    applyColorMode();
    
    // Watch for system preference changes
    const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
    mediaQuery.addEventListener('change', () => {
      if (preferredMode.value === 'system') {
        applyColorMode();
      }
    });
  };
  
  const applyColorMode = () => {
    let mode = preferredMode.value;
    
    if (mode === 'system') {
      mode = window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
    }
    
    colorMode.value = mode;
    
    if (mode === 'dark') {
      document.documentElement.classList.add('dark');
    } else {
      document.documentElement.classList.remove('dark');
    }
  };
  
  const setColorMode = (mode) => {
    preferredMode.value = mode;
    localStorage.setItem('queen-color-mode', mode);
    applyColorMode();
  };
  
  const toggleColorMode = () => {
    const nextMode = colorMode.value === 'light' ? 'dark' : 'light';
    setColorMode(nextMode);
  };
  
  return {
    colorMode,
    preferredMode,
    initColorMode,
    setColorMode,
    toggleColorMode
  };
}

