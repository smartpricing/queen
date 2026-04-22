<template>
  <div class="app-shell" :class="{ collapsed }">
    <!-- Sidebar wrapper - hidden from grid flow on mobile -->
    <div class="sidebar-slot">
      <Sidebar :collapsed="collapsed" @toggle-collapse="collapsed = !collapsed" />
    </div>

    <section class="app-main">
      <Header @refresh="handleRefresh" />
      <main class="app-page">
        <router-view v-slot="{ Component }">
          <component :is="Component" ref="pageRef" />
        </router-view>
      </main>
    </section>
  </div>
</template>

<script setup>
import { ref, provide } from 'vue'
import Sidebar from '@/components/Sidebar.vue'
import Header from '@/components/Header.vue'

const pageRef = ref(null)
const collapsed = ref(false)

const refreshCallbacks = ref(new Map())
const registerRefreshCallback = (key, callback) => { refreshCallbacks.value.set(key, callback) }
const unregisterRefreshCallback = (key) => { refreshCallbacks.value.delete(key) }
provide('registerRefreshCallback', registerRefreshCallback)
provide('unregisterRefreshCallback', unregisterRefreshCallback)

const handleRefresh = () => {
  refreshCallbacks.value.forEach((cb) => { if (typeof cb === 'function') cb() })
}
</script>
