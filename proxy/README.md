# Queen Proxy

Secure authentication proxy for Queen message queue with role-based access control.
Queen server has not authentication or authorization, so this proxy is necessary to secure the access to the Queen server if you want to expose it to the internet.
Mainly intended for exposing the Webapp.

## Features

- JWT-based authentication
- Role-based access control (admin, read-write, read-only)
- Auto-initializes database schema on startup
- Strips large headers to prevent upstream errors
- WebSocket support

## Prerequisites

- Node.js 22+
- PostgreSQL database
- Queen server running

## Quick Start

### 1. Install Dependencies

```bash
npm install
```

### 2. Set Environment Variables

```bash
export DB_HOST=localhost
export DB_PORT=5432
export DB_NAME=postgres
export DB_USER=postgres
export DB_PASSWORD=postgres
export QUEEN_SERVER_URL=http://localhost:6632
export JWT_SECRET=your-secret-key-change-in-production
export PORT=3000
```

### 3. Start the Proxy

```bash
npm start
```

The proxy will automatically create the database schema on first run.

### 4. Create Users

```bash
npm run create-user
```

Follow the prompts to create users with different roles:
- **admin**: Full access to all operations
- **read-write**: Can perform GET, POST, PUT, DELETE
- **read-only**: Can only perform GET operations

## Usage

1. Access Queen through the proxy at `http://localhost:3000`
2. Login with your credentials
3. All requests are authenticated and authorized based on your role
4. Logout button appears in the sidebar when behind proxy

## Docker

### Build

```bash
docker build -t queen-proxy .
```

### Run

```bash
docker run -p 3000:3000 \
  -e DB_HOST=postgres \
  -e DB_NAME=postgres \
  -e DB_USER=postgres \
  -e DB_PASSWORD=postgres \
  -e QUEEN_SERVER_URL=http://queen-server:6632 \
  -e JWT_SECRET=your-secret-key \
  queen-proxy
```

### Create User in Docker

```bash
docker exec -it <container-id> node src/create-user.js
```

## Kubernetes

Deploy using Helm charts in `helm/` directory:

```bash
# Stage environment
helm upgrade --install queen-proxy ./helm -f helm/stage.yaml

# Production environment
helm upgrade --install queen-proxy ./helm -f helm/prod.yaml
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `DB_HOST` | `localhost` | PostgreSQL host |
| `DB_PORT` | `5432` | PostgreSQL port |
| `DB_NAME` | `postgres` | PostgreSQL database name |
| `DB_USER` | `postgres` | PostgreSQL username |
| `DB_PASSWORD` | `postgres` | PostgreSQL password |
| `QUEEN_SERVER_URL` | `http://localhost:8080` | Queen server URL |
| `JWT_SECRET` | ⚠️ Required | Secret key for JWT tokens |
| `JWT_EXPIRES_IN` | `24h` | JWT token expiration |
| `PORT` | `3000` | Proxy server port |
| `NODE_ENV` | `development` | Environment mode |

## Security

- Passwords are hashed using bcrypt with 10 salt rounds
- JWT tokens stored in HTTP-only cookies
- Database schema: `queen_proxy`
- Headers stripped before forwarding to Queen (prevents header size errors)

## Roles

### Admin
- Full access to all HTTP methods
- Can manage system settings

### Read-Write
- GET, POST, PUT, DELETE operations
- Standard user access

### Read-Only
- GET operations only
- Monitoring/viewing access

