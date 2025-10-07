import { createRouter, createWebHistory } from 'vue-router'

const routes = [
  {
    path: '/',
    name: 'Dashboard',
    component: () => import('../views/Dashboard.vue'),
    meta: { title: 'Dashboard', icon: 'pi pi-home' }
  },
  {
    path: '/queues',
    name: 'Queues',
    component: () => import('../views/Queues.vue'),
    meta: { title: 'Queues', icon: 'pi pi-list' }
  },
  {
    path: '/queues/:id',
    name: 'QueueDetail',
    component: () => import('../views/QueueDetail.vue'),
    meta: { title: 'Queue Details', hidden: true }
  },
  {
    path: '/messages',
    name: 'Messages',
    component: () => import('../views/Messages.vue'),
    meta: { title: 'Messages', icon: 'pi pi-envelope' }
  },
  {
    path: '/activity',
    name: 'Activity',
    component: () => import('../views/Activity.vue'),
    meta: { title: 'Activity', icon: 'pi pi-history' }
  },
  {
    path: '/messages/:id',
    name: 'MessageDetail',
    component: () => import('../views/MessageDetail.vue'),
    meta: { title: 'Message Details', hidden: true }
  },
  {
    path: '/namespaces',
    name: 'Namespaces',
    component: () => import('../views/Namespaces.vue'),
    meta: { title: 'Namespaces', icon: 'pi pi-folder' }
  },
  {
    path: '/analytics',
    name: 'Analytics',
    component: () => import('../views/Analytics.vue'),
    meta: { title: 'Analytics', icon: 'pi pi-chart-line' }
  },
  {
    path: '/configure',
    name: 'Configure',
    component: () => import('../views/Configure.vue'),
    meta: { title: 'Configure', icon: 'pi pi-cog' }
  },
  {
    path: '/test',
    name: 'TestTool',
    component: () => import('../views/TestTool.vue'),
    meta: { title: 'Test Tool', icon: 'pi pi-play' }
  },
  {
    path: '/settings',
    name: 'Settings',
    component: () => import('../views/Settings.vue'),
    meta: { title: 'Settings', icon: 'pi pi-sliders-h' }
  }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

router.beforeEach((to, from, next) => {
  document.title = `${to.meta.title || 'Queen'} - Queen Dashboard`
  next()
})

export default router
