# 11 — Auth proxy

The Queen broker has **no built-in authentication or authorization** — it trusts whoever can reach the TCP port. To safely expose Queen (and especially its dashboard) on the public internet, we ship a Node.js proxy in `proxy/` that adds:

- JWT-based authentication (local users + Sign-in-with-Google + arbitrary IDP via JWKS)
- Role-based access control (`admin`, `read-write`, `read-only`)
- Header stripping (prevents oversized headers from breaking upstream)
- WebSocket pass-through

This page is the developer's view. For end-user / operator details, see [`proxy/README.md`](../proxy/README.md).

---

## When you do (and don't) need the proxy

You **do** need the proxy when:

- The broker / dashboard is exposed on the public internet
- Multiple human users access the dashboard with different permissions
- You want to integrate with Google Workspace SSO

You **don't** need it when:

- The broker is on a private network only
- All callers are services using JWT validation already enabled directly on the broker (the broker has its own JWT validation path for `producer_sub` stamping; see `server/src/auth/`)
- You're running locally for development

The proxy and the broker's own JWT auth are not exclusive — you can run both. In practice, for human-facing dashboard access we always use the proxy; for service-to-service we enable broker-level JWT.

---

## Layout

```
proxy/
├── package.json
├── src/                          ← flat, no subfolders
│   ├── server.js                 main HTTP entry (Express)
│   ├── auth.js                   local users + external JWKS passthrough + RBAC
│   ├── google-auth.js            Google OAuth 2.0 / OIDC
│   ├── middleware.js             JWT verify, header strip, request logging
│   ├── db.js                     `queen_proxy` schema bootstrap + user CRUD
│   └── create-user.js            CLI: `npm run create-user`
├── public/                       login page assets
├── helm/                         k8s manifest templates (NOT a real Helm chart — no Chart.yaml)
├── Dockerfile
└── README.md
```

---

## How it sits in front of the broker

```
Internet ──▶ Auth Proxy (Node.js, :3000)
                │
                │ verifies JWT cookie or Google ID token,
                │ checks role, strips oversized headers
                ▼
            Queen Broker (:6632)
                │
                ▼
            Postgres
```

Both proxy and broker can share the same Postgres (the proxy uses a separate schema, `queen_proxy`, so it doesn't touch the broker's data).

---

## Setup

```bash
cd proxy
nvm use 22
npm install
```

### Environment variables

The minimum to get going:

```bash
export DB_HOST=localhost DB_PORT=5432 DB_NAME=postgres
export DB_USER=postgres DB_PASSWORD=postgres
export QUEEN_SERVER_URL=http://localhost:6632
export JWT_SECRET=replace-me-in-production
export PORT=3000
```

The proxy auto-bootstraps its own schema (`queen_proxy.users`, `queen_proxy.sessions`, …) on first start.

### Run

```bash
npm start
```

Now visit `http://localhost:3000`. You'll be redirected to a login page.

### Create a user

```bash
npm run create-user
```

Interactive prompt for username, email, password, and role.

---

## Roles

Three roles, mapped to HTTP method allowlists:

| Role | Allowed methods |
|------|-----------------|
| `admin` | `*` (everything) |
| `read-write` | `GET`, `POST`, `PUT`, `DELETE` |
| `read-only` | `GET` |

If you need finer granularity, the place to extend it is `proxy/src/auth.js` (look for the `role` checks). Per-route roles are not currently supported.

---

## Sign-in with Google

When `GOOGLE_CLIENT_ID`, `GOOGLE_CLIENT_SECRET`, and `GOOGLE_REDIRECT_URI` are all set, the login page exposes a "Sign in with Google" button:

1. Browser hits `GET /api/auth/google` → 302 to `accounts.google.com`
2. Google redirects back to `GET /api/auth/google/callback?code=…&state=…`
3. The proxy exchanges the code, verifies the `id_token` against Google's JWKS
4. Resolves the local user (by `google_sub`, then by email; auto-provisions a new local user if `GOOGLE_AUTO_PROVISION=true`)
5. Sets the same internal JWT cookie as the password flow

`GOOGLE_ALLOWED_DOMAINS` (comma-separated) restricts which email domains can log in. Empty = any verified email.

`GOOGLE_DEFAULT_ROLE` (default `read-only`) is the role assigned to auto-provisioned users. **Never make this `admin` in production.**

---

## External IDP / JWKS pass-through

For "we already have an SSO; just verify its tokens":

```bash
export EXTERNAL_JWKS_URL=https://login.example.com/.well-known/jwks.json
export EXTERNAL_ISSUER=https://login.example.com
export EXTERNAL_AUDIENCE=queen
# (legacy aliases JWT_JWKS_URL / JWT_ISSUER / JWT_AUDIENCE are accepted too)
```

The proxy fetches and caches the JWKS for **1 hour**, validates the bearer token (RS256, EdDSA, anything `jose` accepts), and maps token claims to a local role. **Role mapping is hard-coded** to `payload.role || payload.roles[0] || 'read-only'` (see `proxy/src/auth.js`). If you need a custom claim name (e.g. `groups`, `permissions`), edit that line — there's no env var for it.

Implementation in `proxy/src/auth.js` (look for `verifyExternalToken` and `getJWKS`).

---

## Header stripping

The proxy strips request headers larger than a threshold before forwarding to the broker. This protects against:

- Oversized cookies from third-party JavaScript on the dashboard origin
- Some browsers' `Authorization` headers when used with very long bearer tokens

The strip logic lives in `proxy/src/middleware.js`. If you add a new header that the broker needs, add it to the allow list there.

---

## WebSocket support

The dashboard uses WebSockets for some realtime updates. The proxy passes through `Upgrade` / `Connection` headers, so this works out of the box. If you add a new WebSocket endpoint:

- Make sure it's mounted under `/api/*` (the proxy only forwards that prefix by default)
- Auth check happens on the initial HTTP handshake, before the upgrade

---

## Helm

```
proxy/helm/
└── templates/
    ├── service.yaml
    ├── statefulset.yaml
    ├── httproute.yaml
    └── healthcheckpolicy.yaml
```

> Note: `proxy/helm/` is **not** a self-contained Helm chart — there is no `Chart.yaml`. The team's deployment tooling renders these templates with values from the repo-root `helm/{dev,stage,prod}.yaml` files (which are plain YAML, not Helm `values.yaml`). If you want to run them under stock Helm you'll need to add a `Chart.yaml` and convert the values files.

If you change a template, validate by rendering it locally with whatever tool the team uses (typically `kustomize`-style YAML interpolation in CI, not raw `helm`).

---

## Security checklist before exposing the proxy

- [ ] `JWT_SECRET` is a random 64+ char value, **not** the default
- [ ] `NODE_ENV=production` (changes cookie flags to `Secure`)
- [ ] Behind TLS (terminate at an nginx / cloud LB; the proxy speaks plain HTTP)
- [ ] Rate limiter on the LB (the proxy itself doesn't rate-limit)
- [ ] If using Google: `GOOGLE_ALLOWED_DOMAINS` is set
- [ ] If using external JWKS: `EXTERNAL_AUDIENCE` is set so foreign-issued tokens are rejected
- [ ] No admin user has the default password
- [ ] `queen_proxy.users` audit logged for new admin creation

---

## Common gotchas

- **Login redirects to `/api/auth/login` and gets a 404.** The proxy expects the dashboard at the same origin. If you've split origins (dashboard at `dash.example.com`, broker at `api.example.com`), you need to either keep them same-origin behind the proxy, or implement CORS+credentials and pin the cookie domain.
- **"Cannot read property 'sub' of undefined" on Google callback.** Almost always `GOOGLE_REDIRECT_URI` mismatches the URI in Google Cloud Console.
- **Bearer tokens stripped by header-strip middleware.** The strip logic in `proxy/src/middleware.js` should *not* strip `Authorization`. Verify after upgrades.
- **Auth-then-broker latency.** The proxy adds ~1–2 ms overhead. For high-throughput service-to-service traffic, bypass the proxy and use broker-level JWT instead.
