<?php

return [
    'url' => env('QUEEN_URL', 'http://localhost:6632'),
    'urls' => env('QUEEN_URLS') ? explode(',', env('QUEEN_URLS')) : null,
    'bearer_token' => env('QUEEN_BEARER_TOKEN'),
    'timeout' => env('QUEEN_TIMEOUT', 30000),
    'retry_attempts' => env('QUEEN_RETRY_ATTEMPTS', 3),
    'retry_delay' => env('QUEEN_RETRY_DELAY', 1000),
    'load_balancing_strategy' => env('QUEEN_LB_STRATEGY', 'affinity'),
    'enable_failover' => env('QUEEN_ENABLE_FAILOVER', true),
    'affinity_hash_ring' => env('QUEEN_AFFINITY_HASH_RING', 150),
    'health_retry_after' => env('QUEEN_HEALTH_RETRY_AFTER', 30000),
    'headers' => [],
];
