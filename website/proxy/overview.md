# Proxy Server

Authentication and SSL proxy for Queen MQ.

## Why Use the Proxy?

- **Authentication** - User login and access control
- **SSL/TLS** - HTTPS encryption

## Architecture

```
Client (HTTPS) → Proxy (Auth/SSL) → Queen Server (HTTP)
```

## Features

- User management (PostgreSQL-backed)
- JWT authentication
- Password hashing (bcrypt)
- Session management
- CORS configuration
- Request logging

## When to Use

Use th proxy when you want to expose the Queen server to the internet for human users.

[Setup Guide](/proxy/setup) | [GitHub](https://github.com/smartpricing/queen/tree/master/proxy)
