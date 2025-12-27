"""
Queen MQ - High-performance message queue client for Python
"""

from .admin import Admin
from .client import Queen
from .types import Message, AckResponse, BufferStats, DLQResponse, TransactionResponse
from .utils.defaults import (
    CLIENT_DEFAULTS,
    QUEUE_DEFAULTS,
    CONSUME_DEFAULTS,
    POP_DEFAULTS,
    BUFFER_DEFAULTS,
)

__version__ = "0.12.0"

__all__ = [
    "Queen",
    "Admin",
    "Message",
    "AckResponse",
    "BufferStats",
    "DLQResponse",
    "TransactionResponse",
    "CLIENT_DEFAULTS",
    "QUEUE_DEFAULTS",
    "CONSUME_DEFAULTS",
    "POP_DEFAULTS",
    "BUFFER_DEFAULTS",
]

