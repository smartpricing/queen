/**
 * Utility function to get nested property value from object using dot notation
 */
const getPath = (obj, path) => 
  path.split('.').reduce((o, k) => (o && o[k] !== undefined ? o[k] : null), obj);

/**
 * Window - Represents a time window of messages with utility methods
 */
export class Window {
  constructor(rawWindow) {
    // Copy all properties from raw window
    Object.assign(this, rawWindow);
    
    // Store immutable original messages
    this.allMessages = Object.freeze(rawWindow.messages || []);
    
    // Working copy for transformations
    this.messages = [...this.allMessages];
  }

  /**
   * Filter messages based on a predicate function
   * @param {Function} filterFn - Predicate function (msg) => boolean
   * @returns {Window} this for chaining
   */
  filter(filterFn) {
    this.messages = this.messages.filter(filterFn);
    return this;
  }

  /**
   * Group messages by a key path (dot notation supported)
   * @param {string} keyPath - Path to key in message payload (e.g., 'payload.userId')
   * @returns {Object} Object with keys as group names and values as arrays of messages
   */
  groupBy(keyPath) {
    const groups = {};
    for (const msg of this.messages) {
      const key = getPath(msg, keyPath) || 'null_key';
      if (!groups[key]) {
        groups[key] = [];
      }
      groups[key].push(msg);
    }
    return groups;
  }

  /**
   * Aggregate messages using various aggregation functions
   * @param {Object} config - Aggregation configuration
   * @param {boolean} config.count - Count messages
   * @param {string[]} config.sum - Array of paths to sum
   * @param {string[]} config.avg - Array of paths to average
   * @param {string[]} config.min - Array of paths to find minimum
   * @param {string[]} config.max - Array of paths to find maximum
   * @returns {Object} Aggregation results
   */
  aggregate(config = {}) {
    const results = {};

    // Count
    if (config.count) {
      results.count = this.messages.length;
    }

    // Sum
    if (config.sum) {
      results.sum = {};
      for (const path of config.sum) {
        results.sum[path] = this.messages.reduce((total, msg) => {
          const val = getPath(msg, path);
          return total + (typeof val === 'number' ? val : 0);
        }, 0);
      }
    }

    // Average
    if (config.avg) {
      results.avg = {};
      for (const path of config.avg) {
        const sum = this.messages.reduce((total, msg) => {
          const val = getPath(msg, path);
          return total + (typeof val === 'number' ? val : 0);
        }, 0);
        results.avg[path] = this.messages.length > 0 ? sum / this.messages.length : 0;
      }
    }

    // Min
    if (config.min) {
      results.min = {};
      for (const path of config.min) {
        const values = this.messages
          .map(msg => getPath(msg, path))
          .filter(val => typeof val === 'number');
        results.min[path] = values.length > 0 ? Math.min(...values) : null;
      }
    }

    // Max
    if (config.max) {
      results.max = {};
      for (const path of config.max) {
        const values = this.messages
          .map(msg => getPath(msg, path))
          .filter(val => typeof val === 'number');
        results.max[path] = values.length > 0 ? Math.max(...values) : null;
      }
    }

    return results;
  }

  /**
   * Reset working messages to the original frozen set
   * @returns {Window} this for chaining
   */
  reset() {
    this.messages = [...this.allMessages];
    return this;
  }

  /**
   * Get count of messages in working set
   * @returns {number}
   */
  size() {
    return this.messages.length;
  }

  /**
   * Get count of original messages
   * @returns {number}
   */
  originalSize() {
    return this.allMessages.length;
  }
}

