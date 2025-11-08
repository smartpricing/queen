# Proxy Server

Authentication and SSL proxy for Queen MQ.

## Why Use the Proxy?

- **Authentication** - User login and access control
- **SSL/TLS** - HTTPS encryption
- **Rate Limiting** - API rate limits
- **Logging** - Centralized request logging
- **Load Balancing** - Distribute across Queen servers

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

✅ Production deployments  
✅ Multi-user environments  
✅ Internet-facing APIs  
✅ Compliance requirements

❌ Development (use Queen directly)  
❌ Internal networks (optional)

[Setup Guide](/proxy/setup) | [GitHub](https://github.com/smartpricing/queen/tree/master/proxy)
