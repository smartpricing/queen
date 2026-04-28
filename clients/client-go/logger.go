package queen

import (
	"encoding/json"
	"fmt"
	"os"
	"strings"
	"sync"
	"time"
)

// LogLevel represents the logging level.
type LogLevel int

const (
	// LogLevelNone disables all logging.
	LogLevelNone LogLevel = iota
	// LogLevelError enables error logging only.
	LogLevelError
	// LogLevelWarn enables warning and error logging.
	LogLevelWarn
	// LogLevelInfo enables info, warning, and error logging.
	LogLevelInfo
	// LogLevelDebug enables all logging.
	LogLevelDebug
)

// Logger is the internal logger for the Queen client.
type Logger struct {
	level LogLevel
	mu    sync.Mutex
}

// globalLogger is the singleton logger instance.
var globalLogger = &Logger{
	level: getLogLevelFromEnv(),
}

// getLogLevelFromEnv reads the log level from QUEEN_CLIENT_LOG environment variable.
func getLogLevelFromEnv() LogLevel {
	env := os.Getenv("QUEEN_CLIENT_LOG")
	if env == "" {
		return LogLevelNone
	}

	switch strings.ToLower(env) {
	case "debug", "true", "1":
		return LogLevelDebug
	case "info":
		return LogLevelInfo
	case "warn", "warning":
		return LogLevelWarn
	case "error":
		return LogLevelError
	default:
		return LogLevelNone
	}
}

// SetLogLevel sets the global log level.
func SetLogLevel(level LogLevel) {
	globalLogger.mu.Lock()
	defer globalLogger.mu.Unlock()
	globalLogger.level = level
}

// GetLogLevel returns the current global log level.
func GetLogLevel() LogLevel {
	globalLogger.mu.Lock()
	defer globalLogger.mu.Unlock()
	return globalLogger.level
}

// logEntry represents a log entry.
type logEntry struct {
	Timestamp string                 `json:"timestamp"`
	Level     string                 `json:"level"`
	Operation string                 `json:"operation"`
	Details   map[string]interface{} `json:"details,omitempty"`
}

// log writes a log entry if the level is enabled.
func log(level LogLevel, levelStr, operation string, details map[string]interface{}) {
	globalLogger.mu.Lock()
	currentLevel := globalLogger.level
	globalLogger.mu.Unlock()

	if currentLevel < level {
		return
	}

	entry := logEntry{
		Timestamp: time.Now().UTC().Format(time.RFC3339Nano),
		Level:     levelStr,
		Operation: operation,
		Details:   details,
	}

	data, err := json.Marshal(entry)
	if err != nil {
		fmt.Fprintf(os.Stderr, "[%s] [%s] [%s] (failed to marshal log entry: %v)\n",
			entry.Timestamp, levelStr, operation, err)
		return
	}

	fmt.Fprintln(os.Stderr, string(data))
}

// logDebug logs a debug message.
func logDebug(operation string, details map[string]interface{}) {
	log(LogLevelDebug, "DEBUG", operation, details)
}

// logInfo logs an info message.
func logInfo(operation string, details map[string]interface{}) {
	log(LogLevelInfo, "INFO", operation, details)
}

// logWarn logs a warning message.
func logWarn(operation string, details map[string]interface{}) {
	log(LogLevelWarn, "WARN", operation, details)
}

// logError logs an error message.
func logError(operation string, details map[string]interface{}) {
	log(LogLevelError, "ERROR", operation, details)
}
