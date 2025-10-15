import { ref } from 'vue';

export function useApi(apiCall) {
  const data = ref(null);
  const error = ref(null);
  const loading = ref(false);

  const execute = async (...params) => {
    loading.value = true;
    error.value = null;
    
    try {
      const response = await apiCall(...params);
      data.value = response.data;
      return response.data;
    } catch (err) {
      error.value = err.response?.data?.error || err.message || 'An error occurred';
      throw err;
    } finally {
      loading.value = false;
    }
  };

  const refresh = () => execute();

  return {
    data,
    error,
    loading,
    execute,
    refresh,
  };
}

