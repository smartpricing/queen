<?php

namespace Queen\Buffer;

use Queen\Http\HttpClient;
use Queen\Support\Defaults;

class BufferManager
{
    private HttpClient $httpClient;
    /** @var array<string, MessageBuffer> */
    private array $buffers = [];
    private int $flushCount = 0;

    public function __construct(HttpClient $httpClient)
    {
        $this->httpClient = $httpClient;
    }

    public function addMessage(string $queueAddress, array $formattedMessage, array $bufferOptions): void
    {
        $options = array_merge(Defaults::BUFFER_DEFAULTS, $bufferOptions);

        if (!isset($this->buffers[$queueAddress])) {
            $this->buffers[$queueAddress] = new MessageBuffer(
                $queueAddress,
                $options,
                fn(string $addr) => $this->doFlush($addr)
            );
        }

        $buffer = $this->buffers[$queueAddress];

        // Check time-based trigger before adding (since PHP has no background timers)
        $buffer->checkTimeTrigger();

        $buffer->add($formattedMessage);
    }

    private function doFlush(string $queueAddress): void
    {
        $buffer = $this->buffers[$queueAddress] ?? null;
        if ($buffer === null || $buffer->getMessageCount() === 0) {
            return;
        }

        $buffer->setFlushing(true);

        $messages = $buffer->extractMessages();
        if (empty($messages)) {
            $buffer->setFlushing(false);
            return;
        }

        try {
            $this->httpClient->post('/api/v1/push', ['items' => $messages]);
            $this->flushCount++;
            unset($this->buffers[$queueAddress]);
        } catch (\Throwable $error) {
            $buffer->restoreMessages($messages);
            throw $error;
        }
    }

    public function flushBuffer(string $queueAddress): void
    {
        $buffer = $this->buffers[$queueAddress] ?? null;
        if ($buffer === null) {
            return;
        }

        $batchSize = $buffer->getOptions()['messageCount'];

        while ($buffer->getMessageCount() > 0) {
            $buffer->setFlushing(true);
            $messages = $buffer->extractMessages($batchSize);
            if (empty($messages)) {
                break;
            }
            try {
                $this->httpClient->post('/api/v1/push', ['items' => $messages]);
                $this->flushCount++;
            } catch (\Throwable $error) {
                $buffer->restoreMessages($messages);
                throw $error;
            }
        }

        unset($this->buffers[$queueAddress]);
    }

    /**
     * Flush all buffers concurrently using async HTTP.
     * Each buffer's batches are extracted up front, then all sent in parallel.
     */
    public function flushAllBuffers(): void
    {
        $addresses = array_keys($this->buffers);

        if (empty($addresses)) {
            return;
        }

        // Collect all batches from all buffers
        $batches = []; // [[messages, address], ...]
        foreach ($addresses as $address) {
            $buffer = $this->buffers[$address] ?? null;
            if ($buffer === null || $buffer->getMessageCount() === 0) {
                continue;
            }

            $batchSize = $buffer->getOptions()['messageCount'];
            $buffer->setFlushing(true);

            while ($buffer->getMessageCount() > 0) {
                $messages = $buffer->extractMessages($batchSize);
                if (!empty($messages)) {
                    $batches[] = [$messages, $address];
                }
            }
        }

        if (empty($batches)) {
            foreach ($addresses as $address) {
                unset($this->buffers[$address]);
            }
            return;
        }

        // Send all batches concurrently
        $promises = [];
        foreach ($batches as $i => [$messages, $address]) {
            $promises[$i] = $this->httpClient->postAsync('/api/v1/push', ['items' => $messages]);
        }

        $results = HttpClient::settleAll($promises);

        // Check for failures — restore messages for failed batches
        $errors = [];
        foreach ($results as $i => $outcome) {
            if ($outcome['state'] === 'fulfilled') {
                $this->flushCount++;
            } else {
                [$failedMessages, $failedAddress] = $batches[$i];
                // Re-create buffer if needed and restore messages
                if (!isset($this->buffers[$failedAddress])) {
                    $this->buffers[$failedAddress] = new MessageBuffer(
                        $failedAddress,
                        Defaults::BUFFER_DEFAULTS,
                        fn(string $addr) => $this->doFlush($addr)
                    );
                }
                $this->buffers[$failedAddress]->restoreMessages($failedMessages);
                $errors[] = $outcome['reason'];
            }
        }

        // Clean up only successful buffers
        foreach ($addresses as $address) {
            $buffer = $this->buffers[$address] ?? null;
            if ($buffer !== null && $buffer->getMessageCount() === 0) {
                unset($this->buffers[$address]);
            }
        }

        // Throw first error if any failed
        if (!empty($errors)) {
            throw $errors[0];
        }
    }

    public function getStats(): array
    {
        $totalBufferedMessages = 0;
        $oldestBufferAge = 0.0;

        foreach ($this->buffers as $buffer) {
            $totalBufferedMessages += $buffer->getMessageCount();
            $age = $buffer->getFirstMessageAge();
            $oldestBufferAge = max($oldestBufferAge, $age);
        }

        return [
            'activeBuffers' => count($this->buffers),
            'totalBufferedMessages' => $totalBufferedMessages,
            'oldestBufferAge' => $oldestBufferAge,
            'flushesPerformed' => $this->flushCount,
        ];
    }

    public function cleanup(): void
    {
        foreach ($this->buffers as $buffer) {
            $buffer->cleanup();
        }
        $this->buffers = [];
    }
}
