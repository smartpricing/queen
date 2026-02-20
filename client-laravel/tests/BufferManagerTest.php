<?php

namespace Queen\Tests;

use PHPUnit\Framework\TestCase;
use Queen\Buffer\BufferManager;
use Queen\Http\HttpClient;

class BufferManagerTest extends TestCase
{
    public function testAddMessageCreatesBuffer(): void
    {
        $httpClient = $this->createStub(HttpClient::class);
        $manager = new BufferManager($httpClient);

        $stats = $manager->getStats();
        $this->assertSame(0, $stats['activeBuffers']);

        $manager->addMessage('queue/Default', ['payload' => 'test'], ['messageCount' => 100, 'timeMillis' => 1000]);

        $stats = $manager->getStats();
        $this->assertSame(1, $stats['activeBuffers']);
        $this->assertSame(1, $stats['totalBufferedMessages']);
    }

    public function testFlushBufferSendsMessages(): void
    {
        $httpClient = $this->createMock(HttpClient::class);
        $httpClient->expects($this->once())
            ->method('post')
            ->with('/api/v1/push', $this->callback(function (array $body) {
                return count($body['items']) === 3;
            }))
            ->willReturn(['success' => true]);

        $manager = new BufferManager($httpClient);

        for ($i = 0; $i < 3; $i++) {
            $manager->addMessage('queue/Default', ['payload' => "msg{$i}"], ['messageCount' => 100, 'timeMillis' => 99999]);
        }

        $manager->flushBuffer('queue/Default');

        $stats = $manager->getStats();
        $this->assertSame(0, $stats['activeBuffers']);
        $this->assertSame(1, $stats['flushesPerformed']);
    }

    public function testFlushBufferRestoresOnFailure(): void
    {
        $httpClient = $this->createStub(HttpClient::class);
        $httpClient->method('post')
            ->willThrowException(new \RuntimeException('Server error'));

        $manager = new BufferManager($httpClient);

        $manager->addMessage('queue/Default', ['payload' => 'msg1'], ['messageCount' => 100, 'timeMillis' => 99999]);
        $manager->addMessage('queue/Default', ['payload' => 'msg2'], ['messageCount' => 100, 'timeMillis' => 99999]);

        try {
            $manager->flushBuffer('queue/Default');
            $this->fail('Should have thrown');
        } catch (\RuntimeException $e) {
            $this->assertSame('Server error', $e->getMessage());
        }

        // Messages should be restored
        $stats = $manager->getStats();
        $this->assertSame(2, $stats['totalBufferedMessages']);
        $this->assertSame(0, $stats['flushesPerformed']);
    }

    public function testCleanupClearsAllBuffers(): void
    {
        $httpClient = $this->createStub(HttpClient::class);
        $manager = new BufferManager($httpClient);

        $manager->addMessage('q1/p1', ['payload' => 'a'], ['messageCount' => 100, 'timeMillis' => 99999]);
        $manager->addMessage('q2/p1', ['payload' => 'b'], ['messageCount' => 100, 'timeMillis' => 99999]);

        $stats = $manager->getStats();
        $this->assertSame(2, $stats['activeBuffers']);

        $manager->cleanup();

        $stats = $manager->getStats();
        $this->assertSame(0, $stats['activeBuffers']);
        $this->assertSame(0, $stats['totalBufferedMessages']);
    }

    public function testFlushNonExistentBufferIsNoop(): void
    {
        $httpClient = $this->createMock(HttpClient::class);
        $httpClient->expects($this->never())->method('post');

        $manager = new BufferManager($httpClient);
        $manager->flushBuffer('nonexistent');

        // Should not throw
        $this->assertTrue(true);
    }

    public function testAutoFlushOnCountThreshold(): void
    {
        $httpClient = $this->createMock(HttpClient::class);
        // The auto-flush from MessageBuffer triggers doFlush which calls post
        $httpClient->expects($this->once())
            ->method('post')
            ->with('/api/v1/push', $this->callback(function (array $body) {
                return count($body['items']) === 3;
            }))
            ->willReturn(['success' => true]);

        $manager = new BufferManager($httpClient);

        // messageCount=3, so adding 3 messages should trigger auto-flush
        $manager->addMessage('queue/Default', ['payload' => 'msg1'], ['messageCount' => 3, 'timeMillis' => 99999]);
        $manager->addMessage('queue/Default', ['payload' => 'msg2'], ['messageCount' => 3, 'timeMillis' => 99999]);
        $manager->addMessage('queue/Default', ['payload' => 'msg3'], ['messageCount' => 3, 'timeMillis' => 99999]);
    }

    public function testMultipleBuffersTrackedSeparately(): void
    {
        $httpClient = $this->createStub(HttpClient::class);
        $manager = new BufferManager($httpClient);

        $manager->addMessage('q1/p1', ['payload' => 'a'], ['messageCount' => 100, 'timeMillis' => 99999]);
        $manager->addMessage('q1/p1', ['payload' => 'b'], ['messageCount' => 100, 'timeMillis' => 99999]);
        $manager->addMessage('q2/p1', ['payload' => 'c'], ['messageCount' => 100, 'timeMillis' => 99999]);

        $stats = $manager->getStats();
        $this->assertSame(2, $stats['activeBuffers']);
        $this->assertSame(3, $stats['totalBufferedMessages']);
    }
}
