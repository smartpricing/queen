"""
HTTP client with retry, load balancing, and failover support
"""

import asyncio
from typing import Any, Dict, Optional, Union
import httpx

from ..utils import logger
from .load_balancer import LoadBalancer


class HttpClient:
    """HTTP client with retry, load balancing, and failover support"""

    def __init__(
        self,
        *,
        base_url: Optional[str] = None,
        load_balancer: Optional[LoadBalancer] = None,
        timeout_millis: int = 30000,
        retry_attempts: int = 3,
        retry_delay_millis: int = 1000,
        enable_failover: bool = True,
        bearer_token: Optional[str] = None,
    ):
        """
        Initialize HTTP client

        Args:
            base_url: Base URL for single server
            load_balancer: LoadBalancer instance for multiple servers
            timeout_millis: Request timeout in milliseconds
            retry_attempts: Number of retry attempts
            retry_delay_millis: Initial retry delay (exponential backoff)
            enable_failover: Enable automatic failover
            bearer_token: Bearer token for proxy authentication
        """
        self._base_url = base_url
        self._load_balancer = load_balancer
        self._timeout_millis = timeout_millis
        self._retry_attempts = retry_attempts
        self._retry_delay_millis = retry_delay_millis
        self._enable_failover = enable_failover
        self._bearer_token = bearer_token

        # Build headers with optional auth
        headers = {}
        if bearer_token:
            headers["Authorization"] = f"Bearer {bearer_token}"

        # Create httpx.AsyncClient (persistent connection pool)
        self._client = httpx.AsyncClient(
            timeout=httpx.Timeout(timeout_millis / 1000.0),
            limits=httpx.Limits(max_keepalive_connections=10, max_connections=100),
            headers=headers,
        )

        logger.log(
            "HttpClient.constructor",
            {
                "has_load_balancer": load_balancer is not None,
                "base_url": base_url or "load-balanced",
                "timeout_millis": timeout_millis,
                "retry_attempts": retry_attempts,
                "enable_failover": enable_failover,
                "has_auth": bearer_token is not None,
            },
        )

    async def get(
        self,
        path: str,
        request_timeout_millis: Optional[int] = None,
        affinity_key: Optional[str] = None,
    ) -> Any:
        """GET request"""
        return await self._request_with_failover(
            "GET", path, None, request_timeout_millis, affinity_key
        )

    async def post(
        self,
        path: str,
        body: Optional[Dict[str, Any]] = None,
        request_timeout_millis: Optional[int] = None,
        affinity_key: Optional[str] = None,
    ) -> Any:
        """POST request"""
        return await self._request_with_failover(
            "POST", path, body, request_timeout_millis, affinity_key
        )

    async def put(
        self,
        path: str,
        body: Optional[Dict[str, Any]] = None,
        request_timeout_millis: Optional[int] = None,
        affinity_key: Optional[str] = None,
    ) -> Any:
        """PUT request"""
        return await self._request_with_failover(
            "PUT", path, body, request_timeout_millis, affinity_key
        )

    async def delete(
        self,
        path: str,
        request_timeout_millis: Optional[int] = None,
        affinity_key: Optional[str] = None,
    ) -> Any:
        """DELETE request"""
        return await self._request_with_failover(
            "DELETE", path, None, request_timeout_millis, affinity_key
        )

    async def _execute_request(
        self,
        url: str,
        method: str,
        body: Optional[Dict[str, Any]],
        request_timeout_millis: Optional[int],
    ) -> Any:
        """Execute single HTTP request"""
        effective_timeout = request_timeout_millis or self._timeout_millis
        logger.log(
            "HttpClient.request",
            {"method": method, "url": url, "has_body": body is not None, "timeout": effective_timeout},
        )

        try:
            # Set timeout for this specific request
            timeout = httpx.Timeout(effective_timeout / 1000.0)

            # Prepare request
            kwargs: Dict[str, Any] = {
                "method": method,
                "url": url,
                "timeout": timeout,
            }

            if body:
                kwargs["json"] = body

            response = await self._client.request(**kwargs)

            logger.log("HttpClient.response", {"method": method, "url": url, "status": response.status_code})

            # Handle 204 No Content
            if response.status_code == 204:
                return None

            # Handle errors
            if not response.is_success:
                error_msg = f"HTTP {response.status_code}: {response.reason_phrase}"
                try:
                    text = response.text
                    if text:
                        body_data = response.json()
                        error_msg = body_data.get("error", error_msg)
                except Exception:
                    pass

                logger.error(
                    "HttpClient.request",
                    {"method": method, "url": url, "status": response.status_code, "error": error_msg},
                )
                error = httpx.HTTPStatusError(error_msg, request=response.request, response=response)
                raise error

            # Parse successful response
            content_type = response.headers.get("content-type", "")
            content_length = response.headers.get("content-length", "")

            if (
                not content_type
                or "application/json" not in content_type
                or content_length == "0"
            ):
                text = response.text
                if not text:
                    return None
                try:
                    return response.json()
                except Exception:
                    return None

            return response.json()

        except httpx.TimeoutException as e:
            logger.error(
                "HttpClient.request",
                {"method": method, "url": url, "error": "timeout", "timeout": effective_timeout},
            )
            raise
        except Exception as e:
            logger.error("HttpClient.request", {"method": method, "url": url, "error": str(e)})
            raise

    async def _request_with_retry(
        self,
        method: str,
        path: str,
        body: Optional[Dict[str, Any]],
        request_timeout_millis: Optional[int],
    ) -> Any:
        """Execute request with retry logic"""
        last_error: Optional[Exception] = None

        for attempt in range(self._retry_attempts):
            try:
                url = self._get_url() + path
                return await self._execute_request(url, method, body, request_timeout_millis)
            except Exception as error:
                last_error = error

                # Don't retry on client errors (4xx)
                if isinstance(error, httpx.HTTPStatusError):
                    status = error.response.status_code
                    if 400 <= status < 500:
                        raise error

                # Wait before retry (except on last attempt)
                if attempt < self._retry_attempts - 1:
                    delay = (self._retry_delay_millis / 1000.0) * (2**attempt)
                    logger.warn(
                        "HttpClient.retry",
                        {
                            "method": method,
                            "path": path,
                            "attempt": attempt + 1,
                            "delay": delay,
                            "error": str(error),
                        },
                    )
                    await asyncio.sleep(delay)

        logger.error(
            "HttpClient.retry",
            {"method": method, "path": path, "error": "Max retries exceeded", "attempts": self._retry_attempts},
        )
        if last_error:
            raise last_error
        raise Exception("Max retries exceeded")

    async def _request_with_failover(
        self,
        method: str,
        path: str,
        body: Optional[Dict[str, Any]],
        request_timeout_millis: Optional[int],
        affinity_key: Optional[str],
    ) -> Any:
        """Execute request with failover logic"""
        if not self._load_balancer or not self._enable_failover:
            return await self._request_with_retry(method, path, body, request_timeout_millis)

        urls = self._load_balancer.get_all_urls()
        attempted_urls = set()
        last_error: Optional[Exception] = None

        logger.log(
            "HttpClient.failover",
            {"method": method, "path": path, "total_servers": len(urls), "affinity_key": affinity_key},
        )

        for _ in range(len(urls)):
            # Pass affinity key to load balancer for consistent routing
            url = self._load_balancer.get_next_url(affinity_key)

            if url in attempted_urls:
                continue

            attempted_urls.add(url)

            try:
                result = await self._execute_request(url + path, method, body, request_timeout_millis)

                # Mark backend as healthy on success
                self._load_balancer.mark_healthy(url)

                return result
            except Exception as error:
                last_error = error

                # Mark backend as unhealthy on failure (5xx or network errors)
                if isinstance(error, httpx.HTTPStatusError):
                    status = error.response.status_code
                    if status >= 500:
                        self._load_balancer.mark_unhealthy(url)
                elif isinstance(error, (httpx.NetworkError, httpx.TimeoutException)):
                    self._load_balancer.mark_unhealthy(url)

                logger.warn(
                    "HttpClient.failover", {"url": url, "method": method, "path": path, "error": str(error)}
                )
                print(f"Request failed for {url}: {method} {path} - {error}")

                # Don't retry on client errors (4xx)
                if isinstance(error, httpx.HTTPStatusError):
                    status = error.response.status_code
                    if 400 <= status < 500:
                        raise error

                # Continue to next server for server errors or network issues

        logger.error(
            "HttpClient.failover",
            {"method": method, "path": path, "error": "All servers failed", "attempted": len(attempted_urls)},
        )
        if last_error:
            raise last_error
        raise Exception("All servers failed")

    def _get_url(self) -> str:
        """Get URL from load balancer or base URL"""
        if self._load_balancer:
            return self._load_balancer.get_next_url()
        return self._base_url or ""

    def get_load_balancer(self) -> Optional[LoadBalancer]:
        """Get load balancer instance"""
        return self._load_balancer

    async def close(self) -> None:
        """Close HTTP client and connection pool"""
        await self._client.aclose()

