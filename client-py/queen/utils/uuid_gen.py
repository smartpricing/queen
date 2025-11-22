"""
UUID generation utilities
"""

import uuid

# Try to use uuid7 if available, otherwise fallback to uuid4
try:
    from uuid_extensions import uuid7  # type: ignore

    def generate_uuid() -> str:
        """Generate UUIDv7"""
        return str(uuid7())

except ImportError:

    def generate_uuid() -> str:
        """Generate UUIDv4 (fallback when uuid7 not available)"""
        return str(uuid.uuid4())

