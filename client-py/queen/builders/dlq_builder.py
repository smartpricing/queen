"""
DLQ (Dead Letter Queue) builder for querying failed messages
"""

from typing import Any, Dict, Optional

from ..utils import logger


class DLQBuilder:
    """DLQ (Dead Letter Queue) builder for querying failed messages"""

    def __init__(
        self,
        http_client: Any,
        queue_name: str,
        consumer_group: Optional[str],
        partition: Optional[str],
    ):
        """
        Initialize DLQ builder

        Args:
            http_client: HttpClient instance
            queue_name: Queue name
            consumer_group: Optional consumer group
            partition: Optional partition
        """
        self._http_client = http_client
        self._queue_name = queue_name
        self._consumer_group = consumer_group
        self._partition = partition if partition and partition != "Default" else None
        self._limit = 100
        self._offset = 0
        self._from: Optional[str] = None
        self._to: Optional[str] = None

    def limit(self, count: int) -> "DLQBuilder":
        """
        Set limit

        Args:
            count: Number of messages to return

        Returns:
            Self for chaining
        """
        self._limit = max(1, count)
        return self

    def offset(self, count: int) -> "DLQBuilder":
        """
        Set offset

        Args:
            count: Offset for pagination

        Returns:
            Self for chaining
        """
        self._offset = max(0, count)
        return self

    def from_(self, timestamp: str) -> "DLQBuilder":
        """
        Set from timestamp (using from_ to avoid Python keyword)

        Args:
            timestamp: From timestamp

        Returns:
            Self for chaining
        """
        self._from = timestamp
        return self

    def to(self, timestamp: str) -> "DLQBuilder":
        """
        Set to timestamp

        Args:
            timestamp: To timestamp

        Returns:
            Self for chaining
        """
        self._to = timestamp
        return self

    async def get(self) -> Dict[str, Any]:
        """
        Execute DLQ query

        Returns:
            DLQ response with messages and total
        """
        from urllib.parse import urlencode

        params: Dict[str, str] = {
            "queue": self._queue_name,
            "limit": str(self._limit),
            "offset": str(self._offset),
        }

        if self._consumer_group:
            params["consumerGroup"] = self._consumer_group

        if self._partition:
            params["partition"] = self._partition

        if self._from:
            params["from"] = self._from

        if self._to:
            params["to"] = self._to

        logger.log(
            "DLQBuilder.get",
            {
                "queue": self._queue_name,
                "consumer_group": self._consumer_group,
                "partition": self._partition,
                "limit": self._limit,
                "offset": self._offset,
            },
        )

        try:
            query_string = urlencode(params)
            result = await self._http_client.get(f"/api/v1/dlq?{query_string}")
            logger.log(
                "DLQBuilder.get",
                {
                    "status": "success",
                    "total": result.get("total", 0) if result else 0,
                    "messages": len(result.get("messages", [])) if result else 0,
                },
            )
            return result or {"messages": [], "total": 0}
        except Exception as error:
            logger.error("DLQBuilder.get", {"error": str(error)})
            print(f"DLQ query failed: {error}")
            return {"messages": [], "total": 0}

