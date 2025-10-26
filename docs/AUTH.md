# Authentication System for Queen

**Status:** Planning Phase  
**Version:** 1.0  
**Last Updated:** October 13, 2025

---

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Database Schema](#database-schema)
- [Token Types](#token-types)
- [Role-Based Access Control](#role-based-access-control)
- [Authentication Flows](#authentication-flows)
- [API Endpoints](#api-endpoints)
- [CLI Tool](#cli-tool)
- [Configuration](#configuration)
- [Security Considerations](#security-considerations)
- [Implementation Roadmap](#implementation-roadmap)
- [Future Enhancements](#future-enhancements)

---

## Overview

Queen's authentication system is designed to be:

- **Optional:** Disabled by default (`QUEEN_AUTH_ENABLED=false`)
- **Backward Compatible:** Existing deployments continue to work without changes
- **Zero Overhead:** No performance impact when disabled
- **Token-Based:** Stateless authentication using JWT and service tokens
- **Role-Based:** Three roles with clear permission boundaries
- **Flexible:** Support for both microservices and human users

### Design Principles

1. **Security First:** Industry-standard encryption, hashing, and token management
2. **Developer-Friendly:** Simple CLI tools for token generation and user management
3. **Production-Ready:** Audit logging, token revocation, and monitoring
4. **Scalable:** Stateless design works across multiple Queen server instances

---

## Architecture

### Request Flow

```
Client Request
    ↓
[CORS Middleware]
    ↓
[Auth Middleware] ← Checks QUEEN_AUTH_ENABLED
    ├─ Extract token from Authorization header
    ├─ Identify token type (service token vs JWT)
    ├─ Validate token signature/hash
    ├─ Check expiration (if applicable)
    ├─ Load permissions from database
    ├─ Attach auth context to request
    ↓
[Permission Middleware]
    ├─ Check role-based permissions
    ├─ Check queue-level access (if restricted)
    ├─ Check operation-level access (if restricted)
    ↓
[Route Handler]
    ↓
[Audit Logging] ← Log auth events
```

### Component Structure

```
src/
  auth/
    middleware.js       # Auth middleware for request validation
    permissions.js      # Permission checking logic
    tokens.js          # Token generation, validation, hashing
    jwt.js             # JWT utilities (sign, verify)
    passwords.js       # Password hashing (bcrypt)
    audit.js           # Audit logging functions
  routes/
    auth.js            # Auth endpoints (login, refresh, logout)
    users.js           # User management endpoints (admin only)
    tokens.js          # Service token management endpoints (admin only)
  cli/
    auth.js            # CLI tool for auth management
  database/
    auth-schema.sql    # Auth tables schema
```

---

## Database Schema

### Tables

#### 1. `queen.auth_users`

User accounts for dashboard access and CLI operations.

```sql
CREATE TABLE queen.auth_users (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    username VARCHAR(255) UNIQUE NOT NULL,
    email VARCHAR(255) UNIQUE,
    password_hash TEXT NOT NULL,  -- bcrypt hash
    role VARCHAR(50) NOT NULL CHECK (role IN ('admin', 'rw', 'read')),
    enabled BOOLEAN DEFAULT true,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    last_login_at TIMESTAMPTZ,
    created_by UUID REFERENCES queen.auth_users(id),
    
    CONSTRAINT username_not_empty CHECK (length(username) > 0)
);

CREATE INDEX idx_auth_users_username ON queen.auth_users(username);
CREATE INDEX idx_auth_users_role ON queen.auth_users(role);
CREATE INDEX idx_auth_users_enabled ON queen.auth_users(enabled) WHERE enabled = true;
```

#### 2. `queen.auth_service_tokens`

Long-lived tokens for microservices and automated systems.

```sql
CREATE TABLE queen.auth_service_tokens (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name VARCHAR(255) NOT NULL,  -- Descriptive name: "payment-service", "analytics-worker"
    token_hash TEXT UNIQUE NOT NULL,  -- SHA-256 hash of the token
    role VARCHAR(50) NOT NULL CHECK (role IN ('admin', 'rw', 'read')),
    
    -- Queue-level access control
    allowed_queues JSONB DEFAULT NULL,  -- null = all queues ("*"), or ["orders", "payments"]
    
    -- Operation-level access control (optional fine-grained control)
    allowed_operations JSONB DEFAULT NULL,  -- null = all operations based on role, or ["push", "pop"]
    
    enabled BOOLEAN DEFAULT true,
    expires_at TIMESTAMPTZ DEFAULT NULL,  -- NULL = never expires
    
    created_by UUID REFERENCES queen.auth_users(id),
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    last_used_at TIMESTAMPTZ,
    
    CONSTRAINT name_not_empty CHECK (length(name) > 0)
);

CREATE INDEX idx_auth_service_tokens_hash ON queen.auth_service_tokens(token_hash);
CREATE INDEX idx_auth_service_tokens_enabled ON queen.auth_service_tokens(enabled) WHERE enabled = true;
CREATE INDEX idx_auth_service_tokens_expires ON queen.auth_service_tokens(expires_at) WHERE expires_at IS NOT NULL;
```

#### 3. `queen.auth_refresh_tokens`

Refresh tokens for user sessions (dashboard, CLI).

```sql
CREATE TABLE queen.auth_refresh_tokens (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES queen.auth_users(id) ON DELETE CASCADE,
    token_hash TEXT UNIQUE NOT NULL,  -- SHA-256 hash
    expires_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    revoked_at TIMESTAMPTZ DEFAULT NULL,
    revoked_by UUID REFERENCES queen.auth_users(id),
    revoke_reason TEXT,
    
    -- Session tracking
    ip_address VARCHAR(45),
    user_agent TEXT
);

CREATE INDEX idx_auth_refresh_tokens_user ON queen.auth_refresh_tokens(user_id);
CREATE INDEX idx_auth_refresh_tokens_hash ON queen.auth_refresh_tokens(token_hash);
CREATE INDEX idx_auth_refresh_tokens_expires ON queen.auth_refresh_tokens(expires_at);
CREATE INDEX idx_auth_refresh_tokens_active ON queen.auth_refresh_tokens(user_id, revoked_at) 
    WHERE revoked_at IS NULL;
```

#### 4. `queen.auth_audit_log`

Comprehensive audit trail for security and compliance.

```sql
CREATE TABLE queen.auth_audit_log (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    
    -- Actor (who performed the action)
    user_id UUID REFERENCES queen.auth_users(id),
    service_token_id UUID REFERENCES queen.auth_service_tokens(id),
    
    -- Event details
    event_type VARCHAR(50) NOT NULL,  -- 'login', 'logout', 'token_created', 'unauthorized_access', etc.
    resource_type VARCHAR(50),  -- 'user', 'token', 'queue', 'message'
    resource_id VARCHAR(255),
    
    -- Request context
    ip_address VARCHAR(45),
    user_agent TEXT,
    endpoint VARCHAR(255),
    http_method VARCHAR(10),
    
    -- Result
    success BOOLEAN NOT NULL,
    error_message TEXT,
    
    -- Additional context
    details JSONB,
    
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_auth_audit_log_user ON queen.auth_audit_log(user_id);
CREATE INDEX idx_auth_audit_log_service_token ON queen.auth_audit_log(service_token_id);
CREATE INDEX idx_auth_audit_log_event_type ON queen.auth_audit_log(event_type);
CREATE INDEX idx_auth_audit_log_created_at ON queen.auth_audit_log(created_at DESC);
CREATE INDEX idx_auth_audit_log_success ON queen.auth_audit_log(success) WHERE success = false;
```

---

## Token Types

### 1. Service Tokens (Long-Lived)

**Purpose:** Microservices, workers, automated systems

**Format:**
```
qn_svc_<base58_encoded_32_bytes>

Example:
qn_svc_8K7jQm3pN2xR5wV9yB4cE6fH8jL1mN3qT5uW7xZ9a
```

**Properties:**
- Long-lived or never expires (configurable)
- Stored as SHA-256 hash in database
- Can be scoped to specific queues
- Can be scoped to specific operations
- No refresh mechanism (regenerate if compromised)

**Storage:**
```javascript
{
  id: "uuid",
  name: "payment-service",
  token_hash: "sha256_hash_of_actual_token",
  role: "rw",
  allowed_queues: null,  // "*" = all queues
  allowed_operations: null,  // null = all operations based on role
  expires_at: null  // never expires
}
```

**Queue Access Examples:**
- `allowed_queues: null` → Access to all queues ("*")
- `allowed_queues: ["orders", "payments"]` → Only these queues
- `allowed_queues: ["orders/*"]` → All partitions in orders queue (future enhancement)

### 2. JWT Access Tokens (Short-Lived)

**Purpose:** Dashboard users, CLI users (short-term access)

**Format:** Standard JWT
```
eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiJ1c2VyLXV1aWQiLCJ1c2VybmFtZSI6ImFsaWNlIiwicm9sZSI6ImFkbWluIiwidHlwZSI6ImFjY2VzcyIsImlhdCI6MTY5NzE5MzYwMCwiZXhwIjoxNjk3MTk0NTAwfQ.signature
```

**Payload:**
```json
{
  "sub": "user-uuid",
  "username": "alice",
  "role": "admin",
  "type": "access",
  "iat": 1697193600,
  "exp": 1697194500
}
```

**Properties:**
- Short-lived (15 minutes default)
- Stateless (no database lookup required)
- Verified via JWT signature
- No revocation (use short expiration instead)

### 3. JWT Refresh Tokens (Medium-Lived)

**Purpose:** Renew access tokens without re-authentication

**Format:** Standard JWT
```json
{
  "sub": "user-uuid",
  "type": "refresh",
  "jti": "refresh-token-uuid",
  "iat": 1697193600,
  "exp": 1697798400
}
```

**Properties:**
- Medium-lived (7 days default)
- Stored as hash in `auth_refresh_tokens` table
- Can be revoked (logout, security breach)
- Single-use (rotate on each refresh)

---

## Role-Based Access Control

### Roles

| Role | Description | Primary Use Case |
|------|-------------|------------------|
| `admin` | Full system access | System administrators, DevOps |
| `rw` | Read-write access + queue configuration | Microservices, workers, producers/consumers |
| `read` | Read-only access | Monitoring, analytics, observability |

### Permission Matrix

| Operation | admin | rw | read |
|-----------|-------|----|----- |
| **Queue Operations** | | | |
| Configure queue | ✅ | ✅ | ❌ |
| Delete queue | ✅ | ❌ | ❌ |
| **Message Operations** | | | |
| Push message | ✅ | ✅ | ❌ |
| Pop message | ✅ | ✅ | ❌ |
| Acknowledge message | ✅ | ✅ | ❌ |
| Delete message | ✅ | ❌ | ❌ |
| Browse messages | ✅ | ✅ | ✅ |
| Retry message | ✅ | ✅ | ❌ |
| **Analytics** | | | |
| View queue stats | ✅ | ✅ | ✅ |
| View throughput | ✅ | ✅ | ✅ |
| View system health | ✅ | ✅ | ✅ |
| **Auth Management** | | | |
| Manage users | ✅ | ❌ | ❌ |
| Manage service tokens | ✅ | ❌ | ❌ |
| View audit logs | ✅ | ❌ | ❌ |

### Permission Identifiers

```javascript
const PERMISSIONS = {
  // Queue permissions
  'queue:configure': ['admin', 'rw'],
  'queue:delete': ['admin'],
  'queue:view': ['admin', 'rw', 'read'],
  
  // Message permissions
  'message:push': ['admin', 'rw'],
  'message:pop': ['admin', 'rw'],
  'message:ack': ['admin', 'rw'],
  'message:delete': ['admin'],
  'message:browse': ['admin', 'rw', 'read'],
  'message:retry': ['admin', 'rw'],
  
  // Analytics permissions
  'analytics:read': ['admin', 'rw', 'read'],
  
  // System permissions
  'system:health': ['admin', 'rw', 'read'],
  'system:metrics': ['admin', 'rw', 'read'],
  
  // Auth permissions
  'auth:manage_users': ['admin'],
  'auth:manage_tokens': ['admin'],
  'auth:view_audit': ['admin']
};
```

### Queue-Level Access Control

Service tokens can be restricted to specific queues:

**Examples:**

```javascript
// Full access to all queues
{
  name: "payment-service",
  role: "rw",
  allowed_queues: null  // or ["*"]
}

// Access only to specific queues
{
  name: "analytics-worker",
  role: "rw",
  allowed_queues: ["events", "analytics", "metrics"]
}

// Read-only access to all queues
{
  name: "monitoring-service",
  role: "read",
  allowed_queues: null  // "*"
}
```

**Validation Logic:**
```
if (token.allowed_queues === null || token.allowed_queues.includes("*")) {
  // Access to all queues
  return true;
} else if (token.allowed_queues.includes(requestedQueue)) {
  // Access granted
  return true;
} else {
  // Access denied
  return false;
}
```

---

## Authentication Flows

### Flow 1: Service Token Authentication

```
Microservice                    Queen Server
    |                                |
    |  POST /api/v1/push             |
    |  Authorization: Bearer qn_svc_...|
    |----------------------------------->
    |                                |
    |    [Validate Token]            |
    |    1. Extract token            |
    |    2. Hash with SHA-256        |
    |    3. Look up in DB            |
    |    4. Check enabled & expiration|
    |    5. Load role & permissions  |
    |    6. Check queue access       |
    |                                |
    |  200 OK                        |
    |  { messages: [...] }           |
    |<-----------------------------------
    |                                |
    |  [Update last_used_at]         |
    |  [Log audit event]             |
```

### Flow 2: User Login (Dashboard/CLI)

```
User/CLI                        Queen Server                Database
    |                                |                          |
    |  POST /api/v1/auth/login       |                          |
    |  { username, password }        |                          |
    |----------------------------------->                       |
    |                                |                          |
    |                          [Authenticate]                   |
    |                                |  SELECT * FROM users     |
    |                                |  WHERE username = ?      |
    |                                |------------------------->|
    |                                |  { user }                |
    |                                |<-------------------------|
    |                                |                          |
    |                          [Verify bcrypt]                  |
    |                          [Generate tokens]                |
    |                                |  INSERT refresh_token    |
    |                                |------------------------->|
    |                                |                          |
    |  200 OK                        |                          |
    |  { accessToken, refreshToken } |                          |
    |<-----------------------------------                       |
    |                                |                          |
    |  [Store tokens]                |  [Log audit event]       |
```

### Flow 3: Token Refresh

```
User/CLI                        Queen Server                Database
    |                                |                          |
    |  POST /api/v1/auth/refresh     |                          |
    |  { refreshToken }              |                          |
    |----------------------------------->                       |
    |                                |                          |
    |                          [Validate JWT]                   |
    |                                |  SELECT * FROM refresh   |
    |                                |  WHERE hash = ? AND      |
    |                                |  revoked_at IS NULL      |
    |                                |------------------------->|
    |                                |  { refresh_token }       |
    |                                |<-------------------------|
    |                                |                          |
    |                          [Generate new access token]      |
    |                          [Rotate refresh token]           |
    |                                |  UPDATE old token        |
    |                                |  INSERT new token        |
    |                                |------------------------->|
    |                                |                          |
    |  200 OK                        |                          |
    |  { accessToken, refreshToken } |                          |
    |<-----------------------------------                       |
```

### Flow 4: Logout

```
User/CLI                        Queen Server                Database
    |                                |                          |
    |  POST /api/v1/auth/logout      |                          |
    |  Authorization: Bearer <jwt>   |                          |
    |  { refreshToken }              |                          |
    |----------------------------------->                       |
    |                                |                          |
    |                          [Validate JWT]                   |
    |                                |  UPDATE refresh_tokens   |
    |                                |  SET revoked_at = NOW()  |
    |                                |  WHERE hash = ?          |
    |                                |------------------------->|
    |                                |                          |
    |  200 OK                        |                          |
    |  { message: "Logged out" }     |                          |
    |<-----------------------------------                       |
    |                                |  [Log audit event]       |
```

---

## API Endpoints

### Authentication Endpoints

#### POST `/api/v1/auth/login`

User login with username/password.

**Request:**
```json
{
  "username": "alice",
  "password": "secret123"
}
```

**Response:**
```json
{
  "accessToken": "eyJhbGc...",
  "refreshToken": "eyJhbGc...",
  "expiresIn": 900,
  "user": {
    "id": "uuid",
    "username": "alice",
    "role": "admin",
    "email": "alice@company.com"
  }
}
```

**Status Codes:**
- `200 OK` - Login successful
- `401 Unauthorized` - Invalid credentials
- `403 Forbidden` - Account disabled

---

#### POST `/api/v1/auth/refresh`

Refresh access token using refresh token.

**Request:**
```json
{
  "refreshToken": "eyJhbGc..."
}
```

**Response:**
```json
{
  "accessToken": "eyJhbGc...",
  "refreshToken": "eyJhbGc...",  // New refresh token (rotation)
  "expiresIn": 900
}
```

**Status Codes:**
- `200 OK` - Token refreshed
- `401 Unauthorized` - Invalid or expired refresh token

---

#### POST `/api/v1/auth/logout`

Logout and revoke refresh token.

**Request:**
```json
{
  "refreshToken": "eyJhbGc..."
}
```

**Response:**
```json
{
  "message": "Logged out successfully"
}
```

**Status Codes:**
- `200 OK` - Logged out
- `401 Unauthorized` - Invalid token

---

#### GET `/api/v1/auth/me`

Get current authenticated user info.

**Headers:**
```
Authorization: Bearer <access_token or service_token>
```

**Response:**
```json
{
  "type": "user",  // or "service"
  "id": "uuid",
  "username": "alice",  // or "name" for service tokens
  "role": "admin",
  "permissions": ["queue:configure", "message:push", ...]
}
```

**Status Codes:**
- `200 OK` - User info returned
- `401 Unauthorized` - Invalid token

---

### User Management Endpoints (Admin Only)

#### POST `/api/v1/auth/users`

Create a new user.

**Request:**
```json
{
  "username": "bob",
  "email": "bob@company.com",
  "password": "secret123",
  "role": "rw"
}
```

**Response:**
```json
{
  "id": "uuid",
  "username": "bob",
  "email": "bob@company.com",
  "role": "rw",
  "enabled": true,
  "created_at": "2025-10-13T10:00:00Z"
}
```

---

#### GET `/api/v1/auth/users`

List all users.

**Query Parameters:**
- `role` - Filter by role
- `enabled` - Filter by enabled status
- `limit` - Page size (default: 100)
- `offset` - Page offset (default: 0)

**Response:**
```json
{
  "users": [
    {
      "id": "uuid",
      "username": "alice",
      "email": "alice@company.com",
      "role": "admin",
      "enabled": true,
      "created_at": "2025-10-13T10:00:00Z",
      "last_login_at": "2025-10-13T12:00:00Z"
    }
  ],
  "total": 10
}
```

---

#### GET `/api/v1/auth/users/:id`

Get user details.

**Response:**
```json
{
  "id": "uuid",
  "username": "alice",
  "email": "alice@company.com",
  "role": "admin",
  "enabled": true,
  "created_at": "2025-10-13T10:00:00Z",
  "updated_at": "2025-10-13T10:00:00Z",
  "last_login_at": "2025-10-13T12:00:00Z"
}
```

---

#### PUT `/api/v1/auth/users/:id`

Update user details.

**Request:**
```json
{
  "email": "newemail@company.com",
  "role": "admin",
  "enabled": true
}
```

---

#### PUT `/api/v1/auth/users/:id/password`

Change user password.

**Request:**
```json
{
  "newPassword": "newsecret123"
}
```

---

#### DELETE `/api/v1/auth/users/:id`

Delete user (soft delete - disable account).

---

### Service Token Management Endpoints (Admin Only)

#### POST `/api/v1/auth/tokens`

Create a new service token.

**Request:**
```json
{
  "name": "payment-service",
  "role": "rw",
  "allowed_queues": ["orders", "payments"],  // null or ["*"] for all queues
  "allowed_operations": null,  // null for all operations based on role
  "expires_at": "2026-10-13T00:00:00Z"  // null for never expires
}
```

**Response:**
```json
{
  "id": "uuid",
  "name": "payment-service",
  "token": "qn_svc_8K7jQm3pN2xR5wV9yB4cE6fH8jL1mN3qT5uW7xZ9a",
  "role": "rw",
  "allowed_queues": ["orders", "payments"],
  "allowed_operations": null,
  "expires_at": "2026-10-13T00:00:00Z",
  "created_at": "2025-10-13T10:00:00Z"
}
```

**⚠️ Note:** The actual token is only shown once during creation. Store it securely.

---

#### GET `/api/v1/auth/tokens`

List all service tokens.

**Query Parameters:**
- `role` - Filter by role
- `enabled` - Filter by enabled status
- `limit` - Page size (default: 100)
- `offset` - Page offset (default: 0)

**Response:**
```json
{
  "tokens": [
    {
      "id": "uuid",
      "name": "payment-service",
      "role": "rw",
      "allowed_queues": ["orders", "payments"],
      "allowed_operations": null,
      "enabled": true,
      "expires_at": "2026-10-13T00:00:00Z",
      "created_at": "2025-10-13T10:00:00Z",
      "last_used_at": "2025-10-13T12:00:00Z"
    }
  ],
  "total": 5
}
```

**Note:** Actual token values are never returned (only shown during creation).

---

#### GET `/api/v1/auth/tokens/:id`

Get service token details.

---

#### PUT `/api/v1/auth/tokens/:id`

Update service token (name, role, queues, expiration, enabled status).

**Request:**
```json
{
  "name": "payment-service-v2",
  "role": "rw",
  "allowed_queues": ["*"],
  "enabled": true,
  "expires_at": null
}
```

---

#### POST `/api/v1/auth/tokens/:id/rotate`

Rotate service token (generate new token, invalidate old one).

**Response:**
```json
{
  "id": "uuid",
  "name": "payment-service",
  "token": "qn_svc_NEW_TOKEN_HERE",
  "role": "rw",
  "allowed_queues": ["orders", "payments"],
  "expires_at": null,
  "created_at": "2025-10-13T10:00:00Z"
}
```

---

#### DELETE `/api/v1/auth/tokens/:id`

Delete (revoke) service token.

---

### Audit Log Endpoints (Admin Only)

#### GET `/api/v1/auth/audit`

Query audit logs.

**Query Parameters:**
- `user_id` - Filter by user
- `service_token_id` - Filter by service token
- `event_type` - Filter by event type
- `success` - Filter by success status
- `from` - Start date (ISO 8601)
- `to` - End date (ISO 8601)
- `limit` - Page size (default: 100)
- `offset` - Page offset (default: 0)

**Response:**
```json
{
  "logs": [
    {
      "id": "uuid",
      "event_type": "login",
      "user_id": "uuid",
      "username": "alice",
      "ip_address": "192.168.1.100",
      "user_agent": "Mozilla/5.0...",
      "endpoint": "/api/v1/auth/login",
      "success": true,
      "created_at": "2025-10-13T12:00:00Z"
    }
  ],
  "total": 150
}
```

---

## CLI Tool

### Installation

The CLI tool will be part of the Queen repository:

```bash
# Make CLI executable
chmod +x cli/auth.js

# Or use directly with node
node cli/auth.js <command>
```

### Commands

#### User Management

**Create User:**
```bash
node cli/auth.js user create \
  --username admin \
  --password secret123 \
  --role admin \
  --email admin@company.com

# Interactive mode (prompts for password)
node cli/auth.js user create \
  --username admin \
  --role admin \
  --interactive
```

**List Users:**
```bash
node cli/auth.js user list

# With filters
node cli/auth.js user list --role admin --enabled
```

**Update User:**
```bash
node cli/auth.js user update \
  --username alice \
  --email newemail@company.com \
  --role admin

# Enable/disable user
node cli/auth.js user disable --username bob
node cli/auth.js user enable --username bob
```

**Change Password:**
```bash
node cli/auth.js user password \
  --username alice \
  --new-password newsecret123

# Interactive mode (prompts for passwords)
node cli/auth.js user password --username alice --interactive
```

**Delete User:**
```bash
node cli/auth.js user delete --username bob
```

---

#### Service Token Management

**Create Token:**
```bash
# Basic token (all queues)
node cli/auth.js token create \
  --name payment-service \
  --role rw

# Token with queue restrictions
node cli/auth.js token create \
  --name analytics-worker \
  --role rw \
  --queues "orders,payments,events"

# Token with all queues (explicit)
node cli/auth.js token create \
  --name monitoring-service \
  --role read \
  --queues "*"

# Token with expiration
node cli/auth.js token create \
  --name temp-worker \
  --role rw \
  --expires-in 90d

# Token with operation restrictions
node cli/auth.js token create \
  --name push-only-service \
  --role rw \
  --queues "*" \
  --operations "push"
```

**Output:**
```
✅ Service token created successfully

Token Details:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
ID:              550e8400-e29b-41d4-a716-446655440000
Name:            payment-service
Role:            rw
Queues:          orders, payments
Operations:      all (based on role)
Expires:         never
Created:         2025-10-13T10:00:00Z

Token (⚠️ shown only once):
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
qn_svc_8K7jQm3pN2xR5wV9yB4cE6fH8jL1mN3qT5uW7xZ9a
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

⚠️  IMPORTANT: Store this token securely!
   - This token will NOT be shown again
   - It provides access to your Queen instance
   - Treat it like a password
```

**List Tokens:**
```bash
node cli/auth.js token list

# With filters
node cli/auth.js token list --role rw --enabled

# Show last usage
node cli/auth.js token list --show-usage
```

**Update Token:**
```bash
node cli/auth.js token update \
  --id <token-id> \
  --name payment-service-v2 \
  --queues "*"

# Enable/disable token
node cli/auth.js token disable --id <token-id>
node cli/auth.js token enable --id <token-id>
```

**Rotate Token:**
```bash
node cli/auth.js token rotate --id <token-id>

# Will output new token (old one is invalidated)
```

**Revoke Token:**
```bash
node cli/auth.js token revoke --id <token-id>

# With confirmation prompt
node cli/auth.js token revoke --id <token-id> --confirm
```

---

#### Audit Logs

**View Audit Logs:**
```bash
# Recent logs
node cli/auth.js audit logs --limit 50

# Filter by event type
node cli/auth.js audit logs --event-type login --limit 20

# Filter by user
node cli/auth.js audit logs --username alice

# Filter by date range
node cli/auth.js audit logs \
  --from 2025-10-01 \
  --to 2025-10-13

# Failed attempts only
node cli/auth.js audit logs --failed-only
```

---

#### System Setup

**Initialize Auth System:**
```bash
# Create auth tables and initial admin user
node cli/auth.js init

# Will prompt for admin username and password
```

**Check Auth Status:**
```bash
node cli/auth.js status

# Output:
# Authentication: enabled
# JWT Secret: configured ✅
# Admin Users: 2
# Service Tokens: 5 (3 active)
# Last Login: alice @ 2025-10-13T12:00:00Z
```

---

## Configuration

### Environment Variables

```bash
# ============================================
# Authentication Configuration
# ============================================

# Enable/disable authentication system
QUEEN_AUTH_ENABLED=false  # default: false (disabled)

# JWT Configuration
QUEEN_JWT_SECRET=<random_64_hex_chars>  # Required when auth is enabled
QUEEN_JWT_ACCESS_EXPIRY=900             # 15 minutes (in seconds)
QUEEN_JWT_REFRESH_EXPIRY=604800         # 7 days (in seconds)

# Password Policy
QUEEN_PASSWORD_MIN_LENGTH=8             # Minimum password length
QUEEN_PASSWORD_REQUIRE_UPPERCASE=true   # Require uppercase letter
QUEEN_PASSWORD_REQUIRE_LOWERCASE=true   # Require lowercase letter
QUEEN_PASSWORD_REQUIRE_NUMBER=true      # Require number
QUEEN_PASSWORD_REQUIRE_SPECIAL=false    # Require special character

# Bcrypt Configuration
QUEEN_BCRYPT_ROUNDS=12                  # bcrypt cost factor (10-14 recommended)

# Session Configuration
QUEEN_SESSION_MAX_REFRESH_TOKENS=5      # Max refresh tokens per user
QUEEN_SESSION_ABSOLUTE_TIMEOUT=2592000  # 30 days (in seconds)

# Audit Logging
QUEEN_AUDIT_ENABLED=true                # Enable audit logging
QUEEN_AUDIT_LOG_SUCCESS=true            # Log successful operations
QUEEN_AUDIT_LOG_FAILURES=true           # Log failed operations
QUEEN_AUDIT_RETENTION_DAYS=90           # Keep audit logs for 90 days

# Service Token Configuration
QUEEN_SERVICE_TOKEN_PREFIX=qn_svc_      # Token prefix
QUEEN_SERVICE_TOKEN_LENGTH=32           # Token length in bytes
QUEEN_SERVICE_TOKEN_DEFAULT_EXPIRY=0    # 0 = never expires

# Security Headers (when auth is enabled)
QUEEN_REQUIRE_HTTPS=false               # Enforce HTTPS (set to true in production)
QUEEN_HSTS_ENABLED=false                # Enable HSTS header
QUEEN_HSTS_MAX_AGE=31536000             # 1 year

# CORS Configuration (when auth is enabled)
QUEEN_AUTH_CORS_CREDENTIALS=true        # Allow credentials in CORS
```

### Generating Secrets

**JWT Secret:**
```bash
# Generate secure random secret (64 hex chars)
openssl rand -hex 32

# Or use Node.js
node -e "console.log(require('crypto').randomBytes(32).toString('hex'))"
```

**Initial Setup:**
```bash
# 1. Generate JWT secret
export QUEEN_JWT_SECRET=$(openssl rand -hex 32)

# 2. Enable auth
export QUEEN_AUTH_ENABLED=true

# 3. Start Queen
npm start

# 4. Initialize auth system (creates tables + admin user)
node cli/auth.js init
```

### Configuration File (Optional)

Alternative to environment variables: `config/auth.json`

```json
{
  "enabled": false,
  "jwt": {
    "secret": "env:QUEEN_JWT_SECRET",
    "accessExpiry": 900,
    "refreshExpiry": 604800
  },
  "password": {
    "minLength": 8,
    "requireUppercase": true,
    "requireLowercase": true,
    "requireNumber": true,
    "requireSpecial": false
  },
  "audit": {
    "enabled": true,
    "logSuccess": true,
    "logFailures": true,
    "retentionDays": 90
  }
}
```

---

## Security Considerations

### Best Practices Implemented

#### 1. Token Security

**Storage:**
- ✅ Service tokens stored as SHA-256 hashes in database
- ✅ Refresh tokens stored as SHA-256 hashes
- ✅ Access tokens are JWTs (stateless, not stored)
- ✅ Actual tokens never logged or returned after creation

**Transmission:**
- ✅ Tokens sent via `Authorization: Bearer <token>` header
- ✅ HTTPS enforced in production (`QUEEN_REQUIRE_HTTPS=true`)
- ✅ Tokens never sent in URL query parameters

**Expiration:**
- ✅ Access tokens: short-lived (15 minutes)
- ✅ Refresh tokens: medium-lived (7 days)
- ✅ Service tokens: long-lived or never expire (configurable)
- ✅ Automatic cleanup of expired tokens

**Revocation:**
- ✅ Refresh tokens can be revoked (logout, security breach)
- ✅ Service tokens can be disabled or deleted
- ✅ Audit trail for all token operations

#### 2. Password Security

**Hashing:**
- ✅ bcrypt with configurable cost factor (default: 12)
- ✅ Salts automatically managed by bcrypt
- ✅ Password hashes never exposed via API

**Password Policy:**
- ✅ Configurable minimum length (default: 8)
- ✅ Optional complexity requirements (uppercase, lowercase, numbers, special chars)
- ✅ Password validation before hashing

**Password Reset:**
- 🔄 Future: Email-based password reset flow
- 🔄 Future: Password reset tokens with short expiration

#### 3. Authentication Security

**Brute Force Protection:**
- 🔄 Future: Rate limiting on login endpoint (not in v1)
- 🔄 Future: Account lockout after N failed attempts
- ✅ Failed login attempts logged in audit log

**Session Management:**
- ✅ Refresh token rotation on each use
- ✅ Single refresh token active per session
- ✅ Absolute session timeout (30 days default)
- ✅ Manual logout revokes refresh token

**JWT Security:**
- ✅ HS256 algorithm (HMAC with SHA-256)
- ✅ Short expiration prevents replay attacks
- ✅ No sensitive data in JWT payload
- ✅ JWT secret stored in environment variable

#### 4. Authorization Security

**Permission Checks:**
- ✅ Every protected endpoint checks authentication
- ✅ Every protected endpoint checks authorization (role + permissions)
- ✅ Queue-level access control for service tokens
- ✅ Operation-level access control (optional)

**Principle of Least Privilege:**
- ✅ Three roles with clear permission boundaries
- ✅ Service tokens can be scoped to specific queues
- ✅ Service tokens can be scoped to specific operations
- ✅ Users/tokens can be disabled without deletion

#### 5. Audit Logging

**Comprehensive Logging:**
- ✅ All authentication events (login, logout, failures)
- ✅ All authorization failures (unauthorized access attempts)
- ✅ All user/token management operations
- ✅ IP address and user agent for all requests

**Log Contents:**
- ✅ Who performed the action (user or service token)
- ✅ What action was performed
- ✅ When it was performed
- ✅ Where it came from (IP, user agent)
- ✅ Result (success or failure)

**Log Protection:**
- ✅ Audit logs cannot be modified or deleted via API
- ✅ Configurable retention period
- ✅ Admin-only access to audit logs

#### 6. API Security

**HTTPS:**
- ✅ Enforced in production (`QUEEN_REQUIRE_HTTPS=true`)
- ✅ HSTS header support (`QUEEN_HSTS_ENABLED=true`)

**CORS:**
- ✅ Credentials allowed when auth enabled
- ✅ Configurable allowed origins
- ✅ Preflight request support

**Headers:**
- ✅ `X-Content-Type-Options: nosniff`
- ✅ `X-Frame-Options: DENY`
- ✅ `X-XSS-Protection: 1; mode=block`
- ✅ `Content-Security-Policy` for dashboard

#### 7. Database Security

**SQL Injection:**
- ✅ Parameterized queries for all database operations
- ✅ Input validation before database queries
- ✅ No string concatenation in SQL queries

**Data Protection:**
- ✅ Token hashes stored, not actual tokens
- ✅ Password hashes stored, not passwords
- ✅ Sensitive data encrypted at rest (via Queen's encryption feature)

**Access Control:**
- ✅ Database user with minimal required permissions
- ✅ Connection pooling with proper timeout handling
- ✅ Separate auth tables (can be isolated)

### Security Checklist

**Before Enabling Auth in Production:**

- [ ] Generate strong JWT secret (32+ bytes, random)
- [ ] Set `QUEEN_AUTH_ENABLED=true`
- [ ] Set `QUEEN_REQUIRE_HTTPS=true` (enforce HTTPS)
- [ ] Configure password policy
- [ ] Create initial admin user via CLI
- [ ] Generate service tokens for all microservices
- [ ] Update all clients to use tokens
- [ ] Enable audit logging
- [ ] Configure audit log retention
- [ ] Test authentication flows in staging
- [ ] Review audit logs regularly
- [ ] Document token management procedures
- [ ] Set up token rotation schedule
- [ ] Configure alerts for unauthorized access attempts

---

## Implementation Roadmap

### Phase 1: Foundation (Week 1)

**Database Schema:**
- [ ] Create `queen.auth_users` table
- [ ] Create `queen.auth_service_tokens` table
- [ ] Create `queen.auth_refresh_tokens` table
- [ ] Create `queen.auth_audit_log` table
- [ ] Create database migration script
- [ ] Add indexes for performance

**Core Auth Library:**
- [ ] Implement bcrypt password hashing
- [ ] Implement JWT signing and verification
- [ ] Implement service token generation and validation
- [ ] Implement SHA-256 hashing for tokens
- [ ] Create token format validators
- [ ] Create password policy validator

### Phase 2: Middleware & Permissions (Week 2)

**Authentication Middleware:**
- [ ] Create auth middleware (token extraction and validation)
- [ ] Implement service token authentication
- [ ] Implement JWT authentication
- [ ] Handle auth disabled state (bypass middleware)
- [ ] Attach auth context to request

**Authorization Middleware:**
- [ ] Define permission matrix
- [ ] Create permission checking function
- [ ] Create role-based authorization middleware
- [ ] Implement queue-level access control
- [ ] Implement operation-level access control

**Audit Logging:**
- [ ] Create audit logging functions
- [ ] Log authentication events
- [ ] Log authorization failures
- [ ] Log user/token management operations
- [ ] Implement audit log cleanup job

### Phase 3: API Endpoints (Week 3)

**Auth Endpoints:**
- [ ] POST `/api/v1/auth/login` - User login
- [ ] POST `/api/v1/auth/refresh` - Refresh access token
- [ ] POST `/api/v1/auth/logout` - Logout and revoke refresh token
- [ ] GET `/api/v1/auth/me` - Get current user info

**User Management Endpoints:**
- [ ] POST `/api/v1/auth/users` - Create user
- [ ] GET `/api/v1/auth/users` - List users
- [ ] GET `/api/v1/auth/users/:id` - Get user
- [ ] PUT `/api/v1/auth/users/:id` - Update user
- [ ] PUT `/api/v1/auth/users/:id/password` - Change password
- [ ] DELETE `/api/v1/auth/users/:id` - Delete user

**Service Token Endpoints:**
- [ ] POST `/api/v1/auth/tokens` - Create service token
- [ ] GET `/api/v1/auth/tokens` - List tokens
- [ ] GET `/api/v1/auth/tokens/:id` - Get token details
- [ ] PUT `/api/v1/auth/tokens/:id` - Update token
- [ ] POST `/api/v1/auth/tokens/:id/rotate` - Rotate token
- [ ] DELETE `/api/v1/auth/tokens/:id` - Revoke token

**Audit Endpoints:**
- [ ] GET `/api/v1/auth/audit` - Query audit logs

### Phase 4: CLI Tool (Week 4)

**User Management Commands:**
- [ ] `user create` - Create user
- [ ] `user list` - List users
- [ ] `user update` - Update user
- [ ] `user password` - Change password
- [ ] `user enable/disable` - Enable/disable user
- [ ] `user delete` - Delete user

**Token Management Commands:**
- [ ] `token create` - Create service token
- [ ] `token list` - List tokens
- [ ] `token update` - Update token
- [ ] `token rotate` - Rotate token
- [ ] `token enable/disable` - Enable/disable token
- [ ] `token revoke` - Revoke token

**System Commands:**
- [ ] `init` - Initialize auth system
- [ ] `status` - Check auth system status
- [ ] `audit logs` - View audit logs

### Phase 5: Integration (Week 5)

**Protected Endpoints:**
- [ ] Add auth middleware to all protected routes
- [ ] Update route handlers to check permissions
- [ ] Test with auth enabled and disabled
- [ ] Update WebSocket server for auth

**Client SDK Update:**
- [ ] Add token support to Queen client
- [ ] Add authentication error handling
- [ ] Update examples to use tokens
- [ ] Update documentation

**Dashboard Integration:**
- [ ] Create login page
- [ ] Implement token storage (httpOnly cookies)
- [ ] Add auth guards to routes
- [ ] Add logout functionality
- [ ] Add WebSocket auth
- [ ] Handle token refresh

### Phase 6: Testing & Documentation (Week 6)

**Testing:**
- [ ] Unit tests for auth library
- [ ] Unit tests for middleware
- [ ] Integration tests for auth endpoints
- [ ] Integration tests for protected endpoints
- [ ] Security testing (token validation, permissions)
- [ ] Performance testing (auth overhead)

**Documentation:**
- [ ] Complete AUTH.md (this document)
- [ ] Update README.md with auth section
- [ ] Create migration guide
- [ ] Create deployment guide
- [ ] Update API.md with auth requirements
- [ ] Create security best practices guide

**Configuration:**
- [ ] Add auth config to `src/config.js`
- [ ] Add auth environment variables
- [ ] Update Docker Compose example
- [ ] Update Kubernetes manifests (if applicable)

### Phase 7: Polish & Release (Week 7)

**Final Touches:**
- [ ] Code review and refactoring
- [ ] Security audit
- [ ] Performance optimization
- [ ] Error message improvements
- [ ] CLI UX improvements

**Release:**
- [ ] Create release notes
- [ ] Update CHANGELOG.md
- [ ] Tag release (e.g., v2.0.0)
- [ ] Publish to npm (if applicable)
- [ ] Announce on GitHub

---

## Future Enhancements

### Planned but Not in v1

#### 1. OAuth2 / OpenID Connect (OIDC)

**Use Case:** Enterprise SSO integration (Okta, Auth0, Azure AD, Google)

**Design:**
- Add OAuth2 provider configuration
- Implement authorization code flow
- Map OIDC claims to Queen roles
- Support multiple identity providers
- Maintain service token system alongside OAuth2

**Tables:**
```sql
CREATE TABLE queen.auth_oauth_providers (
    id UUID PRIMARY KEY,
    name VARCHAR(255) UNIQUE NOT NULL,
    issuer VARCHAR(255) NOT NULL,
    client_id VARCHAR(255) NOT NULL,
    client_secret TEXT NOT NULL,
    authorization_endpoint TEXT NOT NULL,
    token_endpoint TEXT NOT NULL,
    userinfo_endpoint TEXT NOT NULL,
    enabled BOOLEAN DEFAULT true
);

CREATE TABLE queen.auth_oauth_tokens (
    id UUID PRIMARY KEY,
    user_id UUID REFERENCES queen.auth_users(id),
    provider_id UUID REFERENCES queen.auth_oauth_providers(id),
    access_token_hash TEXT NOT NULL,
    refresh_token_hash TEXT,
    expires_at TIMESTAMPTZ,
    scope TEXT
);
```

**Implementation Effort:** ~2 weeks

---

#### 2. Rate Limiting

**Use Case:** Prevent abuse, DDoS protection, API quota management

**Design:**
- Token bucket algorithm
- Per-user and per-token rate limits
- Configurable limits per endpoint
- Redis-backed for multi-server support
- Sliding window counters

**Configuration:**
```bash
QUEEN_RATE_LIMIT_ENABLED=true
QUEEN_RATE_LIMIT_WINDOW=60000  # 1 minute
QUEEN_RATE_LIMIT_MAX_REQUESTS=100
QUEEN_RATE_LIMIT_REDIS_URL=redis://localhost:6379
```

**Tables:**
```sql
CREATE TABLE queen.auth_rate_limits (
    id UUID PRIMARY KEY,
    user_id UUID REFERENCES queen.auth_users(id),
    service_token_id UUID REFERENCES queen.auth_service_tokens(id),
    endpoint VARCHAR(255),
    max_requests INTEGER NOT NULL,
    window_seconds INTEGER NOT NULL
);
```

**Response Headers:**
```
X-RateLimit-Limit: 100
X-RateLimit-Remaining: 87
X-RateLimit-Reset: 1697194500
```

**Implementation Effort:** ~1 week

---

#### 3. Multi-Tenancy

**Use Case:** SaaS deployment, isolated customer environments

**Design:**
- Tenant isolation at database level (RLS)
- Tenant-specific queues and messages
- Tenant-specific users and tokens
- Cross-tenant queries forbidden
- Tenant ID in all auth tokens

**Tables:**
```sql
CREATE TABLE queen.tenants (
    id UUID PRIMARY KEY,
    name VARCHAR(255) UNIQUE NOT NULL,
    subdomain VARCHAR(255) UNIQUE,
    enabled BOOLEAN DEFAULT true,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Add tenant_id to all tables
ALTER TABLE queen.auth_users ADD COLUMN tenant_id UUID REFERENCES queen.tenants(id);
ALTER TABLE queen.queues ADD COLUMN tenant_id UUID REFERENCES queen.tenants(id);
ALTER TABLE queen.partitions ADD COLUMN tenant_id UUID REFERENCES queen.tenants(id);
ALTER TABLE queen.messages ADD COLUMN tenant_id UUID REFERENCES queen.tenants(id);

-- Row Level Security
ALTER TABLE queen.auth_users ENABLE ROW LEVEL SECURITY;
CREATE POLICY tenant_isolation ON queen.auth_users
    USING (tenant_id = current_setting('app.tenant_id')::uuid);
```

**Token Format:**
```
qn_svc_<tenant_id>_<token>
```

**Implementation Effort:** ~3 weeks (significant refactoring required)

---

#### 4. Advanced Audit Features

**Enhancements:**
- Real-time audit log streaming (WebSocket)
- Audit log export (CSV, JSON)
- Audit log search with Elasticsearch
- Compliance reports (SOC 2, HIPAA)
- Alert rules for suspicious activity

**Implementation Effort:** ~1-2 weeks

---

#### 5. API Key Management UI

**Features:**
- Dashboard page for managing service tokens
- Token usage analytics
- Token expiration alerts
- Token rotation wizard
- QR codes for token sharing (mobile apps)

**Implementation Effort:** ~1 week

---

#### 6. Advanced Permissions

**Enhancements:**
- Custom roles (beyond admin/rw/read)
- Granular permissions per queue
- Partition-level access control
- Namespace/task-level access control
- Time-based access (tokens valid only during business hours)

**Example:**
```json
{
  "name": "analytics-service",
  "role": "custom",
  "permissions": {
    "orders": ["pop", "ack"],
    "payments": ["pop", "ack"],
    "events": ["push"],
    "analytics/*": ["push", "pop", "ack"]
  },
  "schedule": {
    "timezone": "UTC",
    "allowed_hours": "09:00-17:00",
    "allowed_days": ["Mon", "Tue", "Wed", "Thu", "Fri"]
  }
}
```

**Implementation Effort:** ~2 weeks

---

#### 7. Two-Factor Authentication (2FA)

**Use Case:** Enhanced security for dashboard users

**Design:**
- TOTP-based 2FA (Google Authenticator, Authy)
- Backup codes for recovery
- 2FA enforcement per user or globally
- Remember device option

**Tables:**
```sql
CREATE TABLE queen.auth_2fa (
    id UUID PRIMARY KEY,
    user_id UUID REFERENCES queen.auth_users(id),
    secret TEXT NOT NULL,
    enabled BOOLEAN DEFAULT false,
    backup_codes JSONB,
    created_at TIMESTAMPTZ DEFAULT NOW()
);
```

**Implementation Effort:** ~1 week

---

#### 8. Webhook Notifications

**Use Case:** Security alerts, audit notifications

**Design:**
- Configurable webhooks for auth events
- Failed login attempts → Slack/PagerDuty
- New token created → Email notification
- Suspicious activity → Security team alert

**Tables:**
```sql
CREATE TABLE queen.auth_webhooks (
    id UUID PRIMARY KEY,
    name VARCHAR(255) NOT NULL,
    url TEXT NOT NULL,
    events JSONB NOT NULL,  -- ["login.failed", "token.created"]
    enabled BOOLEAN DEFAULT true,
    secret TEXT  -- HMAC signature
);
```

**Implementation Effort:** ~3 days

---

## Appendices

### Appendix A: Token Format Specification

**Service Token Format:**
```
qn_svc_<base58_encoded_32_bytes>

Prefix:     qn_svc_
Encoding:   Base58 (Bitcoin alphabet)
Length:     32 bytes (44 characters in base58)
Total:      51 characters

Example:
qn_svc_8K7jQm3pN2xR5wV9yB4cE6fH8jL1mN3qT5uW7xZ9a
```

**Why Base58?**
- No ambiguous characters (0/O, I/l)
- URL-safe without encoding
- Human-readable
- Copy-paste friendly

**JWT Format:**
```
header.payload.signature

Header:
{
  "alg": "HS256",
  "typ": "JWT"
}

Payload (Access Token):
{
  "sub": "user-uuid",
  "username": "alice",
  "role": "admin",
  "type": "access",
  "iat": 1697193600,
  "exp": 1697194500
}

Payload (Refresh Token):
{
  "sub": "user-uuid",
  "type": "refresh",
  "jti": "refresh-token-uuid",
  "iat": 1697193600,
  "exp": 1697798400
}
```

---

### Appendix B: Error Codes

**Authentication Errors:**

| Code | Message | HTTP Status | Description |
|------|---------|-------------|-------------|
| `AUTH_DISABLED` | Authentication is not enabled | 501 | Auth system is disabled |
| `MISSING_TOKEN` | No authentication token provided | 401 | Authorization header missing |
| `INVALID_TOKEN` | Invalid authentication token | 401 | Token format invalid |
| `EXPIRED_TOKEN` | Authentication token expired | 401 | JWT expired |
| `REVOKED_TOKEN` | Token has been revoked | 401 | Refresh token revoked |
| `INVALID_CREDENTIALS` | Invalid username or password | 401 | Login failed |
| `ACCOUNT_DISABLED` | User account is disabled | 403 | Account not enabled |
| `TOKEN_DISABLED` | Service token is disabled | 403 | Token not enabled |

**Authorization Errors:**

| Code | Message | HTTP Status | Description |
|------|---------|-------------|-------------|
| `INSUFFICIENT_PERMISSIONS` | Insufficient permissions | 403 | Role lacks required permission |
| `QUEUE_ACCESS_DENIED` | Access denied to this queue | 403 | Queue not in allowed_queues |
| `OPERATION_NOT_ALLOWED` | Operation not allowed | 403 | Operation not in allowed_operations |

**Example Error Response:**
```json
{
  "error": {
    "code": "INVALID_TOKEN",
    "message": "Invalid authentication token",
    "details": "Token signature verification failed"
  }
}
```

---

### Appendix C: Migration Guide

**Migrating from No-Auth to Auth-Enabled:**

1. **Preparation (No Downtime):**
   ```bash
   # 1. Generate JWT secret
   export QUEEN_JWT_SECRET=$(openssl rand -hex 32)
   
   # 2. Deploy Queen with auth disabled (default)
   # Auth tables will be created but not used
   npm start
   
   # 3. Initialize auth system
   node cli/auth.js init
   # Creates admin user interactively
   
   # 4. Generate service tokens for all microservices
   node cli/auth.js token create --name service-1 --role rw
   node cli/auth.js token create --name service-2 --role rw
   # ... etc
   ```

2. **Testing (Staging Environment):**
   ```bash
   # 1. Enable auth in staging
   export QUEEN_AUTH_ENABLED=true
   npm start
   
   # 2. Update staging clients to use tokens
   # 3. Test all workflows
   # 4. Monitor audit logs
   ```

3. **Production Deployment (Staged Rollout):**
   ```bash
   # Week 1: Dashboard only
   # - Enable auth for dashboard
   # - API remains open (QUEEN_AUTH_ENABLED=false)
   # - Users get familiar with login
   
   # Week 2: Read-only services
   # - Enable auth (QUEEN_AUTH_ENABLED=true)
   # - Deploy tokens to monitoring services
   # - API still accepts unauthenticated requests (grace period)
   
   # Week 3: All services
   # - Deploy tokens to all microservices
   # - Monitor errors
   # - Provide support for issues
   
   # Week 4: Enforce
   # - Remove grace period
   # - All requests require authentication
   # - Monitor audit logs for unauthorized attempts
   ```

4. **Rollback Plan:**
   ```bash
   # If issues arise, disable auth immediately
   export QUEEN_AUTH_ENABLED=false
   # Restart Queen servers
   # System returns to no-auth mode
   # No data loss, no breaking changes
   ```

---

### Appendix D: Performance Considerations

**Authentication Overhead:**

| Operation | No Auth | With Auth | Overhead |
|-----------|---------|-----------|----------|
| Service Token Validation | N/A | ~2-3ms | SHA-256 hash + DB lookup |
| JWT Validation | N/A | ~0.5-1ms | Signature verification (no DB) |
| Permission Check | N/A | ~0.1ms | In-memory check |
| **Total per request** | 0ms | **~1-4ms** | Minimal |

**Optimizations:**

1. **Token Cache:** Cache validated service tokens in memory (5 min TTL)
   - First request: ~3ms (DB lookup)
   - Subsequent requests: ~0.1ms (cache hit)

2. **JWT Stateless:** JWT access tokens require no DB lookup
   - Signature verification only
   - ~0.5ms per request

3. **Permission Matrix:** Pre-computed in memory
   - No DB queries for permission checks
   - ~0.1ms lookup time

4. **Batch Operations:** Auth overhead is per-request, not per-message
   - Pushing 1000 messages: ~3ms auth + ~100ms processing
   - Overhead: ~3% of total time

**Database Impact:**

- Auth tables are small and heavily indexed
- Token lookups use hash index (O(1))
- Audit logs written asynchronously
- No impact on message queue performance

**Recommended Settings:**

```bash
# For high-traffic production
QUEEN_JWT_ACCESS_EXPIRY=900  # 15 min (balance security vs cache hit rate)
QUEEN_AUTH_CACHE_ENABLED=true  # Enable token caching
QUEEN_AUTH_CACHE_TTL=300  # 5 min cache TTL
```

---

### Appendix E: Compliance & Standards

**Standards Followed:**

- ✅ **OWASP Top 10:** Protection against common vulnerabilities
- ✅ **NIST 800-63B:** Digital identity guidelines (password requirements)
- ✅ **JWT RFC 7519:** JSON Web Token standard
- ✅ **OAuth 2.0 RFC 6749:** (future: OAuth2 support)
- ✅ **OpenID Connect:** (future: OIDC support)

**Compliance Support:**

| Requirement | Supported | How |
|-------------|-----------|-----|
| **SOC 2** | ✅ | Audit logs, access control, encryption |
| **HIPAA** | ✅ | Encryption at rest/transit, audit trails, access control |
| **GDPR** | ✅ | User data export, right to deletion, audit logs |
| **PCI DSS** | ⚠️ | Partial (use separate DB for card data) |

---

## Conclusion

This authentication system is designed to be:

- **Production-ready** from day one
- **Secure** by following industry standards
- **Flexible** for various deployment scenarios
- **Performant** with minimal overhead
- **Developer-friendly** with CLI tools and clear documentation

The phased implementation approach (7 weeks) allows for:
- Incremental development and testing
- Early feedback and course correction
- Low-risk deployment with rollback options
- Zero impact on existing Queen installations

**Next Steps:**

1. Review this plan and gather feedback
2. Prioritize features (if needed)
3. Begin Phase 1 implementation
4. Set up staging environment for testing
5. Develop migration guide for existing users

---

**Document Version:** 1.0  
**Last Updated:** October 13, 2025  
**Status:** Planning Phase - Ready for Implementation  
**Estimated Implementation Time:** 7 weeks (one developer)

