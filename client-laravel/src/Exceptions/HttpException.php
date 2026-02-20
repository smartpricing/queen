<?php

namespace Queen\Exceptions;

class HttpException extends \RuntimeException
{
    public function __construct(
        string $message,
        public readonly int $statusCode,
        int $code = 0,
        ?\Throwable $previous = null,
    ) {
        parent::__construct($message, $code, $previous);
    }
}
