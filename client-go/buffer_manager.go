package queen

import (
	"context"
	"sync"
	"sync/atomic"
)

// BufferManager manages client-side message buffers.
type BufferManager struct {
	buffers          map[string]*MessageBuffer
	mu               sync.RWMutex
	flushesPerformed int64
}

// NewBufferManager creates a new buffer manager.
func NewBufferManager() *BufferManager {
	return &BufferManager{
		buffers: make(map[string]*MessageBuffer),
	}
}

// FlushFunc is the function called to flush messages.
type FlushFunc func(ctx context.Context, items []PushItem) error

// Add adds messages to a buffer, creating it if necessary.
// Returns immediately if buffering is not needed (triggers flush if threshold reached).
func (bm *BufferManager) Add(ctx context.Context, key string, items []PushItem, config BufferConfig, flushFn FlushFunc) error {
	bm.mu.Lock()
	buffer, exists := bm.buffers[key]
	if !exists {
		buffer = NewMessageBuffer(key, config, flushFn, bm.onFlushComplete)
		bm.buffers[key] = buffer
	}
	bm.mu.Unlock()

	return buffer.Add(ctx, items)
}

// Flush flushes a specific buffer.
func (bm *BufferManager) Flush(ctx context.Context, key string) error {
	bm.mu.RLock()
	buffer, exists := bm.buffers[key]
	bm.mu.RUnlock()

	if !exists {
		return nil // No buffer to flush
	}

	return buffer.Flush(ctx)
}

// FlushAll flushes all buffers.
func (bm *BufferManager) FlushAll(ctx context.Context) error {
	bm.mu.RLock()
	keys := make([]string, 0, len(bm.buffers))
	for key := range bm.buffers {
		keys = append(keys, key)
	}
	bm.mu.RUnlock()

	var lastErr error
	for _, key := range keys {
		if err := bm.Flush(ctx, key); err != nil {
			logError("BufferManager.FlushAll", map[string]interface{}{
				"key":   key,
				"error": err.Error(),
			})
			lastErr = err
		}
	}

	return lastErr
}

// GetStats returns buffer statistics.
func (bm *BufferManager) GetStats() BufferStats {
	bm.mu.RLock()
	defer bm.mu.RUnlock()

	stats := BufferStats{
		ActiveBuffers:    len(bm.buffers),
		FlushesPerformed: int(atomic.LoadInt64(&bm.flushesPerformed)),
	}

	var oldestAge float64
	for _, buffer := range bm.buffers {
		bufferStats := buffer.GetStats()
		stats.TotalBufferedMessages += bufferStats.count
		if bufferStats.age > oldestAge {
			oldestAge = bufferStats.age
		}
	}
	stats.OldestBufferAge = oldestAge

	return stats
}

// onFlushComplete is called when a flush completes.
func (bm *BufferManager) onFlushComplete() {
	atomic.AddInt64(&bm.flushesPerformed, 1)
}

// GetBuffer returns a buffer by key (for testing).
func (bm *BufferManager) GetBuffer(key string) *MessageBuffer {
	bm.mu.RLock()
	defer bm.mu.RUnlock()
	return bm.buffers[key]
}

// Clear removes all buffers (for testing).
func (bm *BufferManager) Clear() {
	bm.mu.Lock()
	defer bm.mu.Unlock()

	for _, buffer := range bm.buffers {
		buffer.Stop()
	}
	bm.buffers = make(map[string]*MessageBuffer)
}
