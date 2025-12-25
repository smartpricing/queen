import { ref, watch } from 'vue'

const COOKIE_NAME = 'queen-theme'
const STORAGE_KEY = 'queen-theme'

// Cookie helpers
function setCookie(name, value, days = 365) {
  const expires = new Date()
  expires.setTime(expires.getTime() + days * 24 * 60 * 60 * 1000)
  document.cookie = `${name}=${value};expires=${expires.toUTCString()};path=/;SameSite=Lax`
}

function getCookie(name) {
  const match = document.cookie.match(new RegExp('(^| )' + name + '=([^;]+)'))
  return match ? match[2] : null
}

// Get initial theme value immediately (before Vue mounts)
function getInitialTheme() {
  // First check cookie
  const cookieValue = getCookie(COOKIE_NAME)
  if (cookieValue) {
    return cookieValue === 'dark'
  }
  
  // Then check localStorage
  const stored = localStorage.getItem(STORAGE_KEY)
  if (stored) {
    return stored === 'dark'
  }
  
  // Fall back to system preference
  return window.matchMedia('(prefers-color-scheme: dark)').matches
}

// Global reactive state - initialized with saved preference
const isDark = ref(getInitialTheme())

// Apply theme to DOM immediately on module load
document.documentElement.classList.toggle('dark', isDark.value)

export function useTheme() {
  const toggleTheme = () => {
    isDark.value = !isDark.value
  }

  const setTheme = (dark) => {
    isDark.value = dark
  }

  // Re-initialize theme (can be called manually if needed)
  const initTheme = () => {
    isDark.value = getInitialTheme()
  }

  // Sync with DOM, localStorage and cookie on changes
  watch(isDark, (dark) => {
    document.documentElement.classList.toggle('dark', dark)
    const themeValue = dark ? 'dark' : 'light'
    localStorage.setItem(STORAGE_KEY, themeValue)
    setCookie(COOKIE_NAME, themeValue)
  })

  return {
    isDark,
    toggleTheme,
    setTheme,
    initTheme
  }
}

// Listen for system theme changes (only if no saved preference)
if (typeof window !== 'undefined') {
  window.matchMedia('(prefers-color-scheme: dark)')
    .addEventListener('change', (e) => {
      if (!getCookie(COOKIE_NAME) && !localStorage.getItem(STORAGE_KEY)) {
        isDark.value = e.matches
      }
    })
}

