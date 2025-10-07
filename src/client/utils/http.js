export const httpRequest = async (url, options = {}) => {
  const response = await fetch(url, {
    ...options,
    headers: {
      'Content-Type': 'application/json',
      ...options.headers
    }
  });
  
  if (response.status === 204) {
    return null; // No content
  }
  
  if (!response.ok) {
    const error = new Error(`HTTP ${response.status}: ${response.statusText}`);
    error.status = response.status;
    
    try {
      const body = await response.json();
      error.message = body.error || error.message;
    } catch (e) {
      // Ignore JSON parse errors
    }
    
    throw error;
  }
  
  return response.json();
};

export const createHttpClient = ({ baseUrl, timeout = 30000 }) => {
  const request = (method, path, body = null) => {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), timeout);
    
    const options = {
      method,
      signal: controller.signal
    };
    
    if (body) {
      options.body = JSON.stringify(body);
    }
    
    return httpRequest(`${baseUrl}${path}`, options)
      .finally(() => clearTimeout(timeoutId));
  };
  
  return {
    get: (path) => request('GET', path),
    post: (path, body) => request('POST', path, body),
    put: (path, body) => request('PUT', path, body),
    delete: (path) => request('DELETE', path)
  };
};
