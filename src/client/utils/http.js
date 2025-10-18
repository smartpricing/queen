export const httpRequest = async (url, options = {}) => {
  //console.log('httpRequest', url, options);
  const response = await fetch(url, {
    ...options,
    headers: {
      'Content-Type': 'application/json',
      ...options.headers
    }
  });
  //console.log('response', response);
  // Handle 204 No Content
  if (response.status === 204) {
    return null; // No content
  }
  
  // Handle errors
  if (!response.ok) {
    const error = new Error(`HTTP ${response.status}: ${response.statusText}`);
    error.status = response.status;
    
    try {
      const text = await response.text();
      if (text) {
        const body = JSON.parse(text);
        error.message = body.error || error.message;
      }
    } catch (e) {
      // Ignore JSON parse errors
    }
    
    throw error;
  }
  
  // Handle successful responses
  const contentType = response.headers.get('content-type');
  const contentLength = response.headers.get('content-length');
  
  // Check if there's actually content to parse
  if (!contentType || !contentType.includes('application/json') || contentLength === '0') {
    const text = await response.text();
    if (!text || text.length === 0) {
      return null; // Empty response
    }
    try {
      return JSON.parse(text);
    } catch (e) {
      console.warn('Failed to parse response as JSON:', text);
      return null;
    }
  }
  
  return response.json();
};

export const createHttpClient = ({ baseUrl, timeout = 30000 }) => {
  const request = (method, path, body = null, requestTimeout = null) => {
    const effectiveTimeout = requestTimeout || timeout;
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), effectiveTimeout);
    
    const options = {
      method,
      signal: controller.signal
    };
    
    if (body) {
      options.body = JSON.stringify(body);
    }
    
    return httpRequest(`${baseUrl}${path}`, options)
      .catch(error => {
        // Enhance abort errors with more context
        if (error.name === 'AbortError') {
          const timeoutError = new Error(`Request timeout after ${effectiveTimeout}ms`);
          timeoutError.name = 'AbortError';
          timeoutError.timeout = effectiveTimeout;
          throw timeoutError;
        }
        throw error;
      })
      .finally(() => clearTimeout(timeoutId));
  };
  
  return {
    get: (path, requestTimeout) => request('GET', path, null, requestTimeout),
    post: (path, body, requestTimeout) => request('POST', path, body, requestTimeout),
    put: (path, body, requestTimeout) => request('PUT', path, body, requestTimeout),
    delete: (path, requestTimeout) => request('DELETE', path, null, requestTimeout)
  };
};

/**
 * Create an HTTP client with load balancing support
 */
export const createLoadBalancedHttpClient = ({ baseUrls, loadBalancer, timeout = 30000, enableFailover = true }) => {
  const request = async (method, path, body = null, requestTimeout = null) => {
    const effectiveTimeout = requestTimeout || timeout;
    
    const executeRequest = async (baseUrl) => {
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), effectiveTimeout);
      
      const options = {
        method,
        signal: controller.signal
      };
      
      if (body) {
        options.body = JSON.stringify(body);
      }
      
      try {
        return await httpRequest(`${baseUrl}${path}`, options);
      } catch (error) {
        // Enhance abort errors with more context
        if (error.name === 'AbortError') {
          const timeoutError = new Error(`Request timeout after ${effectiveTimeout}ms`);
          timeoutError.name = 'AbortError';
          timeoutError.timeout = effectiveTimeout;
          throw timeoutError;
        }
        throw error;
      } finally {
        clearTimeout(timeoutId);
      }
    };
    
    if (enableFailover) {
      // Try with failover to different servers
      const attemptedUrls = new Set();
      const urls = loadBalancer.getAllUrls();
      let lastError = null;
      
      for (let attempt = 0; attempt < urls.length; attempt++) {
        const url = loadBalancer.getNextUrl();
        
        if (attemptedUrls.has(url)) {
          continue;
        }
        
        attemptedUrls.add(url);
        
        try {
          return await executeRequest(url);
        } catch (error) {
          lastError = error;
          console.warn(`Request failed for ${url}: ${method} ${path} - ${error.message}`);
          
          // Don't retry on client errors (4xx)
          if (error.status && error.status >= 400 && error.status < 500) {
            throw error;
          }
          
          // Continue to next server for server errors or network issues
        }
      }
      
      throw lastError || new Error('All servers failed');
    } else {
      // No failover, just use the next URL from load balancer
      const baseUrl = loadBalancer.getNextUrl();
      return executeRequest(baseUrl);
    }
  };
  
  return {
    get: (path, requestTimeout) => request('GET', path, null, requestTimeout),
    post: (path, body, requestTimeout) => request('POST', path, body, requestTimeout),
    put: (path, body, requestTimeout) => request('PUT', path, body, requestTimeout),
    delete: (path, requestTimeout) => request('DELETE', path, null, requestTimeout),
    getLoadBalancer: () => loadBalancer
  };
};
