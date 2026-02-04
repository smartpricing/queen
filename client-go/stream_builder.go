package queen

import (
	"context"
)

// StreamBuilder provides a fluent API for defining streams.
type StreamBuilder struct {
	httpClient   *HttpClient
	name         string
	namespace    string
	sources      []string
	partitioned  bool
	tumblingTime int
	gracePeriod  int
	leaseTimeout int
}

// NewStreamBuilder creates a new StreamBuilder.
func NewStreamBuilder(httpClient *HttpClient, name, namespace string) *StreamBuilder {
	return &StreamBuilder{
		httpClient: httpClient,
		name:       name,
		namespace:  namespace,
	}
}

// Sources sets the source queues for the stream.
func (sb *StreamBuilder) Sources(queueNames []string) *StreamBuilder {
	sb.sources = queueNames
	return sb
}

// Partitioned enables partitioned processing.
func (sb *StreamBuilder) Partitioned() *StreamBuilder {
	sb.partitioned = true
	return sb
}

// TumblingTime sets the tumbling window duration in seconds.
func (sb *StreamBuilder) TumblingTime(seconds int) *StreamBuilder {
	sb.tumblingTime = seconds
	return sb
}

// GracePeriod sets the grace period in seconds.
func (sb *StreamBuilder) GracePeriod(seconds int) *StreamBuilder {
	sb.gracePeriod = seconds
	return sb
}

// LeaseTimeout sets the lease timeout in seconds.
func (sb *StreamBuilder) LeaseTimeout(seconds int) *StreamBuilder {
	sb.leaseTimeout = seconds
	return sb
}

// Define creates the stream definition.
func (sb *StreamBuilder) Define(ctx context.Context) (map[string]interface{}, error) {
	body := map[string]interface{}{
		"name":      sb.name,
		"namespace": sb.namespace,
	}

	if len(sb.sources) > 0 {
		body["sources"] = sb.sources
	}
	if sb.partitioned {
		body["partitioned"] = true
	}
	if sb.tumblingTime > 0 {
		body["tumblingTime"] = sb.tumblingTime
	}
	if sb.gracePeriod > 0 {
		body["gracePeriod"] = sb.gracePeriod
	}
	if sb.leaseTimeout > 0 {
		body["leaseTimeout"] = sb.leaseTimeout
	}

	result, err := sb.httpClient.Post(ctx, "/api/v1/streams/define", body)
	if err != nil {
		return nil, err
	}

	logInfo("StreamBuilder.Define", map[string]interface{}{
		"name":      sb.name,
		"namespace": sb.namespace,
		"sources":   sb.sources,
	})

	return result, nil
}
