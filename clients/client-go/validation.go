package queen

import (
	"fmt"
	"net/url"
	"regexp"
	"strings"
)

// uuidRegex matches UUID v4 format.
var uuidRegex = regexp.MustCompile(`^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$`)

// IsValidUUID checks if a string is a valid UUID.
func IsValidUUID(s string) bool {
	return uuidRegex.MatchString(s)
}

// ValidateURL validates a URL string.
func ValidateURL(rawURL string) (string, error) {
	if rawURL == "" {
		return "", fmt.Errorf("URL cannot be empty")
	}

	// Trim whitespace
	rawURL = strings.TrimSpace(rawURL)

	// Parse the URL
	parsed, err := url.Parse(rawURL)
	if err != nil {
		return "", fmt.Errorf("invalid URL format: %w", err)
	}

	// Check scheme
	if parsed.Scheme != "http" && parsed.Scheme != "https" {
		return "", fmt.Errorf("URL must use http or https scheme: %s", rawURL)
	}

	// Check host
	if parsed.Host == "" {
		return "", fmt.Errorf("URL must have a host: %s", rawURL)
	}

	// Remove trailing slash for consistency
	return strings.TrimSuffix(rawURL, "/"), nil
}

// ValidateURLs validates a list of URLs.
func ValidateURLs(urls []string) ([]string, error) {
	if len(urls) == 0 {
		return nil, fmt.Errorf("at least one URL is required")
	}

	validated := make([]string, len(urls))
	for i, u := range urls {
		v, err := ValidateURL(u)
		if err != nil {
			return nil, fmt.Errorf("invalid URL at index %d: %w", i, err)
		}
		validated[i] = v
	}

	return validated, nil
}

// ValidateQueueName validates a queue name.
func ValidateQueueName(name string) (string, error) {
	if name == "" {
		return "", fmt.Errorf("queue name cannot be empty")
	}

	// Trim whitespace
	name = strings.TrimSpace(name)

	// Check length
	if len(name) > 255 {
		return "", fmt.Errorf("queue name cannot exceed 255 characters")
	}

	// Check for invalid characters (allow alphanumeric, dash, underscore, dot)
	validName := regexp.MustCompile(`^[a-zA-Z0-9_.-]+$`)
	if !validName.MatchString(name) {
		return "", fmt.Errorf("queue name contains invalid characters: %s (allowed: alphanumeric, dash, underscore, dot)", name)
	}

	return name, nil
}

// ValidatePartitionName validates a partition name.
func ValidatePartitionName(name string) (string, error) {
	if name == "" {
		return DefaultPartition, nil
	}

	// Trim whitespace
	name = strings.TrimSpace(name)

	// Check length
	if len(name) > 255 {
		return "", fmt.Errorf("partition name cannot exceed 255 characters")
	}

	return name, nil
}

// ValidateConsumerGroup validates a consumer group name.
func ValidateConsumerGroup(name string) (string, error) {
	if name == "" {
		return "", nil // Empty is valid (means queue mode)
	}

	// Trim whitespace
	name = strings.TrimSpace(name)

	// Check length
	if len(name) > 255 {
		return "", fmt.Errorf("consumer group name cannot exceed 255 characters")
	}

	return name, nil
}

// ValidateBatchSize validates a batch size.
func ValidateBatchSize(size int) error {
	if size < 1 {
		return fmt.Errorf("batch size must be at least 1")
	}
	if size > 1000 {
		return fmt.Errorf("batch size cannot exceed 1000")
	}
	return nil
}

// ValidateConcurrency validates a concurrency value.
func ValidateConcurrency(count int) error {
	if count < 1 {
		return fmt.Errorf("concurrency must be at least 1")
	}
	if count > 100 {
		return fmt.Errorf("concurrency cannot exceed 100")
	}
	return nil
}

// ValidateTimeout validates a timeout value in milliseconds.
func ValidateTimeout(millis int) error {
	if millis < 0 {
		return fmt.Errorf("timeout cannot be negative")
	}
	if millis > 300000 { // 5 minutes max
		return fmt.Errorf("timeout cannot exceed 300000ms (5 minutes)")
	}
	return nil
}

// ValidateMessage validates that a message has the required fields for acknowledgment.
func ValidateMessage(msg *Message) error {
	if msg == nil {
		return fmt.Errorf("message cannot be nil")
	}
	if msg.TransactionID == "" {
		return fmt.Errorf("message must have a transactionId")
	}
	if msg.PartitionID == "" {
		return fmt.Errorf("message must have a partitionId (mandatory for ACK operations)")
	}
	return nil
}

// ValidateMessages validates a slice of messages.
func ValidateMessages(msgs []*Message) error {
	if len(msgs) == 0 {
		return fmt.Errorf("at least one message is required")
	}
	for i, msg := range msgs {
		if err := ValidateMessage(msg); err != nil {
			return fmt.Errorf("invalid message at index %d: %w", i, err)
		}
	}
	return nil
}
