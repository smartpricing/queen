package queen

import (
	"github.com/google/uuid"
)

// GenerateUUID generates a new UUID.
// Prefers UUIDv7 (time-ordered) if available, falls back to UUIDv4.
func GenerateUUID() string {
	// Try to generate UUIDv7 (time-ordered)
	id, err := uuid.NewV7()
	if err != nil {
		// Fall back to UUIDv4
		return uuid.New().String()
	}
	return id.String()
}

// GenerateUUIDv4 generates a new UUIDv4.
func GenerateUUIDv4() string {
	return uuid.New().String()
}

// GenerateUUIDv7 generates a new UUIDv7 (time-ordered).
// Returns an error if UUIDv7 generation fails.
func GenerateUUIDv7() (string, error) {
	id, err := uuid.NewV7()
	if err != nil {
		return "", err
	}
	return id.String(), nil
}

// ParseUUID parses a UUID string.
func ParseUUID(s string) (uuid.UUID, error) {
	return uuid.Parse(s)
}

// MustParseUUID parses a UUID string and panics on error.
func MustParseUUID(s string) uuid.UUID {
	return uuid.MustParse(s)
}
