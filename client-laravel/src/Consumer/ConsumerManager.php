<?php

namespace Queen\Consumer;

use Queen\Http\HttpClient;
use Queen\Queen;
use GuzzleHttp\Promise\Utils as PromiseUtils;

class ConsumerManager
{
    private HttpClient $httpClient;
    private Queen $queen;

    public function __construct(HttpClient $httpClient, Queen $queen)
    {
        $this->httpClient = $httpClient;
        $this->queen = $queen;
    }

    public function start(\Closure $handler, array $options): void
    {
        $queue = $options['queue'] ?? null;
        $partition = $options['partition'] ?? null;
        $namespace = $options['namespace'] ?? null;
        $task = $options['task'] ?? null;
        $group = $options['group'] ?? null;
        $concurrency = $options['concurrency'] ?? 1;
        $batch = $options['batch'] ?? 1;
        $limit = $options['limit'] ?? null;
        $idleMillis = $options['idleMillis'] ?? null;
        $autoAck = $options['autoAck'] ?? true;
        $wait = $options['wait'] ?? true;
        $timeoutMillis = $options['timeoutMillis'] ?? 30000;
        $renewLease = $options['renewLease'] ?? false;
        $renewLeaseIntervalMillis = $options['renewLeaseIntervalMillis'] ?? null;
        $each = $options['each'] ?? false;
        $subscriptionMode = $options['subscriptionMode'] ?? null;
        $subscriptionFrom = $options['subscriptionFrom'] ?? null;

        $path = $this->buildPath($queue, $partition, $namespace, $task);
        $baseParams = $this->buildParams($batch, $wait, $timeoutMillis, $group, $subscriptionMode, $subscriptionFrom, $namespace, $task);
        $affinityKey = $this->getAffinityKey($queue, $partition, $namespace, $task, $group);

        // Install signal handlers, saving previous handlers for restoration
        $running = true;
        $prevSigint = null;
        $prevSigterm = null;
        if (function_exists('pcntl_signal')) {
            $prevSigint = pcntl_signal_get_handler(SIGINT);
            $prevSigterm = pcntl_signal_get_handler(SIGTERM);
            pcntl_signal(SIGINT, function () use (&$running) {
                $running = false;
            });
            pcntl_signal(SIGTERM, function () use (&$running) {
                $running = false;
            });
        }

        try {
            if ($concurrency <= 1) {
                $this->worker(
                    $handler, $path, $baseParams, $batch, $limit, $idleMillis,
                    $autoAck, $wait, $timeoutMillis, $renewLease, $renewLeaseIntervalMillis,
                    $each, $group, $affinityKey, $running
                );
            } else {
                $this->concurrentWorkers(
                    $concurrency, $handler, $path, $baseParams, $batch, $limit, $idleMillis,
                    $autoAck, $wait, $timeoutMillis, $renewLease, $renewLeaseIntervalMillis,
                    $each, $group, $affinityKey, $running
                );
            }
        } finally {
            // Restore previous signal handlers
            if (function_exists('pcntl_signal')) {
                if ($prevSigint !== null) {
                    pcntl_signal(SIGINT, $prevSigint);
                }
                if ($prevSigterm !== null) {
                    pcntl_signal(SIGTERM, $prevSigterm);
                }
            }
        }
    }

    /**
     * Run N concurrent workers using Guzzle async. All workers long-poll
     * concurrently via cURL multi-handle; processing remains sequential
     * per-worker but polls overlap.
     */
    private function concurrentWorkers(
        int $concurrency,
        \Closure $handler,
        string $path,
        string $baseParams,
        int $batch,
        ?int $limit,
        ?int $idleMillis,
        bool $autoAck,
        bool $wait,
        int $timeoutMillis,
        bool $renewLease,
        ?int $renewLeaseIntervalMillis,
        bool $each,
        ?string $group,
        ?string $affinityKey,
        bool &$running,
    ): void {
        // Per-worker state
        $workerProcessed = array_fill(0, $concurrency, 0);
        $workerLastMsg = array_fill(0, $concurrency, $idleMillis !== null ? $this->nowMillis() : null);
        $perWorkerLimit = $limit !== null ? (int) ceil($limit / $concurrency) : null;
        $clientTimeout = $wait ? $timeoutMillis + 5000 : $timeoutMillis;
        $url = "{$path}?{$baseParams}";

        while ($running) {
            if (function_exists('pcntl_signal_dispatch')) {
                pcntl_signal_dispatch();
            }
            if (!$running) {
                break;
            }

            // Determine which workers should poll
            $activeWorkers = [];
            for ($w = 0; $w < $concurrency; $w++) {
                if ($perWorkerLimit !== null && $workerProcessed[$w] >= $perWorkerLimit) {
                    continue;
                }
                if ($idleMillis !== null && $workerLastMsg[$w] !== null) {
                    if ($this->nowMillis() - $workerLastMsg[$w] >= $idleMillis) {
                        continue;
                    }
                }
                $activeWorkers[] = $w;
            }

            if (empty($activeWorkers)) {
                break; // All workers done
            }

            // Fire N concurrent long-poll requests
            $promises = [];
            foreach ($activeWorkers as $w) {
                $promises[$w] = $this->httpClient->getAsync($url, $clientTimeout, $affinityKey);
            }

            // Settle all — don't throw on individual failures
            $results = HttpClient::settleAll($promises);

            if (function_exists('pcntl_signal_dispatch')) {
                pcntl_signal_dispatch();
            }
            if (!$running) {
                break;
            }

            // Process results per worker
            foreach ($results as $w => $outcome) {
                if (!$running) {
                    break;
                }

                if ($outcome['state'] === 'rejected') {
                    $error = $outcome['reason'];
                    $isTimeout = str_contains($error->getMessage(), 'timeout') || str_contains($error->getMessage(), 'timed out');
                    if ($isTimeout && $wait) {
                        continue; // Normal for long polling
                    }
                    $isNetwork = str_contains($error->getMessage(), 'Connection refused') || str_contains($error->getMessage(), 'cURL error');
                    if ($isNetwork) {
                        usleep(1_000_000);
                        continue;
                    }
                    throw $error;
                }

                $result = $outcome['value'];
                if (!$result || !isset($result['messages']) || empty($result['messages'])) {
                    if (!$wait) {
                        usleep(100_000);
                    }
                    continue;
                }

                $messages = array_values(array_filter($result['messages'], fn($msg) => $msg !== null));
                if (empty($messages)) {
                    continue;
                }

                if ($idleMillis !== null) {
                    $workerLastMsg[$w] = $this->nowMillis();
                }

                $this->enhanceMessagesWithTrace($messages, $group);

                $leaseRenewalTime = null;
                if ($renewLease && $renewLeaseIntervalMillis !== null) {
                    $leaseRenewalTime = $this->nowMillis() + $renewLeaseIntervalMillis;
                }

                if ($each) {
                    foreach ($messages as $message) {
                        if (!$running) {
                            break;
                        }
                        $this->renewLeaseIfNeeded($messages, $leaseRenewalTime, $renewLeaseIntervalMillis);
                        $this->processMessage($message, $handler, $autoAck, $group);
                        $workerProcessed[$w]++;
                        if ($perWorkerLimit !== null && $workerProcessed[$w] >= $perWorkerLimit) {
                            break;
                        }
                    }
                } else {
                    $this->renewLeaseIfNeeded($messages, $leaseRenewalTime, $renewLeaseIntervalMillis);
                    $this->processBatch($messages, $handler, $autoAck, $group);
                    $workerProcessed[$w] += count($messages);
                }
            }

            // Check global limit
            if ($limit !== null && array_sum($workerProcessed) >= $limit) {
                break;
            }
        }
    }

    private function worker(
        \Closure $handler,
        string $path,
        string $baseParams,
        int $batch,
        ?int $limit,
        ?int $idleMillis,
        bool $autoAck,
        bool $wait,
        int $timeoutMillis,
        bool $renewLease,
        ?int $renewLeaseIntervalMillis,
        bool $each,
        ?string $group,
        ?string $affinityKey,
        bool &$running,
    ): void {
        $processedCount = 0;
        $lastMessageTime = $idleMillis !== null ? $this->nowMillis() : null;

        while ($running) {
            if (function_exists('pcntl_signal_dispatch')) {
                pcntl_signal_dispatch();
            }

            if (!$running) {
                break;
            }

            if ($limit !== null && $processedCount >= $limit) {
                break;
            }

            if ($idleMillis !== null && $lastMessageTime !== null) {
                $idleTime = $this->nowMillis() - $lastMessageTime;
                if ($idleTime >= $idleMillis) {
                    break;
                }
            }

            try {
                $clientTimeout = $wait ? $timeoutMillis + 5000 : $timeoutMillis;
                $result = $this->httpClient->get("{$path}?{$baseParams}", $clientTimeout, $affinityKey);

                if (!$result || !isset($result['messages']) || empty($result['messages'])) {
                    if (!$wait) {
                        usleep(100_000);
                    }
                    continue;
                }

                $messages = array_values(array_filter($result['messages'], fn($msg) => $msg !== null));

                if (empty($messages)) {
                    continue;
                }

                if ($idleMillis !== null) {
                    $lastMessageTime = $this->nowMillis();
                }

                $this->enhanceMessagesWithTrace($messages, $group);

                $leaseRenewalTime = null;
                if ($renewLease && $renewLeaseIntervalMillis !== null) {
                    $leaseRenewalTime = $this->nowMillis() + $renewLeaseIntervalMillis;
                }

                if ($each) {
                    foreach ($messages as $message) {
                        if (!$running) {
                            break;
                        }

                        $this->renewLeaseIfNeeded($messages, $leaseRenewalTime, $renewLeaseIntervalMillis);
                        $this->processMessage($message, $handler, $autoAck, $group);
                        $processedCount++;

                        if ($limit !== null && $processedCount >= $limit) {
                            break;
                        }
                    }
                } else {
                    $this->renewLeaseIfNeeded($messages, $leaseRenewalTime, $renewLeaseIntervalMillis);
                    $this->processBatch($messages, $handler, $autoAck, $group);
                    $processedCount += count($messages);
                }
            } catch (\Throwable $error) {
                $isTimeout = str_contains($error->getMessage(), 'timeout') || str_contains($error->getMessage(), 'timed out');
                if ($isTimeout && $wait) {
                    continue;
                }

                $isNetwork = str_contains($error->getMessage(), 'Connection refused') || str_contains($error->getMessage(), 'cURL error');
                if ($isNetwork) {
                    usleep(1_000_000);
                    continue;
                }

                throw $error;
            }
        }
    }

    private function processMessage(array $message, \Closure $handler, bool $autoAck, ?string $group): void
    {
        try {
            $handler($message);

            if ($autoAck) {
                $context = $group !== null ? ['group' => $group] : [];
                $this->queen->ack($message, true, $context);
            }
        } catch (\Throwable $error) {
            if ($autoAck) {
                $context = $group !== null ? ['group' => $group] : [];
                $this->queen->ack($message, false, $context);
                return;
            }
            throw $error;
        }
    }

    private function processBatch(array $messages, \Closure $handler, bool $autoAck, ?string $group): void
    {
        try {
            $handler($messages);

            if ($autoAck) {
                $context = $group !== null ? ['group' => $group] : [];
                $this->queen->ack($messages, true, $context);
            }
        } catch (\Throwable $error) {
            if ($autoAck) {
                $context = $group !== null ? ['group' => $group] : [];
                $this->queen->ack($messages, false, $context);
                return;
            }
            throw $error;
        }
    }

    private function renewLeaseIfNeeded(array $messages, ?int &$leaseRenewalTime, ?int $intervalMillis): void
    {
        if ($leaseRenewalTime === null || $intervalMillis === null) {
            return;
        }

        if ($this->nowMillis() >= $leaseRenewalTime) {
            // Fire async renewal — don't block processing
            try {
                $leaseIds = array_filter(array_column($messages, 'leaseId'), fn($id) => $id !== null);
                $promises = [];
                foreach ($leaseIds as $leaseId) {
                    $promises[] = $this->httpClient->postAsync("/api/v1/lease/{$leaseId}/extend", []);
                }
                if (!empty($promises)) {
                    // Settle without throwing — renewal failure is non-fatal
                    HttpClient::settleAll($promises);
                }
            } catch (\Throwable $e) {
                // Lease renewal failure is non-fatal
            }
            $leaseRenewalTime = $this->nowMillis() + $intervalMillis;
        }
    }

    private function enhanceMessagesWithTrace(array &$messages, ?string $group): void
    {
        $httpClient = $this->httpClient;
        $consumerGroup = $group ?? '__QUEUE_MODE__';

        foreach ($messages as &$message) {
            $message['trace'] = function (array $traceConfig) use ($httpClient, $consumerGroup, $message): array {
                try {
                    if (!isset($traceConfig['data'])) {
                        return ['success' => false, 'error' => 'Invalid trace config: requires data key'];
                    }

                    $traceNames = null;
                    if (isset($traceConfig['traceName'])) {
                        $traceNames = is_array($traceConfig['traceName'])
                            ? array_filter($traceConfig['traceName'], fn($n) => is_string($n) && strlen($n) > 0)
                            : [$traceConfig['traceName']];
                        if (empty($traceNames)) {
                            $traceNames = null;
                        }
                    }

                    $response = $httpClient->post('/api/v1/traces', [
                        'transactionId' => $message['transactionId'],
                        'partitionId' => $message['partitionId'],
                        'consumerGroup' => $consumerGroup,
                        'traceNames' => $traceNames,
                        'eventType' => $traceConfig['eventType'] ?? 'info',
                        'data' => $traceConfig['data'],
                    ]);

                    return array_merge(['success' => true], $response ?? []);
                } catch (\Throwable $error) {
                    return ['success' => false, 'error' => $error->getMessage()];
                }
            };
        }
        unset($message);
    }

    private function getAffinityKey(?string $queue, ?string $partition, ?string $namespace, ?string $task, ?string $group): ?string
    {
        if ($queue !== null) {
            return "{$queue}:" . ($partition ?? '*') . ':' . ($group ?? '__QUEUE_MODE__');
        }
        if ($namespace !== null || $task !== null) {
            return ($namespace ?? '*') . ':' . ($task ?? '*') . ':' . ($group ?? '__QUEUE_MODE__');
        }
        return null;
    }

    private function buildPath(?string $queue, ?string $partition, ?string $namespace, ?string $task): string
    {
        if ($queue !== null) {
            if ($partition !== null) {
                return "/api/v1/pop/queue/{$queue}/partition/{$partition}";
            }
            return "/api/v1/pop/queue/{$queue}";
        }

        if ($namespace !== null || $task !== null) {
            return '/api/v1/pop';
        }

        throw new \RuntimeException('Must specify queue, namespace, or task');
    }

    private function buildParams(
        int $batch,
        bool $wait,
        int $timeoutMillis,
        ?string $group,
        ?string $subscriptionMode,
        ?string $subscriptionFrom,
        ?string $namespace,
        ?string $task,
    ): string {
        $params = [
            'batch' => (string) $batch,
            'wait' => $wait ? 'true' : 'false',
            'timeout' => (string) $timeoutMillis,
        ];

        if ($group !== null) {
            $params['consumerGroup'] = $group;
        }
        if ($subscriptionMode !== null) {
            $params['subscriptionMode'] = $subscriptionMode;
        }
        if ($subscriptionFrom !== null) {
            $params['subscriptionFrom'] = $subscriptionFrom;
        }
        if ($namespace !== null) {
            $params['namespace'] = $namespace;
        }
        if ($task !== null) {
            $params['task'] = $task;
        }

        return http_build_query($params);
    }

    private function nowMillis(): int
    {
        return (int)(microtime(true) * 1000);
    }
}
