"""
StreamConsumer - Manages consuming windows from a stream
"""

import asyncio
from typing import Any, Callable, Dict, Optional

from .window import Window


class StreamConsumer:
    """StreamConsumer - Manages consuming windows from a stream"""

    def __init__(self, http_client: Any, queen: Any, stream_name: str, consumer_group: str):
        """
        Initialize stream consumer

        Args:
            http_client: HttpClient instance
            queen: Queen instance
            stream_name: Stream name
            consumer_group: Consumer group name
        """
        self._http_client = http_client
        self._queen = queen
        self._stream_name = stream_name
        self._consumer_group = consumer_group
        self._poll_timeout = 30000  # 30s long poll
        self._lease_renew_interval = 20000  # 20s renewal (before 60s timeout)
        self._running = False

    async def process(self, callback: Callable[[Window], Any]) -> None:
        """
        Start processing windows with the provided callback
        Runs in an infinite loop until stopped

        Args:
            callback: async function(window) to process each window
        """
        self._running = True

        while self._running:
            window = None

            try:
                window = await self.poll_window()

                if not window:
                    # 204 No Content - no window available
                    continue

                await self.execute_callback(window, callback)

            except Exception as err:
                # Backoff on error
                await asyncio.sleep(1)

    def stop(self) -> None:
        """Stop the processing loop"""
        self._running = False

    async def poll_window(self) -> Optional[Window]:
        """
        Poll for a window (blocking call)

        Returns:
            Window or None if no content
        """
        try:
            response = await self._http_client.post(
                "/api/v1/stream/poll",
                {
                    "streamName": self._stream_name,
                    "consumerGroup": self._consumer_group,
                    "timeout": self._poll_timeout,
                },
            )

            # Handle 204 No Content (HttpClient returns None for 204)
            if not response:
                return None

            # HttpClient returns the JSON body directly (e.g., {"window": {...}})
            if not response.get("window"):
                return None

            return Window(response["window"])

        except Exception:
            # HttpClient throws on errors, returns None on 204
            # So any error here is a real network/server error
            raise

    async def execute_callback(self, window: Window, callback: Callable[[Window], Any]) -> None:
        """
        Execute the callback with lease renewal

        Args:
            window: The window to process
            callback: User callback
        """
        lease_timer: Optional[asyncio.Task[None]] = None
        lease_expired = False

        try:
            # Start lease renewal timer
            async def renew_loop() -> None:
                nonlocal lease_expired
                while True:
                    await asyncio.sleep(self._lease_renew_interval / 1000.0)
                    try:
                        await self._http_client.post(
                            "/api/v1/stream/renew-lease",
                            {
                                "leaseId": window.leaseId,  # type: ignore
                                "extend_ms": self._lease_renew_interval + 10000,
                            },
                        )
                    except Exception:
                        lease_expired = True

            lease_timer = asyncio.create_task(renew_loop())

            # Execute user callback
            await callback(window)

            # ACK if lease hasn't expired
            if not lease_expired:
                await self._http_client.post(
                    "/api/v1/stream/ack",
                    {
                        "windowId": window.id,  # type: ignore
                        "leaseId": window.leaseId,  # type: ignore
                        "success": True,
                    },
                )

        except Exception as err:
            # NACK if lease hasn't expired
            if not lease_expired:
                try:
                    await self._http_client.post(
                        "/api/v1/stream/ack",
                        {
                            "windowId": window.id,  # type: ignore
                            "leaseId": window.leaseId,  # type: ignore
                            "success": False,  # NACK
                        },
                    )
                except Exception:
                    pass

            # Re-throw to trigger backoff
            raise

        finally:
            # Stop lease renewal
            if lease_timer:
                lease_timer.cancel()
                try:
                    await lease_timer
                except asyncio.CancelledError:
                    pass

    async def seek(self, timestamp: str) -> Dict[str, Any]:
        """
        Seek to a specific timestamp

        Args:
            timestamp: ISO timestamp to seek to

        Returns:
            Server response
        """
        return await self._http_client.post(
            "/api/v1/stream/seek",
            {
                "streamName": self._stream_name,
                "consumerGroup": self._consumer_group,
                "timestamp": timestamp,
            },
        )

