"""
Validation utilities
"""

import re
from typing import List

UUID_V4_REGEX = re.compile(
    r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$", re.IGNORECASE
)


def is_valid_uuid(s: str) -> bool:
    """Check if string is a valid UUID v4"""
    return isinstance(s, str) and UUID_V4_REGEX.match(s) is not None


def validate_queue_name(name: str) -> str:
    """Validate and normalize queue name"""
    if not isinstance(name, str) or not name.strip():
        raise ValueError("Queue name must be a non-empty string")
    return name.strip()


def validate_url(url: str) -> str:
    """Validate URL"""
    if not isinstance(url, str) or not url.startswith("http"):
        raise ValueError(f"Invalid URL: {url}")
    return url


def validate_urls(urls: List[str]) -> List[str]:
    """Validate list of URLs"""
    if not isinstance(urls, list) or not urls:
        raise ValueError("URLs must be a non-empty list")
    return [validate_url(url) for url in urls]

