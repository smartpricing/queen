<?php

namespace Queen\Tests;

use PHPUnit\Framework\TestCase;
use Queen\Queen;
use Queen\Support\Defaults;

class QueenConfigTest extends TestCase
{
    public function testConstructWithSingleUrl(): void
    {
        $queen = new Queen('http://localhost:6632');

        // Should be able to create queue builders
        $builder = $queen->queue('test');
        $this->assertInstanceOf(\Queen\Builders\QueueBuilder::class, $builder);
    }

    public function testConstructWithUrlArray(): void
    {
        $queen = new Queen(['http://a:6632', 'http://b:6632']);
        $builder = $queen->queue('test');
        $this->assertInstanceOf(\Queen\Builders\QueueBuilder::class, $builder);
    }

    public function testConstructWithConfigArray(): void
    {
        $queen = new Queen([
            'urls' => ['http://a:6632', 'http://b:6632'],
            'bearerToken' => 'test-token',
            'timeoutMillis' => 5000,
            'loadBalancingStrategy' => 'round-robin',
        ]);

        $builder = $queen->queue('test');
        $this->assertInstanceOf(\Queen\Builders\QueueBuilder::class, $builder);
    }

    public function testConstructWithSingleUrlConfig(): void
    {
        $queen = new Queen([
            'url' => 'http://localhost:6632',
        ]);

        $builder = $queen->queue('test');
        $this->assertInstanceOf(\Queen\Builders\QueueBuilder::class, $builder);
    }

    public function testConstructWithNoUrlThrows(): void
    {
        $this->expectException(\InvalidArgumentException::class);
        $this->expectExceptionMessage('Must provide urls or url');

        new Queen(['bearerToken' => 'test']);
    }

    public function testAdminIsSingleton(): void
    {
        $queen = new Queen('http://localhost:6632');

        $admin1 = $queen->admin();
        $admin2 = $queen->admin();

        $this->assertSame($admin1, $admin2);
    }

    public function testTransactionBuilderCreated(): void
    {
        $queen = new Queen('http://localhost:6632');
        $tx = $queen->transaction();
        $this->assertInstanceOf(\Queen\Builders\TransactionBuilder::class, $tx);
    }

    public function testBufferStatsInitiallyEmpty(): void
    {
        $queen = new Queen('http://localhost:6632');
        $stats = $queen->getBufferStats();

        $this->assertSame(0, $stats['activeBuffers']);
        $this->assertSame(0, $stats['totalBufferedMessages']);
        $this->assertSame(0, $stats['flushesPerformed']);
    }

    public function testCloseDoesNotThrow(): void
    {
        $queen = new Queen('http://localhost:6632');
        $queen->close();

        // Should be safe to call close even with no active buffers
        $this->assertTrue(true);
    }
}
