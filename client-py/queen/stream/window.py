"""
Window - Represents a time window of messages with utility methods
"""

from functools import reduce
from typing import Any, Callable, Dict, List, Optional


def _get_path(obj: Dict[str, Any], path: str) -> Any:
    """
    Get nested property value from object using dot notation

    Args:
        obj: Object to get property from
        path: Dot-separated path (e.g., 'data.userId')

    Returns:
        Property value or None
    """

    def reducer(o: Any, k: str) -> Any:
        if o is not None and isinstance(o, dict) and k in o:
            return o[k]
        return None

    return reduce(reducer, path.split("."), obj)


class Window:
    """Window - Represents a time window of messages with utility methods"""

    def __init__(self, raw_window: Dict[str, Any]):
        """
        Initialize window

        Args:
            raw_window: Raw window data from server
        """
        # Copy all properties from raw window
        self.__dict__.update(raw_window)

        # Store immutable original messages
        self.all_messages = tuple(raw_window.get("messages", []))

        # Working copy for transformations
        self.messages = list(self.all_messages)

    def filter(self, filter_fn: Callable[[Dict[str, Any]], bool]) -> "Window":
        """
        Filter messages based on a predicate function

        Args:
            filter_fn: Predicate function (msg) => bool

        Returns:
            Self for chaining
        """
        self.messages = [msg for msg in self.messages if filter_fn(msg)]
        return self

    def group_by(self, key_path: str) -> Dict[str, List[Dict[str, Any]]]:
        """
        Group messages by a key path (dot notation supported)

        Args:
            key_path: Path to key in message data (e.g., 'data.userId')

        Returns:
            Dict with keys as group names and values as arrays of messages
        """
        groups: Dict[str, List[Dict[str, Any]]] = {}
        for msg in self.messages:
            key = _get_path(msg, key_path) or "null_key"
            key_str = str(key)
            if key_str not in groups:
                groups[key_str] = []
            groups[key_str].append(msg)
        return groups

    def aggregate(self, config: Dict[str, Any]) -> Dict[str, Any]:
        """
        Aggregate messages using various aggregation functions

        Args:
            config: Aggregation configuration
                - count: bool - Count messages
                - sum: List[str] - Array of paths to sum
                - avg: List[str] - Array of paths to average
                - min: List[str] - Array of paths to find minimum
                - max: List[str] - Array of paths to find maximum

        Returns:
            Aggregation results
        """
        results: Dict[str, Any] = {}

        # Count
        if config.get("count"):
            results["count"] = len(self.messages)

        # Sum
        if config.get("sum"):
            results["sum"] = {}
            for path in config["sum"]:
                total = sum(
                    val
                    for msg in self.messages
                    if (val := _get_path(msg, path)) is not None and isinstance(val, (int, float))
                )
                results["sum"][path] = total

        # Average
        if config.get("avg"):
            results["avg"] = {}
            for path in config["avg"]:
                values = [
                    val
                    for msg in self.messages
                    if (val := _get_path(msg, path)) is not None and isinstance(val, (int, float))
                ]
                results["avg"][path] = sum(values) / len(values) if values else 0

        # Min
        if config.get("min"):
            results["min"] = {}
            for path in config["min"]:
                values = [
                    val
                    for msg in self.messages
                    if (val := _get_path(msg, path)) is not None and isinstance(val, (int, float))
                ]
                results["min"][path] = min(values) if values else None

        # Max
        if config.get("max"):
            results["max"] = {}
            for path in config["max"]:
                values = [
                    val
                    for msg in self.messages
                    if (val := _get_path(msg, path)) is not None and isinstance(val, (int, float))
                ]
                results["max"][path] = max(values) if values else None

        return results

    def reset(self) -> "Window":
        """
        Reset working messages to the original frozen set

        Returns:
            Self for chaining
        """
        self.messages = list(self.all_messages)
        return self

    def size(self) -> int:
        """Get count of messages in working set"""
        return len(self.messages)

    def original_size(self) -> int:
        """Get count of original messages"""
        return len(self.all_messages)

