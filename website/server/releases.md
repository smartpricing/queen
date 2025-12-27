# Release History

This page documents Queen MQ server releases and their compatible client versions.

## Version Compatibility Matrix

| Server Version | Description | Compatible Clients |
|----------------|-------------|-------------------|
| **0.12.0** | New frontend and docs | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use proxy auth |
| **0.11.0** | Libqueen 0.11.0; added stats tables and optimized analytics procedures, added DB statement timeout and stats reconcile interval | JS ≥0.7.4, Python ≥0.7.4 |
| **0.10.0** | Total rewrite of the engine with libuv and stored procedures, removed streaming engine | JS ≥0.7.4, Python ≥0.7.4 |
| **0.8.0** | Added Shared Cache with UDP sync for clustered deployment | JS ≥0.7.4, Python ≥0.7.4 |
| **0.7.5** | First stable release | JS ≥0.7.4, Python ≥0.7.4 |

## Release Details


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