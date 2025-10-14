import { createRouter, createWebHistory } from 'vue-router';

const routes = [
  {
    path: '/',
    component: () => import('./views/DashboardV2.vue'),
    meta: { title: 'Dashboard' }
  },
  {
    path: '/queues',
    component: () => import('./views/QueuesV3.vue'),
    meta: { title: 'Queues' }
  },
  {
    path: '/queues/:queueName',
    component: () => import('./views/QueueDetailV2.vue'),
    meta: { title: 'Queue Detail' }
  },
  {
    path: '/queues/:queueName/messages',
    component: () => import('./views/Messages.vue'),
    meta: { title: 'Messages' }
  },
  {
    path: '/analytics',
    component: () => import('./views/AnalyticsV2.vue'),
    meta: { title: 'Analytics' }
  },
  {
    path: '/settings',
    component: () => import('./views/SettingsV2.vue'),
    meta: { title: 'Settings' }
  }
];

const router = createRouter({
  history: createWebHistory(),
  routes
});

router.beforeEach((to, from, next) => {
  document.title = `${to.meta.title || 'Queen'} - Queen Dashboard`;
  next();
});

export default router;

