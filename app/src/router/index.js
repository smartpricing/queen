import { createRouter, createWebHistory } from 'vue-router'

const routes = [
  {
    path: '/',
    name: 'Dashboard',
    component: () => import('@/views/Dashboard.vue'),
    meta: { title: 'Dashboard', icon: 'dashboard' }
  },
  {
    path: '/queues',
    name: 'Queues',
    component: () => import('@/views/Queues.vue'),
    meta: { title: 'Queues', icon: 'queues' }
  },
  {
    path: '/queues/:queueName',
    name: 'QueueDetail',
    component: () => import('@/views/QueueDetail.vue'),
    meta: { title: 'Queue Detail', icon: 'queues' }
  },
  {
    path: '/messages',
    name: 'Messages',
    component: () => import('@/views/Messages.vue'),
    meta: { title: 'Messages', icon: 'messages' }
  },
  {
    path: '/consumers',
    name: 'Consumers',
    component: () => import('@/views/Consumers.vue'),
    meta: { title: 'Consumer Groups', icon: 'consumers' }
  },
  {
    path: '/analytics',
    name: 'Analytics',
    component: () => import('@/views/Analytics.vue'),
    meta: { title: 'Analytics', icon: 'analytics' }
  },
  {
    path: '/system',
    name: 'System',
    component: () => import('@/views/System.vue'),
    meta: { title: 'System', icon: 'system' }
  },
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

router.beforeEach((to, from, next) => {
  document.title = `${to.meta.title || 'Queen'} | Queen Dashboard`
  next()
})

export default router

