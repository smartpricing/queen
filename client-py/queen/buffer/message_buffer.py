"""
Message buffer for a single queue
"""

import asyncio
import time
from typing import Any, Callable, Dict, List, Optional


class MessageBuffer:
    """Message buffer for a single queue"""

    def __init__(
        self,
        queue_address: str,
        options: Dict[str, Any],
        flush_callback: Callable[[str], asyncio.Task[None]],
    ):
        """
        Initialize message buffer

        Args:
            queue_address: Queue address (queue/partition)
            options: Buffer options (message_count, time_millis)
            flush_callback: Callback to trigger flush
        """
        self._queue_address = queue_address
        self._messages: List[Dict[str, Any]] = []
        self._options = options
        self._flush_callback = flush_callback
        self._timer: Optional[asyncio.TimerHandle] = None
        self._first_message_time: Optional[float] = None
        self._flushing = False

    def add(self, formatted_message: Dict[str, Any]) -> None:
        """
        Add message to buffer

        Args:
            formatted_message: Formatted message dict
        """
        # Set first message time if this is the first message
        if not self._messages:
            self._first_message_time = time.time()
            self._start_timer()

        self._messages.append(formatted_message)

        # Check if we should flush based on size
        if len(self._messages) >= self._options["message_count"]:
            self._trigger_flush()

    def _start_timer(self) -> None:
        """Start timer for time-based flush"""
        if self._timer:
            return  # Timer already running

        loop = asyncio.get_event_loop()
        delay_seconds = self._options["time_millis"] / 1000.0
        self._timer = loop.call_later(delay_seconds, self._trigger_flush)

    def _trigger_flush(self) -> None:
        """Trigger flush via callback"""
        if self._flushing or not self._messages:
            return

        # Clear timer
        if self._timer:
            self._timer.cancel()
            self._timer = None

        # Trigger flush via callback
        self._flush_callback(self._queue_address)

    def extract_messages(self, batch_size: Optional[int] = None) -> List[Dict[str, Any]]:
        """
        Extract messages from buffer

        Args:
            batch_size: Optional batch size (None = all messages)

        Returns:
            List of extracted messages
        """
        # If no batch size specified, extract all messages
        if batch_size is None or batch_size >= len(self._messages):
            messages = list(self._messages)
            self._messages = []
            self._first_message_time = None
            self._flushing = False

            if self._timer:
                self._timer.cancel()
                self._timer = None

            return messages

        # Extract a batch of messages
        messages = self._messages[:batch_size]
        self._messages = self._messages[batch_size:]

        # If buffer is now empty, reset state
        if not self._messages:
            self._first_message_time = None
            self._flushing = False

            if self._timer:
                self._timer.cancel()
                self._timer = None

        return messages

    def set_flushing(self, value: bool) -> None:
        """Set flushing state"""
        self._flushing = value

    def force_flush(self) -> None:
        """Immediately trigger flush, ignoring timers"""
        if self._timer:
            self._timer.cancel()
            self._timer = None
        self._trigger_flush()

    def cancel_timer(self) -> None:
        """Cancel the timer without triggering a flush"""
        if self._timer:
            self._timer.cancel()
            self._timer = None

    @property
    def message_count(self) -> int:
        """Get message count"""
        return len(self._messages)

    @property
    def options(self) -> Dict[str, Any]:
        """Get buffer options"""
        return self._options

    @property
    def first_message_age(self) -> float:
        """Get age of first message in milliseconds"""
        if self._first_message_time:
            return (time.time() - self._first_message_time) * 1000.0
        return 0.0

    def cleanup(self) -> None:
        """Cleanup buffer"""
        if self._timer:
            self._timer.cancel()
            self._timer = None
        self._messages = []
        self._first_message_time = None
        self._flushing = False

