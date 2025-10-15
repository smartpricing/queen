import { createRouter, createWebHistory } from 'vue-router';

const routes = [
  {
    path: '/',
    name: 'Dashboard',
    component: () => import('../views/Dashboard.vue'),
  },
  {
    path: '/queues',
    name: 'Queues',
    component: () => import('../views/Queues.vue'),
  },
  {
    path: '/queues/:queueName',
    name: 'QueueDetail',
    component: () => import('../views/QueueDetail.vue'),
  },
  {
    path: '/consumer-groups',
    name: 'ConsumerGroups',
    component: () => import('../views/ConsumerGroups.vue'),
  },
  {
    path: '/messages',
    name: 'Messages',
    component: () => import('../views/Messages.vue'),
  },
  {
    path: '/analytics',
    name: 'Analytics',
    component: () => import('../views/Analytics.vue'),
  },
];

const router = createRouter({
  history: createWebHistory(),
  routes,
});

export default router;

