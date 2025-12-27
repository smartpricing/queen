import { ref, onMounted } from 'vue'

export function useProxy() {
  const isProxied = ref(false)
  const proxyUser = ref(null)
  const loading = ref(true)

  async function checkProxy() {
    try {
      const response = await fetch('/api/me')
      if (response.ok) {
        const data = await response.json()
        isProxied.value = true
        proxyUser.value = data
      } else {
        isProxied.value = false
        proxyUser.value = null
      }
    } catch (error) {
      isProxied.value = false
      proxyUser.value = null
    } finally {
      loading.value = false
    }
  }

  async function logout() {
    try {
      await fetch('/api/logout', { method: 'POST' })
      window.location.href = '/login'
    } catch (error) {
      console.error('Logout failed:', error)
      window.location.href = '/login'
    }
  }

  onMounted(() => {
    checkProxy()
  })

  return {
    isProxied,
    proxyUser,
    loading,
    logout,
    checkProxy
  }
}

