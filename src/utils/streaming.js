/**
 * Streaming utilities for uWebSockets.js with backpressure handling
 * 
 * When sending large responses (>1MB), we need to handle backpressure to prevent:
 * - Event loop blocking
 * - Memory buildup
 * - Slow client stalling the server
 * 
 * uWebSockets.js provides efficient backpressure handling via:
 * - res.write() - Returns false if buffer is full
 * - res.onWritable() - Called when buffer has space again
 * - res.cork() - Batches syscalls for efficiency
 */

import { log } from './logger.js';

// Thresholds for streaming decisions
const SMALL_RESPONSE_THRESHOLD = 128 * 1024;  // 128KB - send directly
const CHUNK_SIZE = 64 * 1024;                  // 64KB - stream in chunks

/**
 * Send JSON response with automatic backpressure handling
 * 
 * For small responses (<128KB): Sends directly via res.end()
 * For large responses (>=128KB): Streams with backpressure via res.write() + res.onWritable()
 * 
 * @param {Object} res - uWebSockets.js response object
 * @param {Object} data - Data to serialize and send
 * @param {Object} abortedRef - Reference object with 'aborted' flag
 * @param {Object} options - Optional settings
 * @returns {boolean} - True if sent successfully, false if aborted
 */
export const streamJSON = (res, data, abortedRef = null, options = {}) => {
  // Check if already aborted
  if (abortedRef && abortedRef.aborted) {
    return false;
  }
  
  const jsonStr = JSON.stringify(data);
  const dataSize = jsonStr.length;
  
  // Small response - send directly (fast path)
  if (dataSize < SMALL_RESPONSE_THRESHOLD) {
    try {
      res.cork(() => {
        res.end(jsonStr);
      });
      return true;
    } catch (error) {
      log(`Error sending small response: ${error.message}`);
      return false;
    }
  }
  
  // Large response - stream with backpressure handling
  // uWebSockets.js requires all writes to be synchronous within cork callbacks
  log(`Streaming large response: ${(dataSize / 1024 / 1024).toFixed(2)}MB`);
  
  let offset = 0;
  
  // Start sending - write as much as possible in first cork
  res.cork(() => {
    // Write chunks until buffer is full or done
    while (offset < dataSize) {
      const chunkEnd = Math.min(offset + CHUNK_SIZE, dataSize);
      const chunk = jsonStr.slice(offset, chunkEnd);
      const isLastChunk = chunkEnd >= dataSize;
      
      if (isLastChunk) {
        // Last chunk - end the response
        res.end(chunk);
        log(`Streamed response complete: ${(dataSize / 1024).toFixed(2)}KB sent`);
        return; // All done!
      }
      
      const ok = res.write(chunk);
      offset = chunkEnd;
      
      if (!ok) {
        // Buffer full - stop and wait for drain
        log(`Backpressure detected at offset ${offset}/${dataSize} (${((offset/dataSize)*100).toFixed(1)}%)`);
        break; // Exit while loop, set up onWritable below
      }
      
      // Successfully wrote, continue to next chunk
    }
    
    // If we exited due to backpressure (offset < dataSize), set up handler
    if (offset < dataSize) {
      // Set up backpressure handler for remaining chunks
      // onWritable is called when the socket buffer has space again
      res.onWritable((writeOffset) => {
        if (abortedRef && abortedRef.aborted) {
          return true; // true = done, stop calling
        }
        
        // CRITICAL: All writes inside onWritable must also be corked!
        let isDone = true; // Will be set to false if we need to continue
        
        res.cork(() => {
          // Write as many chunks as possible while buffer has space
          while (offset < dataSize) {
            const chunkEnd = Math.min(offset + CHUNK_SIZE, dataSize);
            const chunk = jsonStr.slice(offset, chunkEnd);
            const isLastChunk = chunkEnd >= dataSize;
            
            if (isLastChunk) {
              // Last chunk - end the response
              res.end(chunk);
              log(`Streamed response complete: ${(dataSize / 1024).toFixed(2)}KB sent`);
              isDone = true; // true = done, stop calling onWritable
              return;
            }
            
            const ok = res.write(chunk);
            offset = chunkEnd;
            
            if (!ok) {
              // Buffer full again - stop and wait for next onWritable call
              log(`Backpressure detected at offset ${offset}/${dataSize} (${((offset/dataSize)*100).toFixed(1)}%)`);
              isDone = false; // false = not done, call me again when writable
              return;
            }
            
            // Successfully wrote, try next chunk in this iteration
          }
          
          // If we get here, all chunks sent
          isDone = true; // Done
        });
        
        return isDone;
      });
    }
  });
  
  return true;
};

/**
 * Send a simple JSON response (for small data)
 * Always uses res.cork() for efficiency
 */
export const sendJSON = (res, data, statusCode = 200) => {
  res.cork(() => {
    res.writeStatus(statusCode.toString());
    res.end(JSON.stringify(data));
  });
};

/**
 * Send error response with proper status code
 * Note: CORS headers should be set by caller before calling this
 */
export const sendError = (res, error, statusCode = 500) => {
  res.cork(() => {
    res.writeStatus(statusCode.toString());
    res.end(JSON.stringify({ 
      error: error.message || error,
      code: error.code || 'INTERNAL_ERROR'
    }));
  });
};

/**
 * Stream large array in chunks (alternative approach for very large arrays)
 * Serializes chunks separately to avoid large JSON.stringify() blocking
 */
export const streamJSONArray = (res, items, abortedRef = null) => {
  if (abortedRef && abortedRef.aborted) return false;
  
  const ITEMS_PER_CHUNK = 1000;
  let sent = 0;
  
  const sendNextChunk = () => {
    if (abortedRef && abortedRef.aborted) return true;
    
    const chunk = items.slice(sent, sent + ITEMS_PER_CHUNK);
    if (chunk.length === 0) {
      // All sent
      res.end(']');
      return true;
    }
    
    const isFirst = sent === 0;
    const isLast = sent + chunk.length >= items.length;
    
    // Build chunk JSON
    let chunkStr;
    if (isFirst) {
      chunkStr = '[' + chunk.map(item => JSON.stringify(item)).join(',');
    } else {
      chunkStr = ',' + chunk.map(item => JSON.stringify(item)).join(',');
    }
    
    const ok = isLast 
      ? res.end(chunkStr + ']')
      : res.write(chunkStr);
    
    sent += chunk.length;
    
    if (!ok) {
      // Backpressure
      res.onWritable(() => sendNextChunk());
      return false;
    }
    
    if (!isLast) {
      setImmediate(sendNextChunk);
    }
    
    return true;
  };
  
  res.cork(() => {
    const ok = sendNextChunk();
    if (!ok) {
      res.onWritable(sendNextChunk);
    }
  });
  
  return true;
};

export default {
  streamJSON,
  sendJSON,
  sendError,
  streamJSONArray
};

