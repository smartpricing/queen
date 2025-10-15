<template>
  <div class="flex items-center gap-2">
    <span
      class="status-dot flex-shrink-0"
      :class="isHealthy ? 'healthy' : 'unhealthy'"
    ></span>
    <span class="text-sm font-medium hidden sm:inline">{{ statusText }}</span>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue';
import { healthApi } from '../../api/health';

const health = ref(null);

const isHealthy = computed(() => health.value?.status === 'healthy');
const statusText = computed(() => isHealthy.value ? 'Healthy' : 'Unhealthy');

async function checkHealth() {
  try {
    const response = await healthApi.getHealth();
    health.value = response.data;
  } catch (error) {
    health.value = { status: 'unhealthy' };
  }
}

onMounted(() => {
  checkHealth();
  setInterval(checkHealth, 30000);
});
</script>
