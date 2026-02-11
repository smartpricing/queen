# Release History

This page documents Queen MQ server releases and their compatible client versions.

## Version Compatibility Matrix

| Server Version | Description | Compatible Clients |
|----------------|-------------|-------------------|
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

- Server 0.12.6: Improved slow cg discovery when there are tons of partitions
- Server 0.12.5: Fixed cg lag calculation for "new" cg at first message
- Server 0.12.4: Fixed window buffer debounce behavior
- Clients 0.12.1: Fixed bug in transaction with consumer groups

## Release Details

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