"""
Default configuration values for Queen Client
Following the convention:
- Properties with "Millis" suffix → milliseconds
- Properties with "Seconds" suffix → seconds
- Properties without suffix (time-related) → seconds
"""

from typing import Any, Dict, Optional

CLIENT_DEFAULTS: Dict[str, Any] = {
    "timeout_millis": 30000,  # 30 seconds
    "retry_attempts": 3,  # 3 retry attempts
    "retry_delay_millis": 1000,  # 1 second initial delay (exponential backoff)
    "load_balancing_strategy": "affinity",  # 'round-robin', 'session', or 'affinity'
    "affinity_hash_ring": 128,  # Number of virtual nodes per server for affinity strategy
    "enable_failover": True,  # Auto-failover to other servers
    "health_retry_after_millis": 5000,  # Retry unhealthy backends after 5 seconds
    "bearer_token": None,  # Bearer token for proxy authentication
}

QUEUE_DEFAULTS: Dict[str, Any] = {
    "lease_time": 300,  # 5 minutes (seconds)
    "retry_limit": 3,  # Max 3 retries before DLQ
    "priority": 0,  # Default priority
    "delayed_processing": 0,  # No delay (seconds)
    "window_buffer": 0,  # No window buffering (seconds)
    "max_size": 0,  # No limit on messages per queue
    "retention_seconds": 0,  # No retention (keep forever)
    "completed_retention_seconds": 0,  # No retention for completed messages
    "encryption_enabled": False,  # No encryption by default
}

CONSUME_DEFAULTS: Dict[str, Any] = {
    "concurrency": 1,  # Single worker
    "batch": 1,  # One message at a time
    "auto_ack": True,  # Client-side auto-ack (NOT sent to server)
    "wait": True,  # Long polling enabled
    "timeout_millis": 30000,  # 30 seconds long poll timeout
    "limit": None,  # No limit (run forever)
    "idle_millis": None,  # No idle timeout
    "renew_lease": False,  # No auto-renewal
    "renew_lease_interval_millis": None,  # Auto-renewal interval when enabled
    "subscription_mode": None,  # No subscription mode (standard queue mode)
    "subscription_from": None,  # No subscription start point
}

POP_DEFAULTS: Dict[str, Any] = {
    "batch": 1,  # One message
    "wait": False,  # No long polling (immediate return)
    "timeout_millis": 30000,  # 30 seconds if wait=true
    "auto_ack": False,  # Server-side auto-ack (false = manual ack required)
}

BUFFER_DEFAULTS: Dict[str, Any] = {
    "message_count": 100,  # Flush after 100 messages
    "time_millis": 1000,  # Or flush after 1 second
}

