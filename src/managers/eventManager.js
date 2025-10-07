import { EventEmitter } from 'events';

export const createEventManager = () => {
  const emitter = new EventEmitter();
  emitter.setMaxListeners(1000); // Support many concurrent long polls
  
  const emit = (event, data) => {
    emitter.emit(event, data);
  };
  
  const on = (event, handler) => {
    emitter.on(event, handler);
  };
  
  const once = (event, handler) => {
    emitter.once(event, handler);
  };
  
  const removeListener = (event, handler) => {
    emitter.removeListener(event, handler);
  };
  
  // Wait for a message with timeout
  const waitForMessage = (queuePath, timeout = 30000) => {
    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        removeListener(`message:${queuePath}`, handler);
        resolve(null);
      }, timeout);
      
      const handler = (data) => {
        clearTimeout(timer);
        resolve(data);
      };
      
      once(`message:${queuePath}`, handler);
    });
  };
  
  // Notify when message is available
  const notifyMessageAvailable = (queuePath) => {
    emit(`message:${queuePath}`, { available: true });
    // Also emit for parent paths
    const parts = queuePath.split('/');
    if (parts.length >= 2) {
      emit(`message:${parts[0]}/${parts[1]}`, { available: true });
    }
    if (parts.length >= 1) {
      emit(`message:${parts[0]}`, { available: true });
    }
  };
  
  return {
    emit,
    on,
    once,
    removeListener,
    waitForMessage,
    notifyMessageAvailable
  };
};
