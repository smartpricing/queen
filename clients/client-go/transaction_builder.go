package queen

import (
	"context"
	"fmt"
)

// TransactionBuilder provides a fluent API for atomic operations.
type TransactionBuilder struct {
	httpClient     *HttpClient
	operations     []Operation
	requiredLeases map[string]bool
}

// NewTransactionBuilder creates a new TransactionBuilder.
func NewTransactionBuilder(httpClient *HttpClient) *TransactionBuilder {
	return &TransactionBuilder{
		httpClient:     httpClient,
		operations:     make([]Operation, 0),
		requiredLeases: make(map[string]bool),
	}
}

// Ack adds an acknowledgment operation to the transaction.
// messages can be *Message, []*Message, or []Message
// status should be "completed" or "failed"
func (tb *TransactionBuilder) Ack(messages interface{}, status string, opts AckOptions) *TransactionBuilder {
	// Normalize messages to slice
	var msgs []*Message
	switch m := messages.(type) {
	case *Message:
		msgs = []*Message{m}
	case []*Message:
		msgs = m
	case []Message:
		for i := range m {
			msgs = append(msgs, &m[i])
		}
	case Message:
		msgs = []*Message{&m}
	}

	// Add ack operations
	for _, msg := range msgs {
		op := Operation{
			Type:          "ack",
			TransactionID: msg.TransactionID,
			PartitionID:   msg.PartitionID,
			Status:        status,
			ConsumerGroup: opts.ConsumerGroup,
		}
		tb.operations = append(tb.operations, op)

		// Track required leases
		if msg.LeaseID != "" {
			tb.requiredLeases[msg.LeaseID] = true
		}
	}

	return tb
}

// Queue returns a TransactionQueueBuilder for adding push operations.
func (tb *TransactionBuilder) Queue(name string) *TransactionQueueBuilder {
	return &TransactionQueueBuilder{
		tb:        tb,
		queueName: name,
		partition: DefaultPartition,
	}
}

// Commit executes the transaction atomically.
func (tb *TransactionBuilder) Commit(ctx context.Context) (*TransactionResponse, error) {
	if len(tb.operations) == 0 {
		return nil, fmt.Errorf("transaction has no operations")
	}

	// Build required leases list
	leases := make([]string, 0, len(tb.requiredLeases))
	for lease := range tb.requiredLeases {
		leases = append(leases, lease)
	}

	// Build request
	req := transactionRequest{
		Operations:     tb.operations,
		RequiredLeases: leases,
	}

	// Make request
	result, err := tb.httpClient.Post(ctx, "/api/v1/transaction", req)
	if err != nil {
		return nil, fmt.Errorf("transaction commit failed: %w", err)
	}

	// Parse response
	response := &TransactionResponse{Success: true}
	if success, ok := result["success"].(bool); ok {
		response.Success = success
	}
	if errMsg, ok := result["error"].(string); ok && errMsg != "" {
		response.Success = false
		response.Error = errMsg
	}

	logInfo("TransactionBuilder.Commit", map[string]interface{}{
		"operations": len(tb.operations),
		"leases":     len(leases),
		"success":    response.Success,
	})

	return response, nil
}

// TransactionQueueBuilder provides a fluent API for adding push operations to a transaction.
type TransactionQueueBuilder struct {
	tb        *TransactionBuilder
	queueName string
	partition string
}

// Partition sets the partition for push operations.
func (tqb *TransactionQueueBuilder) Partition(name string) *TransactionQueueBuilder {
	if name == "" {
		tqb.partition = DefaultPartition
	} else {
		tqb.partition = name
	}
	return tqb
}

// Push adds push operations to the transaction.
// payload can be a single item or a slice.
func (tqb *TransactionQueueBuilder) Push(payload interface{}) *TransactionBuilder {
	// Normalize payload to slice
	var payloads []interface{}
	switch p := payload.(type) {
	case []interface{}:
		payloads = p
	case []map[string]interface{}:
		for _, m := range p {
			payloads = append(payloads, m)
		}
	default:
		payloads = []interface{}{payload}
	}

	// Build push items
	items := make([]PushItem, len(payloads))
	for i, p := range payloads {
		items[i] = PushItem{
			Queue:         tqb.queueName,
			Partition:     tqb.partition,
			Payload:       p,
			TransactionID: GenerateUUID(),
		}
	}

	// Add push operation
	op := Operation{
		Type:  "push",
		Items: items,
	}
	tqb.tb.operations = append(tqb.tb.operations, op)

	return tqb.tb
}
