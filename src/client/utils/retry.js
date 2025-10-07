export const withRetry = async (fn, attempts = 3, delay = 1000, backoff = 2) => {
  let lastError;
  
  for (let i = 0; i < attempts; i++) {
    try {
      return await fn();
    } catch (error) {
      lastError = error;
      
      // Don't retry on client errors (4xx)
      if (error.status && error.status >= 400 && error.status < 500) {
        throw error;
      }
      
      if (i < attempts - 1) {
        const waitTime = delay * Math.pow(backoff, i);
        await new Promise(resolve => setTimeout(resolve, waitTime));
      }
    }
  }
  
  throw lastError;
};

export const createRetryableFunction = (fn, options = {}) => {
  const {
    attempts = 3,
    delay = 1000,
    backoff = 2
  } = options;
  
  return (...args) => withRetry(() => fn(...args), attempts, delay, backoff);
};
