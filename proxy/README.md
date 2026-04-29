# Queen Proxy

Secure authentication proxy for Queen message queue with role-based access control.
Queen server has not authentication or authorization, so this proxy is necessary to secure the access to the Queen server if you want to expose it to the internet.
Mainly intended for exposing the Webapp.

## Features

- JWT-based authentication
- Optional Sign in with Google (OAuth 2.0 / OIDC)
- Optional external SSO passthrough (verify any IDP via JWKS)
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
| `GOOGLE_CLIENT_ID` | _(unset)_ | Google OAuth 2.0 client id (enables "Sign in with Google" when set together with the secret + redirect URI) |
| `GOOGLE_CLIENT_SECRET` | _(unset)_ | Google OAuth 2.0 client secret |
| `GOOGLE_REDIRECT_URI` | _(unset)_ | Must match the Authorized redirect URI in Google Cloud Console, e.g. `https://queen.example.com/api/auth/google/callback` |
| `GOOGLE_ALLOWED_DOMAINS` | _(empty)_ | Comma-separated domain allowlist matched against the `hd` claim or the email domain. Empty = allow any verified email. |
| `GOOGLE_AUTO_PROVISION` | `false` | If `true`, create a local user on first Google login. If `false`, the user must already exist in `queen_proxy.users` (matched by email). |
| `GOOGLE_DEFAULT_ROLE` | `read-only` | Role assigned to auto-provisioned Google users (`admin`, `read-write`, or `read-only`). |

## Sign in with Google

When `GOOGLE_CLIENT_ID`, `GOOGLE_CLIENT_SECRET` and `GOOGLE_REDIRECT_URI` are
all set, the login page exposes a "Sign in with Google" button and the proxy
runs the OAuth 2.0 Authorization Code flow:

1. Browser hits `GET /api/auth/google` → 302 to `accounts.google.com`.
2. Google redirects back to `GET /api/auth/google/callback?code=…&state=…`.
3. The proxy exchanges the code, verifies the `id_token` against Google's JWKS,
   then resolves the local user:
   - by `google_sub` if previously linked, else
   - by verified email (links the Google identity to the existing local user),
     else
   - auto-provisions a new user when `GOOGLE_AUTO_PROVISION=true`, else
   - denies with `?error=not_provisioned`.
4. A standard internal JWT cookie is set, identical to the password flow, so
   the rest of the system (RBAC + Queen forwarding) is unchanged.

### Google Cloud Console setup

1. Create an OAuth 2.0 Client ID of type **Web application**.
2. Add the redirect URI you'll set in `GOOGLE_REDIRECT_URI` (must include the
   `/api/auth/google/callback` path).
3. Request scopes `openid email profile` (the proxy does this automatically).

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

