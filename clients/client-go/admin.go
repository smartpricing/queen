package queen

import (
	"context"
	"fmt"
	"net/url"
	"strconv"
)

// Admin provides administrative API methods.
type Admin struct {
	httpClient *HttpClient
}

// NewAdmin creates a new Admin client.
func NewAdmin(httpClient *HttpClient) *Admin {
	return &Admin{httpClient: httpClient}
}

// === Resources ===

// GetOverview returns the system overview.
func (a *Admin) GetOverview(ctx context.Context) (map[string]interface{}, error) {
	return a.httpClient.Get(ctx, "/api/v1/resources/overview", 0, "")
}

// GetNamespaces returns all namespaces.
func (a *Admin) GetNamespaces(ctx context.Context) (map[string]interface{}, error) {
	return a.httpClient.Get(ctx, "/api/v1/resources/namespaces", 0, "")
}

// GetTasks returns all tasks.
func (a *Admin) GetTasks(ctx context.Context) (map[string]interface{}, error) {
	return a.httpClient.Get(ctx, "/api/v1/resources/tasks", 0, "")
}

// ListQueuesParams contains parameters for listing queues.
type ListQueuesParams struct {
	Namespace string
	Task      string
	Limit     int
	Offset    int
}

// ListQueues returns queues matching the parameters.
func (a *Admin) ListQueues(ctx context.Context, params ListQueuesParams) (map[string]interface{}, error) {
	query := url.Values{}
	if params.Namespace != "" {
		query.Set("namespace", params.Namespace)
	}
	if params.Task != "" {
		query.Set("task", params.Task)
	}
	if params.Limit > 0 {
		query.Set("limit", strconv.Itoa(params.Limit))
	}
	if params.Offset > 0 {
		query.Set("offset", strconv.Itoa(params.Offset))
	}

	path := "/api/v1/resources/queues"
	if encoded := query.Encode(); encoded != "" {
		path += "?" + encoded
	}

	return a.httpClient.Get(ctx, path, 0, "")
}

// GetQueue returns details for a specific queue.
func (a *Admin) GetQueue(ctx context.Context, name string) (map[string]interface{}, error) {
	path := fmt.Sprintf("/api/v1/resources/queues/%s", url.PathEscape(name))
	return a.httpClient.Get(ctx, path, 0, "")
}

// ClearQueue clears all messages from a queue.
func (a *Admin) ClearQueue(ctx context.Context, name string, partition string) (map[string]interface{}, error) {
	path := fmt.Sprintf("/api/v1/queues/%s/clear", url.PathEscape(name))
	if partition != "" {
		path += "?partition=" + url.QueryEscape(partition)
	}
	return a.httpClient.Delete(ctx, path)
}

// GetPartitions returns partitions matching the parameters.
func (a *Admin) GetPartitions(ctx context.Context, queueName string) (map[string]interface{}, error) {
	query := url.Values{}
	if queueName != "" {
		query.Set("queue", queueName)
	}

	path := "/api/v1/resources/partitions"
	if encoded := query.Encode(); encoded != "" {
		path += "?" + encoded
	}

	return a.httpClient.Get(ctx, path, 0, "")
}

// === Messages ===

// ListMessagesParams contains parameters for listing messages.
type ListMessagesParams struct {
	Queue         string
	Partition     string
	Status        string
	ConsumerGroup string
	Limit         int
	Offset        int
}

// ListMessages returns messages matching the parameters.
func (a *Admin) ListMessages(ctx context.Context, params ListMessagesParams) (map[string]interface{}, error) {
	query := url.Values{}
	if params.Queue != "" {
		query.Set("queue", params.Queue)
	}
	if params.Partition != "" {
		query.Set("partition", params.Partition)
	}
	if params.Status != "" {
		query.Set("status", params.Status)
	}
	if params.ConsumerGroup != "" {
		query.Set("consumerGroup", params.ConsumerGroup)
	}
	if params.Limit > 0 {
		query.Set("limit", strconv.Itoa(params.Limit))
	}
	if params.Offset > 0 {
		query.Set("offset", strconv.Itoa(params.Offset))
	}

	path := "/api/v1/messages"
	if encoded := query.Encode(); encoded != "" {
		path += "?" + encoded
	}

	return a.httpClient.Get(ctx, path, 0, "")
}

// GetMessage returns a specific message.
func (a *Admin) GetMessage(ctx context.Context, partitionID, transactionID string) (map[string]interface{}, error) {
	path := fmt.Sprintf("/api/v1/messages/%s/%s",
		url.PathEscape(partitionID), url.PathEscape(transactionID))
	return a.httpClient.Get(ctx, path, 0, "")
}

// DeleteMessage deletes a specific message.
func (a *Admin) DeleteMessage(ctx context.Context, partitionID, transactionID string) (map[string]interface{}, error) {
	path := fmt.Sprintf("/api/v1/messages/%s/%s",
		url.PathEscape(partitionID), url.PathEscape(transactionID))
	return a.httpClient.Delete(ctx, path)
}

// RetryMessage retries a failed message.
func (a *Admin) RetryMessage(ctx context.Context, partitionID, transactionID string) (map[string]interface{}, error) {
	path := fmt.Sprintf("/api/v1/messages/%s/%s/retry",
		url.PathEscape(partitionID), url.PathEscape(transactionID))
	return a.httpClient.Post(ctx, path, nil)
}

// MoveMessageToDLQ moves a message to the dead letter queue.
func (a *Admin) MoveMessageToDLQ(ctx context.Context, partitionID, transactionID string) (map[string]interface{}, error) {
	path := fmt.Sprintf("/api/v1/messages/%s/%s/dlq",
		url.PathEscape(partitionID), url.PathEscape(transactionID))
	return a.httpClient.Post(ctx, path, nil)
}

// === Traces ===

// GetTracesByName returns traces by name.
func (a *Admin) GetTracesByName(ctx context.Context, traceName string, limit, offset int) (map[string]interface{}, error) {
	query := url.Values{}
	if limit > 0 {
		query.Set("limit", strconv.Itoa(limit))
	}
	if offset > 0 {
		query.Set("offset", strconv.Itoa(offset))
	}

	path := fmt.Sprintf("/api/v1/traces/by-name/%s", url.PathEscape(traceName))
	if encoded := query.Encode(); encoded != "" {
		path += "?" + encoded
	}

	return a.httpClient.Get(ctx, path, 0, "")
}

// GetTraceNames returns all trace names.
func (a *Admin) GetTraceNames(ctx context.Context, limit, offset int) (map[string]interface{}, error) {
	query := url.Values{}
	if limit > 0 {
		query.Set("limit", strconv.Itoa(limit))
	}
	if offset > 0 {
		query.Set("offset", strconv.Itoa(offset))
	}

	path := "/api/v1/traces/names"
	if encoded := query.Encode(); encoded != "" {
		path += "?" + encoded
	}

	return a.httpClient.Get(ctx, path, 0, "")
}

// GetTracesForMessage returns traces for a specific message.
func (a *Admin) GetTracesForMessage(ctx context.Context, partitionID, transactionID string) (map[string]interface{}, error) {
	path := fmt.Sprintf("/api/v1/traces/%s/%s",
		url.PathEscape(partitionID), url.PathEscape(transactionID))
	return a.httpClient.Get(ctx, path, 0, "")
}

// === Analytics/Status ===

// GetStatusParams contains parameters for status queries.
type GetStatusParams struct {
	Queue     string
	Namespace string
	Task      string
}

// GetStatus returns the system status.
func (a *Admin) GetStatus(ctx context.Context, params GetStatusParams) (map[string]interface{}, error) {
	query := url.Values{}
	if params.Queue != "" {
		query.Set("queue", params.Queue)
	}
	if params.Namespace != "" {
		query.Set("namespace", params.Namespace)
	}
	if params.Task != "" {
		query.Set("task", params.Task)
	}

	path := "/api/v1/status"
	if encoded := query.Encode(); encoded != "" {
		path += "?" + encoded
	}

	return a.httpClient.Get(ctx, path, 0, "")
}

// GetQueueStats returns queue statistics.
func (a *Admin) GetQueueStats(ctx context.Context, namespace, task string) (map[string]interface{}, error) {
	query := url.Values{}
	if namespace != "" {
		query.Set("namespace", namespace)
	}
	if task != "" {
		query.Set("task", task)
	}

	path := "/api/v1/status/queues"
	if encoded := query.Encode(); encoded != "" {
		path += "?" + encoded
	}

	return a.httpClient.Get(ctx, path, 0, "")
}

// GetQueueDetail returns detailed statistics for a queue.
func (a *Admin) GetQueueDetail(ctx context.Context, name string, includePartitions bool) (map[string]interface{}, error) {
	path := fmt.Sprintf("/api/v1/status/queues/%s", url.PathEscape(name))
	if includePartitions {
		path += "?includePartitions=true"
	}
	return a.httpClient.Get(ctx, path, 0, "")
}

// GetAnalytics returns analytics data.
func (a *Admin) GetAnalytics(ctx context.Context, from, to string) (map[string]interface{}, error) {
	query := url.Values{}
	if from != "" {
		query.Set("from", from)
	}
	if to != "" {
		query.Set("to", to)
	}

	path := "/api/v1/status/analytics"
	if encoded := query.Encode(); encoded != "" {
		path += "?" + encoded
	}

	return a.httpClient.Get(ctx, path, 0, "")
}

// === Consumer Groups ===

// ListConsumerGroups returns all consumer groups.
func (a *Admin) ListConsumerGroups(ctx context.Context) (map[string]interface{}, error) {
	return a.httpClient.Get(ctx, "/api/v1/consumer-groups", 0, "")
}

// RefreshConsumerStats refreshes consumer group statistics.
func (a *Admin) RefreshConsumerStats(ctx context.Context) (map[string]interface{}, error) {
	return a.httpClient.Post(ctx, "/api/v1/stats/refresh", nil)
}

// GetConsumerGroup returns details for a specific consumer group.
func (a *Admin) GetConsumerGroup(ctx context.Context, name string) (map[string]interface{}, error) {
	path := fmt.Sprintf("/api/v1/consumer-groups/%s", url.PathEscape(name))
	return a.httpClient.Get(ctx, path, 0, "")
}

// GetLaggingConsumers returns consumer groups with lag above the threshold.
func (a *Admin) GetLaggingConsumers(ctx context.Context, minLagSeconds int) (map[string]interface{}, error) {
	path := fmt.Sprintf("/api/v1/consumer-groups/lagging?minLagSeconds=%d", minLagSeconds)
	return a.httpClient.Get(ctx, path, 0, "")
}

// DeleteConsumerGroupForQueue deletes a consumer group for a specific queue.
func (a *Admin) DeleteConsumerGroupForQueue(ctx context.Context, consumerGroup, queueName string, deleteMetadata bool) (map[string]interface{}, error) {
	path := fmt.Sprintf("/api/v1/consumer-groups/%s/queues/%s?deleteMetadata=%t",
		url.PathEscape(consumerGroup), url.PathEscape(queueName), deleteMetadata)
	return a.httpClient.Delete(ctx, path)
}

// SeekConsumerGroupOptions contains options for seeking a consumer group.
type SeekConsumerGroupOptions struct {
	Timestamp string
	Mode      string // "beginning", "end", or specific timestamp
}

// SeekConsumerGroup seeks a consumer group to a specific position.
func (a *Admin) SeekConsumerGroup(ctx context.Context, consumerGroup, queueName string, opts SeekConsumerGroupOptions) (map[string]interface{}, error) {
	path := fmt.Sprintf("/api/v1/consumer-groups/%s/queues/%s/seek",
		url.PathEscape(consumerGroup), url.PathEscape(queueName))

	body := map[string]interface{}{}
	if opts.Timestamp != "" {
		body["timestamp"] = opts.Timestamp
	}
	if opts.Mode != "" {
		body["mode"] = opts.Mode
	}

	return a.httpClient.Post(ctx, path, body)
}

// === System ===

// Health returns the health status.
func (a *Admin) Health(ctx context.Context) (map[string]interface{}, error) {
	return a.httpClient.Get(ctx, "/health", 0, "")
}

// Metrics returns Prometheus metrics.
func (a *Admin) Metrics(ctx context.Context) (string, error) {
	result, err := a.httpClient.Get(ctx, "/metrics", 0, "")
	if err != nil {
		return "", err
	}
	if raw, ok := result["raw"].(string); ok {
		return raw, nil
	}
	return "", nil
}

// GetMaintenanceMode returns the maintenance mode status.
func (a *Admin) GetMaintenanceMode(ctx context.Context) (map[string]interface{}, error) {
	return a.httpClient.Get(ctx, "/api/v1/system/maintenance", 0, "")
}

// SetMaintenanceMode sets the maintenance mode.
func (a *Admin) SetMaintenanceMode(ctx context.Context, enabled bool) (map[string]interface{}, error) {
	body := map[string]interface{}{"enabled": enabled}
	return a.httpClient.Post(ctx, "/api/v1/system/maintenance", body)
}

// GetPopMaintenanceMode returns the pop maintenance mode status.
func (a *Admin) GetPopMaintenanceMode(ctx context.Context) (map[string]interface{}, error) {
	return a.httpClient.Get(ctx, "/api/v1/system/maintenance/pop", 0, "")
}

// SetPopMaintenanceMode sets the pop maintenance mode.
func (a *Admin) SetPopMaintenanceMode(ctx context.Context, enabled bool) (map[string]interface{}, error) {
	body := map[string]interface{}{"enabled": enabled}
	return a.httpClient.Post(ctx, "/api/v1/system/maintenance/pop", body)
}

// GetSystemMetrics returns system metrics.
func (a *Admin) GetSystemMetrics(ctx context.Context, from, to string) (map[string]interface{}, error) {
	query := url.Values{}
	if from != "" {
		query.Set("from", from)
	}
	if to != "" {
		query.Set("to", to)
	}

	path := "/api/v1/analytics/system-metrics"
	if encoded := query.Encode(); encoded != "" {
		path += "?" + encoded
	}

	return a.httpClient.Get(ctx, path, 0, "")
}

// GetWorkerMetrics returns worker metrics.
func (a *Admin) GetWorkerMetrics(ctx context.Context, from, to string) (map[string]interface{}, error) {
	query := url.Values{}
	if from != "" {
		query.Set("from", from)
	}
	if to != "" {
		query.Set("to", to)
	}

	path := "/api/v1/analytics/worker-metrics"
	if encoded := query.Encode(); encoded != "" {
		path += "?" + encoded
	}

	return a.httpClient.Get(ctx, path, 0, "")
}

// GetPostgresStats returns PostgreSQL statistics.
func (a *Admin) GetPostgresStats(ctx context.Context) (map[string]interface{}, error) {
	return a.httpClient.Get(ctx, "/api/v1/analytics/postgres-stats", 0, "")
}
