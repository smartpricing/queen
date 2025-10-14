import { createRouter, createWebHistory } from 'vue-router';

const routes = [
  {
    path: '/',
    component: () => import('./views/Dashboard.vue'),
    meta: { title: 'Dashboard' }
  },
  {
    path: '/queues',
    component: () => import('./views/Queues.vue'),
    meta: { title: 'Queues' }
  },
  {
    path: '/queues/:queueName',
    component: () => import('./views/QueueDetail.vue'),
    meta: { title: 'Queue Detail' }
  },
  {
    path: '/queues/:queueName/messages',
    component: () => import('./views/Messages.vue'),
    meta: { title: 'Messages' }
  },
  {
    path: '/analytics',
    component: () => import('./views/Analytics.vue'),
    meta: { title: 'Analytics' }
  },
  {
    path: '/settings',
    component: () => import('./views/Settings.vue'),
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

