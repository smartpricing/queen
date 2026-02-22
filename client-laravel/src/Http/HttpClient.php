<?php

namespace Queen\Http;

use GuzzleHttp\Client;
use GuzzleHttp\Promise\PromiseInterface;
use GuzzleHttp\Promise\Utils as PromiseUtils;
use Psr\Http\Message\ResponseInterface;
use Queen\Exceptions\HttpException;

class HttpClient
{
    private ?string $baseUrl;
    private ?LoadBalancer $loadBalancer;
    private int $timeoutMillis;
    private int $retryAttempts;
    private int $retryDelayMillis;
    private bool $enableFailover;
    private ?string $bearerToken;
    private array $headers;
    private Client $guzzle;

    public function __construct(array $options = [])
    {
        $this->baseUrl = $options['baseUrl'] ?? null;
        $this->loadBalancer = $options['loadBalancer'] ?? null;
        $this->timeoutMillis = $options['timeoutMillis'] ?? 30000;
        $this->retryAttempts = $options['retryAttempts'] ?? 3;
        $this->retryDelayMillis = $options['retryDelayMillis'] ?? 1000;
        $this->enableFailover = $options['enableFailover'] ?? true;
        $this->bearerToken = $options['bearerToken'] ?? null;
        $this->headers = $options['headers'] ?? [];
        $this->guzzle = new Client();
    }

    // ===========================
    // Synchronous API
    // ===========================

    public function get(string $path, ?int $requestTimeoutMillis = null, ?string $affinityKey = null): mixed
    {
        return $this->requestWithFailover('GET', $path, null, $requestTimeoutMillis, $affinityKey);
    }

    public function post(string $path, ?array $body = null, ?int $requestTimeoutMillis = null, ?string $affinityKey = null): mixed
    {
        return $this->requestWithFailover('POST', $path, $body, $requestTimeoutMillis, $affinityKey);
    }

    public function put(string $path, ?array $body = null, ?int $requestTimeoutMillis = null, ?string $affinityKey = null): mixed
    {
        return $this->requestWithFailover('PUT', $path, $body, $requestTimeoutMillis, $affinityKey);
    }

    public function delete(string $path, ?int $requestTimeoutMillis = null, ?string $affinityKey = null): mixed
    {
        return $this->requestWithFailover('DELETE', $path, null, $requestTimeoutMillis, $affinityKey);
    }

    // ===========================
    // Async API (returns Guzzle promises)
    // ===========================

    public function getAsync(string $path, ?int $requestTimeoutMillis = null, ?string $affinityKey = null): PromiseInterface
    {
        return $this->executeRequestAsync($this->resolveUrl($affinityKey) . $path, 'GET', null, $requestTimeoutMillis);
    }

    public function postAsync(string $path, ?array $body = null, ?int $requestTimeoutMillis = null, ?string $affinityKey = null): PromiseInterface
    {
        return $this->executeRequestAsync($this->resolveUrl($affinityKey) . $path, 'POST', $body, $requestTimeoutMillis);
    }

    public function putAsync(string $path, ?array $body = null, ?int $requestTimeoutMillis = null, ?string $affinityKey = null): PromiseInterface
    {
        return $this->executeRequestAsync($this->resolveUrl($affinityKey) . $path, 'PUT', $body, $requestTimeoutMillis);
    }

    public function deleteAsync(string $path, ?int $requestTimeoutMillis = null, ?string $affinityKey = null): PromiseInterface
    {
        return $this->executeRequestAsync($this->resolveUrl($affinityKey) . $path, 'DELETE', null, $requestTimeoutMillis);
    }

    /**
     * Wait for multiple promises to resolve concurrently.
     *
     * @param PromiseInterface[] $promises
     * @return array Results indexed same as input
     */
    public static function awaitAll(array $promises): array
    {
        return PromiseUtils::unwrap($promises);
    }

    /**
     * Settle all promises (no exceptions on failure). Returns array of
     * ['state' => 'fulfilled'|'rejected', 'value' => ..., 'reason' => ...]
     *
     * @param PromiseInterface[] $promises
     * @return array
     */
    public static function settleAll(array $promises): array
    {
        return PromiseUtils::settle($promises)->wait();
    }

    // ===========================
    // Internals
    // ===========================

    public function getLoadBalancer(): ?LoadBalancer
    {
        return $this->loadBalancer;
    }

    private function resolveUrl(?string $affinityKey = null): string
    {
        if ($this->loadBalancer !== null) {
            return $this->loadBalancer->getNextUrl($affinityKey);
        }
        if ($this->baseUrl === null) {
            throw new \LogicException('HttpClient has no baseUrl and no LoadBalancer configured');
        }
        return $this->baseUrl;
    }

    private function buildRequestOptions(string $method, ?array $body, ?int $requestTimeoutMillis): array
    {
        $effectiveTimeout = $requestTimeoutMillis ?? $this->timeoutMillis;

        $headers = ['Content-Type' => 'application/json'];
        if ($this->bearerToken !== null) {
            $headers['Authorization'] = "Bearer {$this->bearerToken}";
        }
        $headers = array_merge($headers, $this->headers);

        $options = [
            'headers' => $headers,
            'timeout' => $effectiveTimeout / 1000,
            'connect_timeout' => 5,
            'http_errors' => false,
        ];

        if ($body !== null) {
            $options['json'] = $body;
        }

        return $options;
    }

    private function parseResponse(ResponseInterface $response): mixed
    {
        $statusCode = $response->getStatusCode();

        if ($statusCode === 204) {
            return null;
        }

        $responseBody = (string) $response->getBody();

        if ($statusCode >= 400) {
            $error = "HTTP {$statusCode}";
            if ($responseBody) {
                $decoded = json_decode($responseBody, true);
                if (isset($decoded['error'])) {
                    $error = $decoded['error'];
                }
            }
            throw new HttpException($error, $statusCode);
        }

        if (empty($responseBody)) {
            return null;
        }

        return json_decode($responseBody, true);
    }

    private function executeRequest(string $url, string $method, ?array $body = null, ?int $requestTimeoutMillis = null): mixed
    {
        $options = $this->buildRequestOptions($method, $body, $requestTimeoutMillis);
        $response = $this->guzzle->request($method, $url, $options);
        return $this->parseResponse($response);
    }

    private function executeRequestAsync(string $url, string $method, ?array $body = null, ?int $requestTimeoutMillis = null): PromiseInterface
    {
        $options = $this->buildRequestOptions($method, $body, $requestTimeoutMillis);

        return $this->guzzle->requestAsync($method, $url, $options)->then(
            fn(ResponseInterface $response) => $this->parseResponse($response)
        );
    }

    private function getStatusCode(\Throwable $error): int
    {
        return ($error instanceof HttpException) ? $error->statusCode : 0;
    }

    private function requestWithRetry(string $method, string $path, ?array $body = null, ?int $requestTimeoutMillis = null, ?string $affinityKey = null): mixed
    {
        $lastError = null;

        for ($attempt = 0; $attempt < $this->retryAttempts; $attempt++) {
            try {
                $url = $this->resolveUrl($affinityKey) . $path;
                return $this->executeRequest($url, $method, $body, $requestTimeoutMillis);
            } catch (\Throwable $error) {
                $lastError = $error;

                $statusCode = $this->getStatusCode($error);
                if ($statusCode >= 400 && $statusCode < 500) {
                    throw $error;
                }

                if ($attempt < $this->retryAttempts - 1) {
                    $delay = $this->retryDelayMillis * (2 ** $attempt);
                    usleep($delay * 1000);
                }
            }
        }

        throw $lastError;
    }

    private function requestWithFailover(string $method, string $path, ?array $body = null, ?int $requestTimeoutMillis = null, ?string $affinityKey = null): mixed
    {
        if ($this->loadBalancer === null || !$this->enableFailover) {
            return $this->requestWithRetry($method, $path, $body, $requestTimeoutMillis, $affinityKey);
        }

        $urls = $this->loadBalancer->getAllUrls();
        $attemptedUrls = [];
        $lastError = null;

        for ($i = 0; $i < count($urls); $i++) {
            $url = $this->loadBalancer->getNextUrl($affinityKey);

            if (in_array($url, $attemptedUrls, true)) {
                continue;
            }

            $attemptedUrls[] = $url;

            try {
                $result = $this->executeRequest($url . $path, $method, $body, $requestTimeoutMillis);
                $this->loadBalancer->markHealthy($url);
                return $result;
            } catch (\Throwable $error) {
                $lastError = $error;

                $statusCode = $this->getStatusCode($error);
                if ($statusCode === 0 || $statusCode >= 500) {
                    $this->loadBalancer->markUnhealthy($url);
                }

                if ($statusCode >= 400 && $statusCode < 500) {
                    throw $error;
                }
            }
        }

        throw $lastError ?? new \RuntimeException('All servers failed');
    }
}
