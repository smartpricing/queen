package queen

import (
	"context"
	"sync"
	"time"
)

// MessageBuffer is a buffer for messages to a specific queue/partition.
type MessageBuffer struct {
	key             string
	config          BufferConfig
	flushFn         FlushFunc
	onFlushComplete func()

	mu              sync.Mutex
	items           []PushItem
	firstAddTime    time.Time
	timer           *time.Timer
	flushing        bool
	stopped         bool
}

// bufferStats contains internal buffer statistics.
type bufferStats struct {
	count int
	age   float64
}

// NewMessageBuffer creates a new message buffer.
func NewMessageBuffer(key string, config BufferConfig, flushFn FlushFunc, onFlushComplete func()) *MessageBuffer {
	// Apply defaults
	if config.MessageCount == 0 {
		config.MessageCount = BufferDefaults.MessageCount
	}
	if config.TimeMillis == 0 {
		config.TimeMillis = BufferDefaults.TimeMillis
	}

	return &MessageBuffer{
		key:             key,
		config:          config,
		flushFn:         flushFn,
		onFlushComplete: onFlushComplete,
		items:           make([]PushItem, 0),
	}
}

// Add adds items to the buffer.
// Triggers flush if count threshold is reached or timer expires.
func (mb *MessageBuffer) Add(ctx context.Context, items []PushItem) error {
	mb.mu.Lock()

	if mb.stopped {
		mb.mu.Unlock()
		return nil
	}

	// Record first add time if this is the first item
	if len(mb.items) == 0 {
		mb.firstAddTime = time.Now()
		// Start timer for time-based flush
		mb.startTimer(ctx)
	}

	// Add items
	mb.items = append(mb.items, items...)

	// Check if we need to flush (count threshold)
	shouldFlush := len(mb.items) >= mb.config.MessageCount
	mb.mu.Unlock()

	if shouldFlush {
		return mb.Flush(ctx)
	}

	return nil
}

// startTimer starts the time-based flush timer.
func (mb *MessageBuffer) startTimer(ctx context.Context) {
	if mb.timer != nil {
		mb.timer.Stop()
	}

	mb.timer = time.AfterFunc(time.Duration(mb.config.TimeMillis)*time.Millisecond, func() {
		mb.mu.Lock()
		if mb.stopped || len(mb.items) == 0 {
			mb.mu.Unlock()
			return
		}
		mb.mu.Unlock()

		// Use background context for timer-triggered flush
		if err := mb.Flush(context.Background()); err != nil {
			logError("MessageBuffer.timer", map[string]interface{}{
				"key":   mb.key,
				"error": err.Error(),
			})
		}
	})
}

// Flush flushes all items in the buffer.
func (mb *MessageBuffer) Flush(ctx context.Context) error {
	mb.mu.Lock()

	// Check if already flushing or empty
	if mb.flushing || len(mb.items) == 0 {
		mb.mu.Unlock()
		return nil
	}

	mb.flushing = true
	
	// Stop timer
	if mb.timer != nil {
		mb.timer.Stop()
		mb.timer = nil
	}

	// Extract items in batches
	var lastErr error
	for len(mb.items) > 0 {
		// Get batch
		batchSize := mb.config.MessageCount
		if batchSize > len(mb.items) {
			batchSize = len(mb.items)
		}
		batch := mb.items[:batchSize]
		mb.items = mb.items[batchSize:]

		// Release lock during flush
		mb.mu.Unlock()

		// Flush batch
		if err := mb.flushFn(ctx, batch); err != nil {
			logError("MessageBuffer.Flush", map[string]interface{}{
				"key":   mb.key,
				"count": len(batch),
				"error": err.Error(),
			})
			lastErr = err
		} else {
			logDebug("MessageBuffer.Flush", map[string]interface{}{
				"key":   mb.key,
				"count": len(batch),
			})
		}

		if mb.onFlushComplete != nil {
			mb.onFlushComplete()
		}

		// Reacquire lock
		mb.mu.Lock()
	}

	mb.flushing = false
	mb.firstAddTime = time.Time{}
	mb.mu.Unlock()

	return lastErr
}

// GetStats returns buffer statistics.
func (mb *MessageBuffer) GetStats() bufferStats {
	mb.mu.Lock()
	defer mb.mu.Unlock()

	stats := bufferStats{
		count: len(mb.items),
	}

	if !mb.firstAddTime.IsZero() {
		stats.age = time.Since(mb.firstAddTime).Seconds()
	}

	return stats
}

// Stop stops the buffer and prevents further operations.
func (mb *MessageBuffer) Stop() {
	mb.mu.Lock()
	defer mb.mu.Unlock()

	mb.stopped = true
	if mb.timer != nil {
		mb.timer.Stop()
		mb.timer = nil
	}
}

// Count returns the number of items in the buffer.
func (mb *MessageBuffer) Count() int {
	mb.mu.Lock()
	defer mb.mu.Unlock()
	return len(mb.items)
}
