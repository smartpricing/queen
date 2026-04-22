# PHP / Laravel Client

PHP client library for Queen MQ. Works standalone on any PHP 8.1+ project, with optional Laravel auto-discovery (service provider, facade, artisan command).

## Installation

```bash
composer require smartpricing/queen-mq
```

**Requirements:**
- PHP 8.1 or higher
- Laravel 10+ (optional — only if you want the Laravel facade / artisan command)

### Laravel setup (optional)

Publish the config file:

```bash
php artisan vendor:publish --tag=queen-config
```

Set environment variables:

```env
QUEEN_URL=http://localhost:6632
QUEEN_BEARER_TOKEN=your-token
```

## Quick Start

### Standalone (any PHP project)

```php
<?php
use Queen\Queen;

$queen = new Queen('http://localhost:6632');

// Create queue
$queen->queue('orders')->create()->execute();

// Push
$queen->queue('orders')->push([
    ['data' => ['orderId' => 1, 'amount' => 100]],
])->execute();

// Pop
$messages = $queen->queue('orders')->batch(10)->wait(true)->pop();

foreach ($messages as $msg) {
    // Process...
    $queen->ack($msg);
}
```

### Laravel facade

```php
use Queen\Laravel\QueenFacade as Queen;

Queen::queue('orders')->push([['data' => ['test' => true]]])->execute();
```

## Queue Operations

### Create / Delete

```php
// With config
$queen->queue('orders')
    ->config(['leaseTime' => 300, 'retryLimit' => 3])
    ->create()
    ->execute();

$queen->queue('orders')->delete()->execute();
```

### Push

```php
// Basic
$queen->queue('orders')
    ->partition('user-123')
    ->push([
        ['data' => ['orderId' => 1, 'amount' => 100]],
        ['data' => ['orderId' => 2, 'amount' => 200]],
    ])
    ->execute();

// With callbacks
$queen->queue('orders')
    ->push([['data' => ['orderId' => 1]]])
    ->onSuccess(fn($items) => logger()->info('Pushed', ['count' => count($items)]))
    ->onError(fn($items, $e) => logger()->error($e->getMessage()))
    ->onDuplicate(fn($items, $e) => logger()->warning('Duplicate'))
    ->execute();

// Buffered push (accumulate and flush by count or time)
$queen->queue('orders')
    ->buffer(['messageCount' => 100, 'timeMillis' => 1000])
    ->push([['data' => ['orderId' => 1]]])
    ->execute();

$queen->flushAllBuffers();  // manual flush
```

### Pop

```php
// Single
$messages = $queen->queue('orders')->pop();

// Batch with long polling
$messages = $queen->queue('orders')->batch(10)->wait(true)->pop();

// From a specific partition
$messages = $queen->queue('orders')->partition('user-123')->pop();
```

## Consuming

### High-level consumer (recommended)

Inspired by [php-rdkafka](https://github.com/arnaud-lb/php-rdkafka)'s `KafkaConsumer` — graceful SIGINT/SIGTERM handling, lease renewal, idle timeout support.

```php
$consumer = $queen->queue('orders')
    ->group('processors')
    ->batch(1)
    ->autoAck(false)
    ->getConsumer();

$consumer->subscribe();

while (!$consumer->isClosed()) {
    $message = $consumer->consume(1000);   // timeout in ms
    if ($message === null) continue;

    processOrder($message['payload']);
    $consumer->ack($message);
}

$consumer->close();
```

#### Consumer methods

| Method | Description |
|--------|-------------|
| `subscribe()` | Start the consumer (must call before `consume`) |
| `consume(int $timeoutMs)` | Pull one message (or `null` on timeout) |
| `consumeBatch(int $timeoutMs, int $max)` | Pull up to `$max` messages |
| `ack($message)` | Acknowledge (single or batch) |
| `nack($message)` | Negative-ack (marks failed, triggers retry/DLQ) |
| `renewLease($msg)` | Extend the message lease for long-running work |
| `isClosed()` | Check if stopped (e.g. by SIGINT) |
| `close()` | Stop the consumer |

### Batch consuming

```php
$consumer = $queen->queue('orders')->group('processors')->getConsumer();
$consumer->subscribe();

while (!$consumer->isClosed()) {
    $messages = $consumer->consumeBatch(1000, 10);
    if (empty($messages)) continue;

    foreach ($messages as $msg) {
        processOrder($msg['payload']);
    }
    $consumer->ack($messages);
}
```

### Callback-based consumer

```php
$queen->queue('orders')
    ->group('processors')
    ->batch(10)
    ->autoAck(true)
    ->consume(function (array $messages) {
        foreach ($messages as $msg) {
            processOrder($msg['payload']);
        }
    })
    ->execute();
```

## Transactions

Atomic ack + push across queues:

```php
$queen->transaction()
    ->ack($messages)
    ->queue('notifications')->partition('user-123')->push([
        ['data' => ['notify' => true]],
    ])
    ->queue('audit-log')->push([
        ['data' => ['action' => 'order-processed']],
    ])
    ->commit();
```

## Direct ACK / Nack / Lease Renewal

```php
// Ack
$queen->ack($message);
$queen->ack($messages);                       // batch
$queen->ack($message, false);                 // nack
$queen->ack($message, true, ['group' => 'processors']);

// Renew lease (for long-running handlers)
$queen->renew($message);
$queen->renew('lease-id-string');
$queen->renew($messages);
```

## Dead Letter Queue

```php
$result = $queen->queue('orders')
    ->dlq('processors')
    ->limit(50)
    ->from('2024-01-01T00:00:00Z')
    ->to('2024-12-31T23:59:59Z')
    ->get();

// $result = ['messages' => [...], 'total' => 123]
```

## Admin API

```php
$admin = $queen->admin();

$admin->health();
$admin->listQueues();
$admin->getQueue('orders');
$admin->listConsumerGroups();
$admin->getConsumerGroup('processors');
$admin->seekConsumerGroup('processors', 'orders', ['timestamp' => '2024-01-01T00:00:00Z']);
$admin->getSystemMetrics();
$admin->setMaintenanceMode(true);
```

## Namespace / Task Wildcards

Consume from many queues sharing a namespace or task label:

```php
$consumer = $queen->queue()
    ->namespace('billing')
    ->task('invoices')
    ->group('invoice-processors')
    ->getConsumer();
```

## Laravel Artisan Command

```bash
# Basic
php artisan queen:consume orders App\\Handlers\\OrderHandler --group=processors --auto-ack

# With full options
php artisan queen:consume orders App\\Handlers\\OrderHandler \
    --group=processors \
    --batch=10 \
    --auto-ack \
    --timeout=30000 \
    --limit=1000

# Subscription mode (only new messages, or replay from timestamp)
php artisan queen:consume events App\\Handlers\\EventHandler \
    --group=watchers \
    --subscription-mode=new-only
```

Handler class:

```php
namespace App\Handlers;

class OrderHandler
{
    public function handle(array $messageOrMessages): void
    {
        // Laravel will DI whatever you typehint in the constructor
        // $messageOrMessages is a single message (batch=1) or array (batch>1)
    }
}
```

## Configuration

### Standalone

```php
$queen = new Queen([
    'urls'                  => ['http://server1:6632', 'http://server2:6632'],
    'loadBalancingStrategy' => 'affinity',  // or 'round-robin', 'session'
    'bearerToken'           => 'my-token',
    'timeoutMillis'         => 30000,
    'retryAttempts'         => 3,
    'retryDelayMillis'      => 1000,
    'affinityHashRing'      => 128,
    'enableFailover'        => true,
    'healthRetryAfterMillis'=> 5000,
    'headers'               => ['X-Custom' => 'value'],
]);
```

### Laravel (`config/queen.php`)

```php
return [
    'url'                     => env('QUEEN_URL', 'http://localhost:6632'),
    'urls'                    => env('QUEEN_URLS') ? explode(',', env('QUEEN_URLS')) : null,
    'bearer_token'            => env('QUEEN_BEARER_TOKEN'),
    'timeout'                 => env('QUEEN_TIMEOUT', 30000),
    'retry_attempts'          => env('QUEEN_RETRY_ATTEMPTS', 3),
    'load_balancing_strategy' => env('QUEEN_LB_STRATEGY', 'affinity'),
    'headers'                 => [],
];
```

## Tracing

Every message received by a consumer carries a `trace` callable you can invoke to record structured events on that message:

```php
while (!$consumer->isClosed()) {
    $message = $consumer->consume(1000);
    if ($message === null) continue;

    $trace = $message['trace'];
    $trace([
        'traceName' => ['tenant-acme', 'order-processing'],
        'eventType' => 'info',
        'data'      => ['step' => 'started', 'orderId' => $message['payload']['orderId']],
    ]);

    processOrder($message['payload']);

    $trace(['data' => ['step' => 'completed']]);
    $consumer->ack($message);
}
```

## Graceful Shutdown

The high-level consumer traps SIGINT / SIGTERM and exits the loop cleanly:

```php
$consumer = $queen->queue('orders')->group('processors')->getConsumer();
$consumer->subscribe();

while (!$consumer->isClosed()) {
    $message = $consumer->consume(1000);
    if ($message === null) continue;
    processOrder($message['payload']);
    $consumer->ack($message);
}
// Ctrl+C sets isClosed() = true; loop exits cleanly.

$consumer->close();
$queen->close();  // flushes any remaining buffered pushes
```

## Complete Documentation

- [PHP/Laravel Client README](https://github.com/smartpricing/queen/blob/master/client-laravel/README.md)
- [Source on GitHub](https://github.com/smartpricing/queen/tree/master/client-laravel)

## Support

[GitHub Repository](https://github.com/smartpricing/queen)
