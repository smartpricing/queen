package queen

import (
	"context"
	"fmt"
	"time"
)

// Window represents a stream processing window.
type Window struct {
	ID        string                   `json:"id"`
	Start     time.Time                `json:"start"`
	End       time.Time                `json:"end"`
	Messages  []map[string]interface{} `json:"messages"`
	Partition string                   `json:"partition,omitempty"`
}

// StreamConsumer provides stream consumption functionality.
type StreamConsumer struct {
	httpClient    *HttpClient
	streamName    string
	consumerGroup string
	running       bool
	stopChan      chan struct{}
}

// NewStreamConsumer creates a new StreamConsumer.
func NewStreamConsumer(httpClient *HttpClient, streamName, consumerGroup string) *StreamConsumer {
	return &StreamConsumer{
		httpClient:    httpClient,
		streamName:    streamName,
		consumerGroup: consumerGroup,
		stopChan:      make(chan struct{}),
	}
}

// WindowHandler is the function signature for handling windows.
type WindowHandler func(ctx context.Context, window *Window) error

// Process starts processing windows with the given handler.
func (sc *StreamConsumer) Process(ctx context.Context, handler WindowHandler) error {
	sc.running = true
	defer func() { sc.running = false }()

	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-sc.stopChan:
			return nil
		default:
		}

		window, err := sc.PollWindow(ctx)
		if err != nil {
			logWarn("StreamConsumer.Process", map[string]interface{}{
				"error": err.Error(),
			})
			// Wait before retrying
			select {
			case <-ctx.Done():
				return ctx.Err()
			case <-time.After(time.Second):
				continue
			}
		}

		if window == nil {
			// No window available, wait
			select {
			case <-ctx.Done():
				return ctx.Err()
			case <-time.After(100 * time.Millisecond):
				continue
			}
		}

		if err := handler(ctx, window); err != nil {
			logError("StreamConsumer.Process", map[string]interface{}{
				"windowId": window.ID,
				"error":    err.Error(),
			})
		}
	}
}

// PollWindow polls for the next available window.
func (sc *StreamConsumer) PollWindow(ctx context.Context) (*Window, error) {
	path := fmt.Sprintf("/api/v1/streams/%s/poll?consumerGroup=%s",
		sc.streamName, sc.consumerGroup)

	result, err := sc.httpClient.Get(ctx, path, 30000, "")
	if err != nil {
		return nil, err
	}

	if result == nil {
		return nil, nil
	}

	// Parse window
	window := &Window{}
	if id, ok := result["id"].(string); ok {
		window.ID = id
	}
	if startStr, ok := result["start"].(string); ok {
		if t, err := time.Parse(time.RFC3339, startStr); err == nil {
			window.Start = t
		}
	}
	if endStr, ok := result["end"].(string); ok {
		if t, err := time.Parse(time.RFC3339, endStr); err == nil {
			window.End = t
		}
	}
	if msgs, ok := result["messages"].([]interface{}); ok {
		for _, m := range msgs {
			if msg, ok := m.(map[string]interface{}); ok {
				window.Messages = append(window.Messages, msg)
			}
		}
	}
	if partition, ok := result["partition"].(string); ok {
		window.Partition = partition
	}

	return window, nil
}

// Stop stops the consumer.
func (sc *StreamConsumer) Stop() {
	if sc.running {
		close(sc.stopChan)
	}
}

// Seek seeks the consumer to a specific timestamp.
func (sc *StreamConsumer) Seek(ctx context.Context, timestamp time.Time) (map[string]interface{}, error) {
	path := fmt.Sprintf("/api/v1/streams/%s/seek", sc.streamName)
	body := map[string]interface{}{
		"consumerGroup": sc.consumerGroup,
		"timestamp":     timestamp.Format(time.RFC3339),
	}
	return sc.httpClient.Post(ctx, path, body)
}
