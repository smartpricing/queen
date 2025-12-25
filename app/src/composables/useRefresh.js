import { inject, onMounted, onUnmounted } from 'vue'
import { useRoute } from 'vue-router'

export function useRefresh(callback) {
  const route = useRoute()
  const registerRefreshCallback = inject('registerRefreshCallback', null)
  const unregisterRefreshCallback = inject('unregisterRefreshCallback', null)

  onMounted(() => {
    if (registerRefreshCallback && callback) {
      registerRefreshCallback(route.path, callback)
    }
  })

  onUnmounted(() => {
    if (unregisterRefreshCallback) {
      unregisterRefreshCallback(route.path)
    }
  })
}

