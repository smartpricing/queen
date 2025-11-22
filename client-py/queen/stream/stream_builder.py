"""
StreamBuilder - Fluent API for defining streams
"""

from typing import Any, Dict, List


class StreamBuilder:
    """StreamBuilder - Fluent API for defining streams"""

    def __init__(self, http_client: Any, queen: Any, name: str, namespace: str):
        """
        Initialize stream builder

        Args:
            http_client: HttpClient instance
            queen: Queen instance
            name: Stream name
            namespace: Stream namespace
        """
        self._http_client = http_client
        self._queen = queen
        self._config: Dict[str, Any] = {
            "name": name,
            "namespace": namespace,
            "source_queue_names": [],
            "partitioned": False,
            "window_type": "tumbling",
            "window_duration_ms": 60000,
            "window_grace_period_ms": 30000,
            "window_lease_timeout_ms": 60000,
        }

    def sources(self, queue_names: List[str]) -> "StreamBuilder":
        """
        Set source queues

        Args:
            queue_names: Array of queue names

        Returns:
            Self for chaining
        """
        self._config["source_queue_names"] = queue_names
        return self

    def partitioned(self) -> "StreamBuilder":
        """
        Enable partitioned processing

        Returns:
            Self for chaining
        """
        self._config["partitioned"] = True
        return self

    def tumbling_time(self, seconds: int) -> "StreamBuilder":
        """
        Configure tumbling time window

        Args:
            seconds: Window duration in seconds

        Returns:
            Self for chaining
        """
        self._config["window_type"] = "tumbling"
        self._config["window_duration_ms"] = seconds * 1000
        return self

    def grace_period(self, seconds: int) -> "StreamBuilder":
        """
        Configure grace period

        Args:
            seconds: Grace period in seconds

        Returns:
            Self for chaining
        """
        self._config["window_grace_period_ms"] = seconds * 1000
        return self

    def lease_timeout(self, seconds: int) -> "StreamBuilder":
        """
        Configure lease timeout

        Args:
            seconds: Lease timeout in seconds

        Returns:
            Self for chaining
        """
        self._config["window_lease_timeout_ms"] = seconds * 1000
        return self

    async def define(self) -> Dict[str, Any]:
        """
        Define/create the stream on the server

        Returns:
            Server response
        """
        return await self._http_client.post("/api/v1/stream/define", self._config)

