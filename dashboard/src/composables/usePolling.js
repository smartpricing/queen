import { ref, onUnmounted } from 'vue';

export function usePolling(callback, interval = 5000) {
  const isPolling = ref(false);
  const intervalId = ref(null);
  
  const startPolling = () => {
    if (intervalId.value) return;
    
    isPolling.value = true;
    intervalId.value = setInterval(() => {
      callback();
    }, interval);
  };
  
  const stopPolling = () => {
    if (intervalId.value) {
      clearInterval(intervalId.value);
      intervalId.value = null;
    }
    isPolling.value = false;
  };
  
  const restartPolling = () => {
    stopPolling();
    startPolling();
  };
  
  onUnmounted(() => {
    stopPolling();
  });
  
  return {
    isPolling,
    startPolling,
    stopPolling,
    restartPolling
  };
}

