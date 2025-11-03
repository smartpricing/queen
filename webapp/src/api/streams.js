import apiClient from './client';

export const streamsApi = {
  // Get all streams
  getStreams: () => apiClient.get('/api/v1/resources/streams'),
  
  // Get stream details
  getStream: (streamName) => apiClient.get(`/api/v1/resources/streams/${streamName}`),
  
  // Define/create a stream
  defineStream: (data) => apiClient.post('/api/v1/stream/define', data),
  
  // Delete a stream
  deleteStream: (streamName) => apiClient.delete(`/api/v1/resources/streams/${streamName}`),
  
  // Get consumer groups for a stream
  getStreamConsumers: (streamName) => apiClient.get(`/api/v1/resources/streams/${streamName}/consumers`),
  
  // Seek to timestamp for a consumer group
  seekStream: (data) => apiClient.post('/api/v1/stream/seek', data),
  
  // Get stream statistics
  getStreamStats: () => apiClient.get('/api/v1/resources/streams/stats'),
};

