# Release History

This page documents Queen MQ server releases and their compatible client versions.

## Version Compatibility Matrix

| Server Version | Description | Compatible Clients |
|----------------|-------------|-------------------|
| **0.12.12** | Built-in database migration (pg_dump \| pg_restore, no temp file, selective table groups, row count validation) | JS ≥0.7.4, Python ≥0.7.4 |
| **0.12.10** | Fixed JWKS fetch over HTTPS (cpp-httplib TLS support) | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use |
| **0.12.9** | Fixed server crash (SIGSEGV) on lease renewal, added EdDSA/JWKS auth, fixed examples | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use |
| **0.12.8** | Added single partition move to now to frontend | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use |
| **0.12.7** | Optimized cg metadata creation for new consumer groups | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use |
| **0.12.6** | Improved slow cg discovery when there are tons of partitions | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use |
| **0.12.5** | Fixed cg lag calculation for "new" cg at first message | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use |
| **0.12.4** | Fixed window buffer debounce behavior | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use proxy auth |
| **0.12.3** | Added JWT authentication | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use proxy auth |
| **0.12.x** | New frontend and docs | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use proxy auth |
| **0.11.x** | Libqueen 0.11.0; added stats tables and optimized analytics procedures, added DB statement timeout and stats reconcile interval | JS ≥0.7.4, Python ≥0.7.4 |
| **0.10.x** | Total rewrite of the engine with libuv and stored procedures, removed streaming engine | JS ≥0.7.4, Python ≥0.7.4 |
| **0.8.x** | Added Shared Cache with UDP sync for clustered deployment | JS ≥0.7.4, Python ≥0.7.4 |
| **0.7.5** | First stable release | JS ≥0.7.4, Python ≥0.7.4 |

## Bug fixing and improvements 

- Server 0.12.12: Added built-in database migration (pg_dump | pg_restore stream, no temp file, selective table groups, row count validation, PG 18 client in Docker image)
- Clients 0.12.2: Added custom `headers` option to JS, Python, and Go clients for API gateway authentication
- Server 0.12.10: Fixed JWKS fetch over HTTPS (added CPPHTTPLIB_OPENSSL_SUPPORT to enable TLS in cpp-httplib)
- Server 0.12.9: Fixed server crash (SIGSEGV) on lease renewal caused by use-after-free of HttpRequest pointer
- Server 0.12.9: Added native EdDSA and JWKS JWT authentication (auto-discovery via JWT_JWKS_URL)
- Server 0.12.9: Fixed quickstart consumer example (autoAck + onError silent conflict)
- Server 0.12.9: Fixed examples 03-transactional-pipeline.js and 08-consumer-groups.js (missing .each())
- Server 0.12.8: Added single partition move to now to frontend
- Server 0.12.7: Optimized cg metadata creation for new consumer groups
- Server 0.12.6: Improved slow cg discovery when there are tons of partitions
- Server 0.12.5: Fixed cg lag calculation for "new" cg at first message
- Server 0.12.4: Fixed window buffer debounce behavior
- Clients 0.12.1: Fixed bug in transaction with consumer groups

## Release Details

### Version 0.12.12
**Highlights:**
- **Built-in database migration:** Queen now includes a full PostgreSQL-to-PostgreSQL migration tool accessible from the web dashboard. No external scripts or sidecars needed. The migration streams `pg_dump` directly into `pg_restore` via a kernel pipe — no temp file is written regardless of database size. Supports selective table group migration (skip messages, traces, history, or metrics independently), row count validation against the target, and safe retry on failure.
- **PostgreSQL 18 client in Docker image:** The runtime image now installs `postgresql-client-18` from the official PGDG apt repository, enabling migration to/from PostgreSQL 18 servers without version mismatch errors.
- **Migration API:** New endpoints at `/api/v1/migration/` — `test-connection`, `start`, `status`, `validate`, `reset`.

**Compatible Clients:**
- JavaScript Client: `queen-mq` >=0.7.4
- Python Client: `queen-mq` >=0.7.4

### Clients 0.12.2
**Highlights:**
- **Custom request headers:** All three client libraries (JS, Python, Go) now accept a `headers` option in the constructor. This allows passing arbitrary HTTP headers (e.g., `x-api-key`, custom `Authorization`) that are included in every request to the Queen server. This is essential for corporate environments where services are hosted behind an API gateway that requires its own authentication headers.

**Usage:**

JavaScript:
```javascript
const queen = new Queen({
  url: 'https://queen.example.com',
  headers: {
    'x-api-key': 'some-key',
    'Authorization': 'Bearer my-JWT-Key'
  }
})
```

Python:
```python
queen = Queen(
    url='https://queen.example.com',
    headers={
        'x-api-key': 'some-key',
        'Authorization': 'Bearer my-JWT-Key'
    }
)
```

Go:
```go
client, _ := queen.New(queen.ClientConfig{
    URL: "https://queen.example.com",
    Headers: map[string]string{
        "x-api-key":    "some-key",
        "Authorization": "Bearer my-JWT-Key",
    },
})
```

Custom headers are merged after `bearerToken`, so explicit `headers.Authorization` overrides `bearerToken` if both are set.

### Version 0.12.10
**Highlights:**
- **Fixed JWKS fetch over HTTPS:** The JWKS endpoint fetcher used `cpp-httplib` which was compiled without TLS support, causing `'https' scheme is not supported` errors when `JWT_JWKS_URL` pointed to an HTTPS endpoint. Added `CPPHTTPLIB_OPENSSL_SUPPORT` compile flag to enable HTTPS. HTTP endpoints were unaffected.

**Compatible Clients:**
- JavaScript Client: `queen-mq` >=0.7.4
- Python Client: `queen-mq` >=0.7.4

### Version 0.12.9
**Highlights:**
- **Fixed server crash (SIGSEGV):** The lease renewal endpoint (`/api/v1/lease/:leaseId/extend`) captured the uWebSockets `HttpRequest*` pointer inside an async body callback, causing a use-after-free when the body arrived. This crash was triggered by clients using `renewLease()` with any interval. Fixed by extracting URL parameters synchronously before the async body read.
- **Native EdDSA and JWKS authentication:** Added support for EdDSA (Ed25519) JWT tokens and automatic JWKS key discovery. Configure with `JWT_ENABLED=true`, `JWT_ALGORITHM=auto` (or `EdDSA` for strict mode), and `JWT_JWKS_URL` pointing to your JWKS endpoint. This replaces the experimental `-exp-jwks` build and is now part of the main release.
- **Fixed documentation and examples:** Fixed the quickstart Step 4 consumer example where `.autoAck(true)` combined with `.onError()` silently disabled auto-acknowledgment. Fixed `03-transactional-pipeline.js` and `08-consumer-groups.js` examples that were missing `.each()` before `.consume()`, causing handlers to receive arrays instead of single messages.

**Compatible Clients:**
- JavaScript Client: `queen-mq` >=0.7.4
- Python Client: `queen-mq` >=0.7.4

### Version 0.12.0
**Highlights:**
- New frontend and docs, new stats and fixed some issues

### Version 0.11.0
**Highlights:**
- Libqueen 0.11.0; added stats tables and optimized analytics procedures, added DB statement timeout and stats reconcile interval

**Compatible Clients:**
- JavaScript Client: `queen-mq` ≥0.7.4
- Python Client: `queen-mq` ≥0.7.4

### Version 0.10.0

**Highlights:**
- Total rewrite of the engine with libuv and stored procedures, removed streaming engine

**Compatible Clients:**
- JavaScript Client: `queen-mq` ≥0.7.4
- Python Client: `queen-mq` ≥0.7.4

### Version 0.8.0

**Highlights:**
- Added Shared Cache with UDP sync for clustered deployment
- Enables multi-server deployments to share state efficiently
- Reduces database queries by 80-90% in clustered environments

**Compatible Clients:**
- JavaScript Client: `queen-mq` ≥0.7.4
- Python Client: `queen-mq` ≥0.7.4

### Version 0.7.5

**Highlights:**
- First stable release of Queen MQ
- Full feature set including partitions, consumer groups, transactions, and streaming
- Production-ready with comprehensive testing

**Compatible Clients:**
- JavaScript Client: `queen-mq` ≥0.7.4
- Python Client: `queen-mq` ≥0.7.4

---

## Client Installation

Install the latest compatible clients:

**JavaScript:**
```bash
npm install queen-mq
```

**Python:**
```bash
pip install queen-mq
```