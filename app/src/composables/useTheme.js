import { ref } from 'vue'

// Cursor theme is dark-only. Light mode has been retired.
// The toggle button is kept in the Header component as a no-op until
// phase 7 removes it completely; this composable prevents any stored
// `queen-theme=light` cookie / localStorage value from flipping the app.

const isDark = ref(true)

// Force dark class on <html>, clean up any lingering light class
document.documentElement.classList.add('dark')
document.documentElement.classList.remove('light')

// Also scrub persisted "light" preference so it doesn't try to bite again
try {
  if (typeof localStorage !== 'undefined' && localStorage.getItem('queen-theme') === 'light') {
    localStorage.setItem('queen-theme', 'dark')
  }
  if (typeof document !== 'undefined') {
    document.cookie = 'queen-theme=dark;path=/;SameSite=Lax;max-age=31536000'
  }
} catch {
  // storage/cookies may be unavailable in SSR or strict contexts; ignore
}

export function useTheme() {
  const toggleTheme = () => {
    // No-op: dark-only. Kept as a stable API so the Header button doesn't break.
  }
  const setTheme = () => {}
  const initTheme = () => {}
  return { isDark, toggleTheme, setTheme, initTheme }
}
