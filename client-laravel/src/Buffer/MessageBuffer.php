<?php

namespace Queen\Buffer;

class MessageBuffer
{
    private string $queueAddress;
    private array $messages = [];
    private array $options;
    private \Closure $flushCallback;
    private ?float $firstMessageTime = null;
    private bool $flushing = false;

    public function __construct(string $queueAddress, array $options, \Closure $flushCallback)
    {
        $this->queueAddress = $queueAddress;
        $this->options = $options;
        $this->flushCallback = $flushCallback;
    }

    public function add(array $formattedMessage): void
    {
        if (empty($this->messages)) {
            $this->firstMessageTime = microtime(true);
        }

        $this->messages[] = $formattedMessage;

        // Check if we should flush based on count
        if (count($this->messages) >= $this->options['messageCount']) {
            $this->triggerFlush();
        }
    }

    /**
     * Check time-based flush trigger. Call this periodically.
     */
    public function checkTimeTrigger(): void
    {
        if ($this->flushing || empty($this->messages) || $this->firstMessageTime === null) {
            return;
        }

        $elapsedMs = (microtime(true) - $this->firstMessageTime) * 1000;
        if ($elapsedMs >= $this->options['timeMillis']) {
            $this->triggerFlush();
        }
    }

    private function triggerFlush(): void
    {
        if ($this->flushing || empty($this->messages)) {
            return;
        }

        $this->flushing = true;
        ($this->flushCallback)($this->queueAddress);
    }

    public function extractMessages(?int $batchSize = null): array
    {
        if ($batchSize === null || $batchSize >= count($this->messages)) {
            $messages = $this->messages;
            $this->messages = [];
            $this->firstMessageTime = null;
            $this->flushing = false;
            return $messages;
        }

        $messages = array_splice($this->messages, 0, $batchSize);

        if (empty($this->messages)) {
            $this->firstMessageTime = null;
            $this->flushing = false;
        }

        return $messages;
    }

    /**
     * Restore messages to the front of the buffer (used on flush failure).
     */
    public function restoreMessages(array $messages): void
    {
        array_unshift($this->messages, ...$messages);
        if ($this->firstMessageTime === null && !empty($this->messages)) {
            $this->firstMessageTime = microtime(true);
        }
        $this->flushing = false;
    }

    public function setFlushing(bool $value): void
    {
        $this->flushing = $value;
    }

    public function getMessageCount(): int
    {
        return count($this->messages);
    }

    public function getOptions(): array
    {
        return $this->options;
    }

    public function getFirstMessageAge(): float
    {
        return $this->firstMessageTime !== null
            ? (microtime(true) - $this->firstMessageTime) * 1000
            : 0;
    }

    public function cleanup(): void
    {
        $this->messages = [];
        $this->firstMessageTime = null;
        $this->flushing = false;
    }
}
