import { createRouter, createWebHistory } from 'vue-router'

const routes = [
  {
    path: '/',
    name: 'Dashboard',
    component: () => import('@/views/Dashboard.vue'),
    meta: { title: 'Dashboard', subtitle: 'System overview and key metrics' }
  },
  {
    path: '/queues',
    name: 'Queues',
    component: () => import('@/views/Queues.vue'),
    meta: { title: 'Queues', subtitle: 'Manage message queues and partitions' }
  },
  {
    path: '/queues/:queueName',
    name: 'QueueDetail',
    component: () => import('@/views/QueueDetail.vue'),
    meta: { title: 'Queue Detail', subtitle: 'Queue configuration and status' }
  },
  {
    path: '/messages',
    name: 'Messages',
    component: () => import('@/views/Messages.vue'),
    meta: { title: 'Messages', subtitle: 'Browse and inspect messages' }
  },
  {
    path: '/traces',
    name: 'Traces',
    component: () => import('@/views/Traces.vue'),
    meta: { title: 'Traces', subtitle: 'Track message flows across queues' }
  },
  {
    path: '/consumers',
    name: 'Consumers',
    component: () => import('@/views/Consumers.vue'),
    meta: { title: 'Consumer Groups', subtitle: 'Monitor consumer lag and status' }
  },
  {
    path: '/analytics',
    name: 'Analytics',
    component: () => import('@/views/Analytics.vue'),
    meta: { title: 'Analytics', subtitle: 'Throughput and performance trends' }
  },
  {
    path: '/system',
    name: 'System',
    component: () => import('@/views/System.vue'),
    meta: { title: 'System', subtitle: 'Server health and resources' }
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

