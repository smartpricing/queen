"""
Type definitions for Queen client
"""

from typing import Any, Dict, List, Optional, Union, Callable, Awaitable
from typing_extensions import TypedDict, Protocol


class Message(TypedDict, total=False):
    """Message from Queen"""

    transactionId: str
    partitionId: str
    leaseId: Optional[str]
    queue: str
    partition: str
    data: Dict[str, Any]
    createdAt: str
    errorMessage: Optional[str]
    retryCount: int


class AckResponse(TypedDict, total=False):
    """Acknowledgment response"""

    success: bool
    error: Optional[str]


class BufferStats(TypedDict):
    """Buffer statistics"""

    activeBuffers: int
    totalBufferedMessages: int
    oldestBufferAge: float
    flushesPerformed: int


class DLQResponse(TypedDict):
    """Dead Letter Queue response"""

    messages: List[Message]
    total: int


class TransactionResponse(TypedDict, total=False):
    """Transaction response"""

    success: bool
    error: Optional[str]


# Type aliases for handlers
MessageHandler = Callable[[Message], Awaitable[Any]]
BatchMessageHandler = Callable[[List[Message]], Awaitable[Any]]
SuccessCallback = Callable[[Union[Message, List[Message]], Any], Awaitable[None]]
ErrorCallback = Callable[[Union[Message, List[Message]], Exception], Awaitable[None]]
DuplicateCallback = Callable[[List[Any], Exception], Awaitable[None]]


class TraceConfig(TypedDict, total=False):
    """Trace configuration"""

    traceName: Optional[Union[str, List[str]]]
    eventType: Optional[str]
    data: Dict[str, Any]


class TraceMethod(Protocol):
    """Protocol for message.trace() method"""

    async def __call__(self, trace_config: TraceConfig) -> Dict[str, Any]:
        ...


class MessageWithTrace(Message):
    """Message enhanced with trace method"""

    trace: TraceMethod

