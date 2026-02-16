# Release History

This page documents Queen MQ server releases and their compatible client versions.

## Version Compatibility Matrix

| Server Version | Description | Compatible Clients |
|----------------|-------------|-------------------|
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