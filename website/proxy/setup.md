# Proxy Setup

Install and configure the Queen MQ proxy server.

## Installation

```bash
cd proxy
npm install
```

## Configuration

Create `.env`:

```env
PORT=3000
QUEEN_URL=http://localhost:6632
PG_HOST=localhost
PG_PORT=5432
PG_USER=queen
PG_PASSWORD=password
PG_DB=queen
JWT_SECRET=your-secret-key
SESSION_SECRET=your-session-secret
```

## Create Users

```bash
node src/create-user.js 
```

Then follow the prompts to create users with different roles.

## Create Microservice Tokens

For microservices that need to authenticate programmatically, you can generate long-lived or non-expiring tokens directly when creating a user:

```bash
node src/create-user.js
```

Follow the prompts:

```
=== Queen Proxy - Create User ===

Username: order-service
Password: ********

Roles:
  1) admin       - Full access
  2) read-write  - Read-write access
  3) read-only   - Read-only access

Select role (1-3): 2

Token expiration:
  1) 24h    - 24 hours (default)
  2) 7d     - 7 days
  3) 30d    - 30 days
  4) 1y     - 1 year
  5) never  - No expiration (for microservices)
  6) skip   - Do not generate token now

Select token expiration (1-6) [1]: 5

✓ User created successfully!
  Username: order-service
  Role: read-write
  ID: 42
  Token expiry: Never

  Bearer Token:
  eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpZCI6NDIsInVzZXJuYW1lIjoib3JkZXItc2VydmljZSIsInJvbGUiOiJyZWFkLXdyaXRlIiwiaWF0IjoxNzM1MzA3MjAwfQ.xxxxx

  Use this token in the Authorization header:
  Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
```

::: tip Token Expiration Options
- **24h/7d/30d** - For temporary access or testing
- **1y** - For long-running services with annual rotation
- **never** - For microservices that shouldn't expire (rotate manually if compromised)
- **skip** - Create user without generating a token (use login flow instead)
:::

::: warning Security
Store tokens securely in environment variables or secrets management (e.g., Kubernetes Secrets, HashiCorp Vault). Never commit tokens to version control.
:::

## Run

```bash
npm start
```

Proxy available at `http://localhost:3000`.

## Docker

```bash
docker build -t queen-proxy .
docker run -p 3000:3000 \
  -e QUEEN_URL=http://queen:6632 \
  -e PG_HOST=postgres \
  queen-proxy
```

## nginx SSL

```nginx
server {
    listen 443 ssl;
    server_name queen.example.com;
    
    ssl_certificate /path/to/cert.pem;
    ssl_certificate_key /path/to/key.pem;
    
    location / {
        proxy_pass http://localhost:3000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
    }
}
```

[Complete guide](https://github.com/smartpricing/queen/tree/master/proxy)
