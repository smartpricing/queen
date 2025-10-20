const config = {
  CLIENT: {
    DEFAULT_RETRY_ATTEMPTS: 3,
    DEFAULT_RETRY_DELAY: 1000,
    DEFAULT_RETRY_BACKOFF: 2 
  }
}

export const withRetry = async (fn, attempts = config.CLIENT.DEFAULT_RETRY_ATTEMPTS, delay = config.CLIENT.DEFAULT_RETRY_DELAY, backoff = config.CLIENT.DEFAULT_RETRY_BACKOFF) => {
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
    attempts = config.CLIENT.DEFAULT_RETRY_ATTEMPTS,
    delay = config.CLIENT.DEFAULT_RETRY_DELAY,
    backoff = config.CLIENT.DEFAULT_RETRY_BACKOFF
  } = options;
  
  return (...args) => withRetry(() => fn(...args), attempts, delay, backoff);
};
