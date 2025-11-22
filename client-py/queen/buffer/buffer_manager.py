"""
Buffer manager for client-side message buffering across queues
"""

import asyncio
from typing import Any, Dict, Set

from ..utils import logger
from ..utils.defaults import BUFFER_DEFAULTS
from .message_buffer import MessageBuffer


class BufferManager:
    """Buffer manager for client-side message buffering across queues"""

    def __init__(self, http_client: Any):  # Type: HttpClient but avoiding circular import
        """
        Initialize buffer manager

        Args:
            http_client: HttpClient instance
        """
        self._http_client = http_client
        self._buffers: Dict[str, MessageBuffer] = {}  # queueAddress -> MessageBuffer
        self._pending_flushes: Set[asyncio.Task[None]] = set()
        self._flush_count = 0
        self._lock = asyncio.Lock()

    def add_message(
        self, queue_address: str, formatted_message: Dict[str, Any], buffer_options: Dict[str, Any]
    ) -> None:
        """
        Add message to buffer

        Args:
            queue_address: Queue address (queue/partition)
            formatted_message: Formatted message dict
            buffer_options: Buffer options
        """
        options = {**BUFFER_DEFAULTS, **buffer_options}

        if queue_address not in self._buffers:
            logger.log("BufferManager.createBuffer", {"queue_address": queue_address, "options": options})
            self._buffers[queue_address] = MessageBuffer(
                queue_address, options, lambda addr: self._schedule_flush(addr)
            )

        buffer = self._buffers[queue_address]
        buffer.add(formatted_message)
        logger.log("BufferManager.addMessage", {"queue_address": queue_address, "message_count": buffer.message_count})

    def _schedule_flush(self, queue_address: str) -> asyncio.Task[None]:
        """
        Schedule a flush (called from MessageBuffer timer)

        Args:
            queue_address: Queue address to flush

        Returns:
            Task for the flush operation
        """
        task = asyncio.create_task(self._flush_buffer(queue_address))
        return task

    async def _flush_buffer(self, queue_address: str) -> None:
        """
        Flush buffer for a queue address

        Args:
            queue_address: Queue address to flush
        """
        async with self._lock:
            buffer = self._buffers.get(queue_address)
            if not buffer or buffer.message_count == 0:
                logger.log("BufferManager.flushBuffer", {"queue_address": queue_address, "status": "empty"})
                print(f"No buffer or empty buffer for {queue_address}")
                return

            logger.log("BufferManager.flushBuffer", {"queue_address": queue_address, "message_count": buffer.message_count})
            print(f"Flushing {buffer.message_count} messages for {queue_address}")
            buffer.set_flushing(True)

        # Create a promise for this flush and track it
        flush_promise = asyncio.create_task(self._do_flush(queue_address, buffer))
        self._pending_flushes.add(flush_promise)

        # Remove from pending when done
        flush_promise.add_done_callback(lambda t: self._pending_flushes.discard(t))

        await flush_promise

    async def _do_flush(self, queue_address: str, buffer: MessageBuffer) -> None:
        """
        Actually perform the flush

        Args:
            queue_address: Queue address
            buffer: MessageBuffer instance
        """
        try:
            messages = buffer.extract_messages()

            print(f"Extracted {len(messages)} messages, sending to server...")

            if not messages:
                return

            # Send to server
            result = await self._http_client.post("/api/v1/push", {"items": messages})
            print(f"Server responded: {len(result) if result else 'N/A'} items")

            self._flush_count += 1
            logger.log(
                "BufferManager.flushBuffer",
                {"queue_address": queue_address, "status": "success", "messages_sent": len(messages)},
            )

            # Remove empty buffer
            async with self._lock:
                if queue_address in self._buffers and self._buffers[queue_address].message_count == 0:
                    del self._buffers[queue_address]

        except Exception as error:
            logger.error("BufferManager.flushBuffer", {"queue_address": queue_address, "error": str(error)})
            print(f"Flush error for {queue_address}: {error}")
            buffer.set_flushing(False)
            raise

    async def _flush_buffer_batch(self, queue_address: str, batch_size: int) -> None:
        """
        Flush a batch of messages from buffer

        Args:
            queue_address: Queue address
            batch_size: Number of messages to flush
        """
        async with self._lock:
            buffer = self._buffers.get(queue_address)
            if not buffer or buffer.message_count == 0:
                return

            buffer.set_flushing(True)

        # Create a promise for this flush and track it
        flush_promise = asyncio.create_task(self._do_flush_batch(queue_address, buffer, batch_size))
        self._pending_flushes.add(flush_promise)

        # Remove from pending when done
        flush_promise.add_done_callback(lambda t: self._pending_flushes.discard(t))

        await flush_promise

    async def _do_flush_batch(self, queue_address: str, buffer: MessageBuffer, batch_size: int) -> None:
        """
        Actually perform the batch flush

        Args:
            queue_address: Queue address
            buffer: MessageBuffer instance
            batch_size: Number of messages to flush
        """
        try:
            messages = buffer.extract_messages(batch_size)

            print(f"Extracted {len(messages)} messages, sending to server...")

            if not messages:
                return

            # Send to server
            result = await self._http_client.post("/api/v1/push", {"items": messages})
            print(f"Server responded: {len(result) if result else 'N/A'} items")

            self._flush_count += 1

            # Remove empty buffer if no more messages
            async with self._lock:
                if queue_address in self._buffers:
                    if self._buffers[queue_address].message_count == 0:
                        del self._buffers[queue_address]
                    else:
                        self._buffers[queue_address].set_flushing(False)

        except Exception as error:
            print(f"Flush error for {queue_address}: {error}")
            buffer.set_flushing(False)
            raise

    async def flush_buffer(self, queue_address: str) -> None:
        """
        Flush all messages for a queue address

        Args:
            queue_address: Queue address to flush
        """
        logger.log(
            "BufferManager.flushBuffer",
            {
                "queue_address": queue_address,
                "active_buffers": len(self._buffers),
                "pending_flushes": len(self._pending_flushes),
            },
        )
        print(f"flushBuffer called for address: {queue_address}")
        print(f"Active buffers: {list(self._buffers.keys())}")
        print(f"Pending flushes: {len(self._pending_flushes)}")

        async with self._lock:
            buffer = self._buffers.get(queue_address)
            if not buffer:
                logger.log("BufferManager.flushBuffer", {"queue_address": queue_address, "status": "not-found"})
                print(f"No buffer found for {queue_address}")
                await self._wait_for_pending_flushes()
                return

            # Cancel timer to prevent time-based flush
            buffer.cancel_timer()

            # Get the batch size from buffer options
            batch_size = buffer.options["message_count"]

        # Flush all messages in batches
        while True:
            async with self._lock:
                buffer = self._buffers.get(queue_address)
                if not buffer or buffer.message_count == 0:
                    break

            print(f"Flushing batch of up to {batch_size} messages ({buffer.message_count} remaining)")
            await self._flush_buffer_batch(queue_address, batch_size)

        # Wait for all pending flushes to complete
        await self._wait_for_pending_flushes()

        print(f"flushBuffer completed for {queue_address}")

    async def flush_all_buffers(self) -> None:
        """Flush all buffers"""
        async with self._lock:
            queue_addresses = list(self._buffers.keys())

        logger.log(
            "BufferManager.flushAllBuffers",
            {"buffer_count": len(queue_addresses), "pending_flushes": len(self._pending_flushes)},
        )
        print(f"flushAllBuffers called, pending flushes: {len(self._pending_flushes)}")

        # Flush each buffer
        for queue_address in queue_addresses:
            await self.flush_buffer(queue_address)

        logger.log("BufferManager.flushAllBuffers", {"status": "completed"})
        print("flushAllBuffers completed")

    async def _wait_for_pending_flushes(self) -> None:
        """Wait for all pending flush tasks"""
        if not self._pending_flushes:
            return

        print(f"Waiting for {len(self._pending_flushes)} pending flushes...")
        await asyncio.gather(*list(self._pending_flushes), return_exceptions=True)
        print("All pending flushes completed")

    def get_stats(self) -> Dict[str, Any]:
        """
        Get buffer statistics

        Returns:
            Dict with buffer stats
        """
        total_buffered_messages = 0
        oldest_buffer_age = 0.0

        for buffer in self._buffers.values():
            total_buffered_messages += buffer.message_count
            age = buffer.first_message_age
            oldest_buffer_age = max(oldest_buffer_age, age)

        stats = {
            "activeBuffers": len(self._buffers),
            "totalBufferedMessages": total_buffered_messages,
            "oldestBufferAge": oldest_buffer_age,
            "flushesPerformed": self._flush_count,
        }

        logger.log("BufferManager.getStats", stats)
        return stats

    def cleanup(self) -> None:
        """Cleanup all buffers"""
        logger.log("BufferManager.cleanup", {"buffer_count": len(self._buffers)})
        for buffer in self._buffers.values():
            buffer.cleanup()
        self._buffers.clear()

