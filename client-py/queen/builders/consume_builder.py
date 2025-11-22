"""
Consume builder for chaining callbacks
"""

from typing import Any, Callable, Optional, Union, List

from ..types import Message


class ConsumeBuilder:
    """Consume builder for chaining callbacks"""

    def __init__(
        self,
        http_client: Any,
        queen: Any,
        handler: Callable[[Union[Message, List[Message]]], Any],
        options: dict[str, Any],
    ):
        """
        Initialize consume builder

        Args:
            http_client: HttpClient instance
            queen: Queen instance
            handler: Message handler
            options: Consume options
        """
        self._http_client = http_client
        self._queen = queen
        self._handler = handler
        self._options = options
        self._on_success_callback: Optional[Callable[[Any, Any], Any]] = None
        self._on_error_callback: Optional[Callable[[Any, Exception], Any]] = None
        self._executed = False

    def on_success(self, callback: Callable[[Any, Any], Any]) -> "ConsumeBuilder":
        """Set success callback"""
        self._on_success_callback = callback
        return self

    def on_error(self, callback: Callable[[Any, Exception], Any]) -> "ConsumeBuilder":
        """Set error callback"""
        self._on_error_callback = callback
        return self

    def __await__(self):
        """Make awaitable"""
        return self._execute().__await__()

    async def _execute(self) -> Any:
        """Execute consumption"""
        if self._executed:
            return None
        self._executed = True

        # Import ConsumerManager lazily to avoid circular dependency
        from ..consumer.consumer_manager import ConsumerManager

        consumer_manager = ConsumerManager(self._http_client, self._queen)

        # Wrap the handler to include callback logic
        async def wrapped_handler(msg_or_msgs: Union[Message, List[Message]]) -> Any:
            try:
                result = await self._handler(msg_or_msgs)

                # Call onSuccess if defined
                if self._on_success_callback:
                    await self._on_success_callback(msg_or_msgs, result)

                return result
            except Exception as error:
                # Call onError if defined
                if self._on_error_callback:
                    await self._on_error_callback(msg_or_msgs, error)
                    # Don't re-raise if callback is defined
                    return None
                # Re-raise if no error callback
                raise

        # IMPORTANT: If callbacks are defined, auto-ack must be disabled
        # to prevent double-acking (auto-ack + manual ack in callback)
        has_callbacks = self._on_success_callback is not None or self._on_error_callback is not None
        effective_auto_ack = False if has_callbacks else self._options["auto_ack"]

        updated_options = {**self._options, "auto_ack": effective_auto_ack}

        return await consumer_manager.start(wrapped_handler, updated_options)

