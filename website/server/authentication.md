# Authentication

Queen MQ supports built-in JWT authentication for securing API endpoints without requiring a separate proxy.

## Overview

There are three deployment options for authentication:

| Option | Description | Use Case |
|--------|-------------|----------|
| **Direct JWT** | Clients authenticate directly with Queen using JWT tokens | IoT devices, microservices, external APIs |
| **Proxy Only** | Proxy handles auth, Queen has no authentication | Internal networks, legacy setups |
| **Proxy + JWT** | Both proxy and Queen validate tokens | Defense in depth, zero-trust networks |

## Quick Start (Direct JWT)

Enable JWT authentication with a shared secret:

```bash
JWT_ENABLED=true \
JWT_ALGORITHM=HS256 \
JWT_SECRET=your-secret-key \
./bin/queen-server
```

Test with curl:

```bash
# Without token - returns 401
curl http://localhost:6632/api/v1/status
# {"error":"Authentication required"}

# With valid token - returns 200
curl -H "Authorization: Bearer <your-jwt-token>" \
  http://localhost:6632/api/v1/status
```

## Supported Algorithms

### HS256 (HMAC-SHA256)

Symmetric algorithm using a shared secret. Simple to set up, ideal when you control both token generation and validation.

```bash
JWT_ENABLED=true
JWT_ALGORITHM=HS256
JWT_SECRET=your-secret-key-at-least-32-chars
```

### RS256 (RSA-SHA256)

Asymmetric algorithm using public/private key pairs. Ideal for external identity providers (Auth0, Keycloak, Okta, etc.).

**Option A: JWKS URL (recommended)**

```bash
JWT_ENABLED=true
JWT_ALGORITHM=RS256
JWT_JWKS_URL=https://your-idp.com/.well-known/jwks.json
```

**Option B: Static public key**

```bash
JWT_ENABLED=true
JWT_ALGORITHM=RS256
JWT_PUBLIC_KEY="-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA...
-----END PUBLIC KEY-----"
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `JWT_ENABLED` | `false` | Enable/disable JWT authentication |
| `JWT_ALGORITHM` | `HS256` | Algorithm: `HS256`, `RS256`, or `auto` |
| `JWT_SECRET` | - | Shared secret for HS256 |
| `JWT_JWKS_URL` | - | JWKS endpoint URL for RS256 |
| `JWT_PUBLIC_KEY` | - | Static PEM public key for RS256 |
| `JWT_ISSUER` | - | Expected `iss` claim (optional) |
| `JWT_AUDIENCE` | - | Expected `aud` claim (optional) |
| `JWT_CLOCK_SKEW` | `30` | Clock skew tolerance in seconds |
| `JWT_SKIP_PATHS` | `/health,/metrics,/` | Paths that skip authentication |
| `JWT_ROLES_CLAIM` | `role` | Claim name for role (single value) |
| `JWT_ROLES_ARRAY_CLAIM` | `roles` | Claim name for roles (array) |
| `JWT_ROLE_ADMIN` | `admin` | Role value for admin access |
| `JWT_ROLE_READ_WRITE` | `read-write` | Role value for read-write access |
| `JWT_ROLE_READ_ONLY` | `read-only` | Role value for read-only access |

## Role-Based Access Control (RBAC)

Queen enforces role-based access on all endpoints:

| Access Level | Endpoints | Required Role |
|--------------|-----------|---------------|
| **PUBLIC** | `/health`, `/metrics`, `/` (dashboard) | None |
| **READ_ONLY** | `GET /api/v1/status/*`, `GET /api/v1/resources/*`, `GET /api/v1/messages/*`, `GET /api/v1/consumer-groups/*` | `read-only`, `read-write`, or `admin` |
| **READ_WRITE** | `/api/v1/push`, `/api/v1/pop/*`, `/api/v1/ack/*`, `/api/v1/configure`, `/api/v1/transaction` | `read-write` or `admin` |
| **ADMIN** | `/api/v1/system/*`, `DELETE /api/v1/resources/queues/*`, `DELETE /api/v1/consumer-groups/*` | `admin` only |

### Token Structure

Tokens should include a `role` claim (or `roles` array):

```json
{
  "sub": "user-123",
  "username": "alice",
  "role": "read-write",
  "iat": 1704067200,
  "exp": 1704153600
}
```

Or with roles array:

```json
{
  "sub": "service-account",
  "roles": ["read-write", "admin"],
  "iat": 1704067200,
  "exp": 1704153600
}
```

## Producer Identity Stamping (`producerSub`)

When JWT authentication is enabled, Queen **automatically stamps every pushed
message with the authenticated producer's `sub` claim** and stores it on the
message. Consumers can read the stamped identity from the `producerSub` field
on popped messages and trust it — a client cannot set or forge this field.

This closes a class of impersonation attacks where one authenticated producer
pretends to be another by embedding a fake identity inside the payload.

### Guarantees

- `producerSub` is set **server-side from the validated JWT `sub` claim**.
- Any `producerSub` field supplied in the HTTP push body is **ignored**.
- When JWT auth is disabled, `producerSub` is `NULL` (no identity to attest).
- The field survives failover: messages buffered to disk during maintenance
  mode or DB outages keep the producer identity when replayed.

### Example

Producer (authenticated as `alice-producer`):

```javascript
await queen.queue('orders').push([{ data: { orderId: 42 } }])
// Request body sent to server - note: no producerSub here.
// Server writes producer_sub='alice-producer' to the message row.
```

Consumer:

```javascript
const [msg] = await queen.queue('orders').batch(1).pop()
console.log(msg.producerSub)   // 'alice-producer' (trusted, from JWT)
console.log(msg.data.orderId)  // 42 (untrusted, from client payload)
```

Attempted spoofing is silently ignored:

```javascript
// Even with a valid JWT for alice-producer, this producerSub is dropped.
await fetch('/api/v1/push', {
  method: 'POST',
  headers: { Authorization: `Bearer ${aliceToken}` },
  body: JSON.stringify({
    items: [{ queue: 'orders', payload: {}, producerSub: 'attacker' }]
  })
})
// Stored on the message: producer_sub = 'alice-producer' (NOT 'attacker').
```

### Where `producerSub` appears

| Endpoint | Field location |
|---|---|
| `POST /api/v1/pop` | `messages[].producerSub` |
| `GET /api/v1/messages` (admin) | `messages[].producerSub` |
| `GET /api/v1/messages/:partitionId/:txId` (admin) | `producerSub` |
| `GET /api/v1/dlq` | `messages[].producerSub` |

The field is omitted when `NULL` (i.e. when JWT auth was disabled at push
time), so existing consumers that don't use it see no change in wire format.

## Generating Tokens

### Using Node.js

```javascript
const jwt = require('jsonwebtoken');

const token = jwt.sign(
  {
    sub: 'user-123',
    username: 'alice',
    role: 'admin'
  },
  process.env.JWT_SECRET,
  { expiresIn: '24h' }
);

console.log(token);
```

### Using Python

```python
import jwt
import os
from datetime import datetime, timedelta

token = jwt.encode(
    {
        'sub': 'user-123',
        'username': 'alice',
        'role': 'admin',
        'exp': datetime.utcnow() + timedelta(hours=24)
    },
    os.environ['JWT_SECRET'],
    algorithm='HS256'
)

print(token)
```

## Client Integration

### JavaScript Client

```javascript
import { Queen } from 'queen-mq';

const queen = new Queen({
  url: 'http://localhost:6632',
  bearerToken: process.env.QUEEN_TOKEN
});

await queen.queue('orders').push({ orderId: 123 });
```

### Python Client

```python
from queen_mq import Queen

queen = Queen(
    url='http://localhost:6632',
    bearer_token=os.environ["QUEEN_TOKEN"]
)

await queen.queue('orders').push({'orderId': 123})
```

## See Also

- [Proxy Setup](/proxy/setup) - Using proxy with JWT authentication
- [Deployment](/server/deployment) - Production deployment guide

