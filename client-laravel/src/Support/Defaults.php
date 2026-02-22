<?php

namespace Queen\Support;

class Defaults
{
    public const CLIENT_DEFAULTS = [
        'timeoutMillis' => 30000,
        'retryAttempts' => 3,
        'retryDelayMillis' => 1000,
        'loadBalancingStrategy' => 'affinity',
        'affinityHashRing' => 128,
        'enableFailover' => true,
        'healthRetryAfterMillis' => 5000,
        'bearerToken' => null,
        'headers' => [],
    ];

    public const QUEUE_DEFAULTS = [
        'leaseTime' => 300,
        'retryLimit' => 3,
        'priority' => 0,
        'delayedProcessing' => 0,
        'windowBuffer' => 0,
        'maxSize' => 0,
        'retentionSeconds' => 0,
        'completedRetentionSeconds' => 0,
        'encryptionEnabled' => false,
    ];

    public const CONSUME_DEFAULTS = [
        'concurrency' => 1,
        'batch' => 1,
        'autoAck' => true,
        'wait' => true,
        'timeoutMillis' => 30000,
        'limit' => null,
        'idleMillis' => null,
        'renewLease' => false,
        'renewLeaseIntervalMillis' => null,
        'subscriptionMode' => null,
        'subscriptionFrom' => null,
    ];

    public const POP_DEFAULTS = [
        'batch' => 1,
        'wait' => false,
        'timeoutMillis' => 30000,
        'autoAck' => false,
    ];

    public const BUFFER_DEFAULTS = [
        'messageCount' => 100,
        'timeMillis' => 1000,
    ];
}
