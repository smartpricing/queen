<?php

namespace Queen\Tests;

use PHPUnit\Framework\TestCase;
use Queen\Http\HttpClient;
use Queen\Http\LoadBalancer;
use Queen\Exceptions\HttpException;

class HttpClientTest extends TestCase
{
    public function testNoBaseUrlAndNoLoadBalancerThrows(): void
    {
        $client = new HttpClient([]);

        $this->expectException(\LogicException::class);
        $client->get('/test');
    }

    public function testHttpExceptionHasStatusCode(): void
    {
        $ex = new HttpException('Not Found', 404);

        $this->assertSame(404, $ex->statusCode);
        $this->assertSame('Not Found', $ex->getMessage());
        $this->assertInstanceOf(\RuntimeException::class, $ex);
    }

    public function testHttpExceptionWithPrevious(): void
    {
        $previous = new \RuntimeException('original');
        $ex = new HttpException('Wrapped', 500, 0, $previous);

        $this->assertSame(500, $ex->statusCode);
        $this->assertSame($previous, $ex->getPrevious());
    }

    public function testGetLoadBalancerReturnsNull(): void
    {
        $client = new HttpClient(['baseUrl' => 'http://localhost']);
        $this->assertNull($client->getLoadBalancer());
    }

    public function testGetLoadBalancerReturnsInstance(): void
    {
        $lb = new LoadBalancer(['http://a', 'http://b']);
        $client = new HttpClient(['loadBalancer' => $lb]);
        $this->assertSame($lb, $client->getLoadBalancer());
    }
}
