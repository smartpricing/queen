import { ref } from 'vue';
import apiClient from '../api/client';

export function useApi() {
  const loading = ref(false);
  const error = ref(null);
  
  const execute = async (apiMethod, ...args) => {
    loading.value = true;
    error.value = null;
    
    try {
      const result = await apiMethod(...args);
      return result;
    } catch (err) {
      error.value = err.message;
      console.error('API error:', err);
      throw err;
    } finally {
      loading.value = false;
    }
  };
  
  return {
    loading,
    error,
    execute,
    client: apiClient
  };
}

