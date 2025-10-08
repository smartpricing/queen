import { createRouter, createWebHistory } from 'vue-router'

// Lazy load views for better performance
const Dashboard = () => import('./views/Dashboard.vue')
const Queues = () => import('./views/Queues.vue')
const QueueDetail = () => import('./views/QueueDetail.vue')
const Analytics = () => import('./views/Analytics.vue')
const Messages = () => import('./views/Messages.vue')
const System = () => import('./views/System.vue')

const routes = [
  {
    path: '/',
    name: 'dashboard',
    component: Dashboard,
    meta: { title: 'Dashboard' }
  },
  {
    path: '/queues',
    name: 'queues',
    component: Queues,
    meta: { title: 'Queues' }
  },
  {
    path: '/queues/:queueName',
    name: 'queue-detail',
    component: QueueDetail,
    meta: { title: 'Queue Detail' }
  },
  {
    path: '/analytics',
    name: 'analytics',
    component: Analytics,
    meta: { title: 'Analytics' }
  },
  {
    path: '/messages',
    name: 'messages',
    component: Messages,
    meta: { title: 'Messages' }
  },
  {
    path: '/system',
    name: 'system',
    component: System,
    meta: { title: 'System' }
  }
]

const router = createRouter({
  history: createWebHistory('/dashboard/'),
  routes
})

// Update page title on route change
router.beforeEach((to, from, next) => {
  document.title = `${to.meta.title || 'Queen'} - Queen Dashboard`
  next()
})

export default router
