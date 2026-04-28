"""
Load balancer for distributing requests across multiple servers
Supports round-robin, session, and affinity-based routing with virtual nodes
"""

import time
from typing import Dict, List, Optional, Any


class LoadBalancer:
    """Load balancer with round-robin, session, and affinity strategies"""

    def __init__(
        self,
        urls: List[str],
        strategy: str = "round-robin",
        *,
        affinity_hash_ring: int = 128,
        health_retry_after_millis: int = 5000,
    ):
        """
        Initialize load balancer

        Args:
            urls: List of server URLs
            strategy: Load balancing strategy ('round-robin', 'session', 'affinity')
            affinity_hash_ring: Number of virtual nodes per server for affinity
            health_retry_after_millis: Retry unhealthy backends after N milliseconds
        """
        self._urls = [url.rstrip("/") for url in urls]  # Remove trailing slashes
        self._strategy = strategy
        self._current_index = 0
        self._session_map: Dict[str, int] = {}
        self._session_id = f"session_{int(time.time())}_{id(self)}"
        self._affinity_hash_ring = affinity_hash_ring
        self._health_retry_after_millis = health_retry_after_millis

        # Health status: url -> {'healthy': bool, 'failures': int, 'last_failure': float}
        self._health_status: Dict[str, Dict[str, Any]] = {
            url: {"healthy": True, "failures": 0, "last_failure": None} for url in self._urls
        }

        # Virtual nodes for affinity strategy
        self._virtual_nodes: Optional[List[Dict[str, Any]]] = None

        if strategy == "affinity":
            self._build_virtual_node_ring()

    def _build_virtual_node_ring(self) -> None:
        """Build virtual node ring for consistent hashing"""
        self._virtual_nodes = []

        for url in self._urls:
            for i in range(self._affinity_hash_ring):
                # Create virtual node identifier
                vnode_key = f"{url}#vnode{i}"
                hash_value = self._hash_string(vnode_key)

                self._virtual_nodes.append({"hash": hash_value, "real_server": url})

        # Sort by hash value to create the ring
        self._virtual_nodes.sort(key=lambda x: x["hash"])

    def _rebuild_virtual_node_ring(self, urls: Optional[List[str]] = None) -> None:
        """
        Rebuild virtual node ring with specified URLs

        Args:
            urls: URLs to include in ring (if None, uses all healthy URLs)
        """
        urls_to_include = (
            urls
            if urls is not None
            else [url for url in self._urls if self._health_status[url]["healthy"]]
        )

        self._virtual_nodes = []

        for url in urls_to_include:
            for i in range(self._affinity_hash_ring):
                vnode_key = f"{url}#vnode{i}"
                hash_value = self._hash_string(vnode_key)

                self._virtual_nodes.append({"hash": hash_value, "real_server": url})

        self._virtual_nodes.sort(key=lambda x: x["hash"])

    def get_next_url(self, session_key: Optional[str] = None) -> str:
        """
        Get next URL based on strategy

        Args:
            session_key: Optional session key for affinity or session routing

        Returns:
            Next URL to use
        """
        key = session_key or self._session_id

        # Get healthy URLs + unhealthy URLs that are ready for retry
        now = time.time()
        need_ring_rebuild = False

        healthy_urls = []
        for url in self._urls:
            status = self._health_status[url]
            if status["healthy"]:
                healthy_urls.append(url)
            else:
                # Allow retry of unhealthy backends after configured interval
                last_failure = status["last_failure"]
                if last_failure and (now - last_failure) >= (
                    self._health_retry_after_millis / 1000.0
                ):
                    need_ring_rebuild = True  # Need to rebuild ring to include this server
                    healthy_urls.append(url)

        # Rebuild vnode ring if we have servers eligible for retry
        if need_ring_rebuild and self._virtual_nodes is not None:
            self._rebuild_virtual_node_ring(healthy_urls)

        if not healthy_urls:
            # All backends unhealthy, try any backend as fallback
            return self._urls[self._current_index % len(self._urls)]

        if self._strategy == "affinity":
            # Virtual node consistent hashing
            return self._get_affinity_url(key, healthy_urls)

        if self._strategy == "session":
            # Session affinity: stick to the same server per session
            if key not in self._session_map:
                assigned_index = self._current_index
                self._session_map[key] = assigned_index
                self._current_index = (self._current_index + 1) % len(healthy_urls)

            assigned_url = self._urls[self._session_map[key]]
            # If assigned URL is unhealthy, reassign
            if assigned_url not in healthy_urls:
                new_index = self._current_index % len(healthy_urls)
                self._session_map[key] = new_index
                self._current_index = (self._current_index + 1) % len(healthy_urls)
                return healthy_urls[new_index]

            return assigned_url

        # Round robin: cycle through healthy URLs only
        url = healthy_urls[self._current_index % len(healthy_urls)]
        self._current_index = (self._current_index + 1) % len(healthy_urls)
        return url

    def _get_affinity_url(self, key: str, healthy_urls: List[str]) -> str:
        """
        Affinity-based routing using consistent hashing with virtual nodes

        Args:
            key: Affinity key
            healthy_urls: List of healthy URLs

        Returns:
            URL to use
        """
        if not key or not self._virtual_nodes or not self._virtual_nodes:
            # No key or no vnodes, fall back to round-robin
            url = healthy_urls[self._current_index % len(healthy_urls)]
            self._current_index = (self._current_index + 1) % len(healthy_urls)
            return url

        # Hash the consumer group key
        key_hash = self._hash_string(key)

        # Binary search to find the first vnode >= keyHash (clockwise on ring)
        left = 0
        right = len(self._virtual_nodes) - 1
        result = 0

        while left <= right:
            mid = (left + right) // 2
            if self._virtual_nodes[mid]["hash"] >= key_hash:
                result = mid
                right = mid - 1
            else:
                left = mid + 1

        # Walk forward on the ring to find first healthy server
        start_idx = result
        for i in range(len(self._virtual_nodes)):
            idx = (start_idx + i) % len(self._virtual_nodes)
            vnode = self._virtual_nodes[idx]

            if vnode["real_server"] in healthy_urls:
                return vnode["real_server"]

        # Fallback (should never happen if healthy_urls is not empty)
        return healthy_urls[0]

    def _hash_string(self, s: str) -> int:
        """
        FNV-1a hash function for consistent hashing

        Args:
            s: String to hash

        Returns:
            Unsigned 32-bit integer hash
        """
        hash_value = 2166136261  # FNV offset basis
        for char in s:
            hash_value ^= ord(char)
            hash_value += (
                (hash_value << 1)
                + (hash_value << 4)
                + (hash_value << 7)
                + (hash_value << 8)
                + (hash_value << 24)
            )
            # Keep it in 32-bit range
            hash_value &= 0xFFFFFFFF
        return hash_value

    def mark_unhealthy(self, url: str) -> None:
        """
        Mark a backend as unhealthy after failure

        Args:
            url: URL to mark unhealthy
        """
        status = self._health_status.get(url)
        if status:
            status["healthy"] = False
            status["failures"] += 1
            status["last_failure"] = time.time()

            # Rebuild vnode ring to exclude unhealthy server
            if self._virtual_nodes is not None:
                self._rebuild_virtual_node_ring()

    def mark_healthy(self, url: str) -> None:
        """
        Mark a backend as healthy after success

        Args:
            url: URL to mark healthy
        """
        status = self._health_status.get(url)
        if not status:
            return

        was_unhealthy = not status["healthy"]

        status["healthy"] = True
        status["failures"] = 0
        status["last_failure"] = None

        # Rebuild vnode ring to include recovered server
        if self._virtual_nodes is not None and was_unhealthy:
            self._rebuild_virtual_node_ring()

    def get_health_status(self) -> Dict[str, Dict[str, Any]]:
        """Get health status of all backends"""
        return dict(self._health_status)

    def get_all_urls(self) -> List[str]:
        """Get all URLs"""
        return list(self._urls)

    def get_strategy(self) -> str:
        """Get load balancing strategy"""
        return self._strategy

    def get_virtual_node_count(self) -> int:
        """Get number of virtual nodes in the ring"""
        return len(self._virtual_nodes) if self._virtual_nodes else 0

    def reset(self) -> None:
        """Reset load balancer state"""
        self._current_index = 0
        self._session_map.clear()

