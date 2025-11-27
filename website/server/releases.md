# Release History

This page documents Queen MQ server releases and their compatible client versions.

## Version Compatibility Matrix

| Server Version | Description | Compatible Clients |
|----------------|-------------|-------------------|
| **0.8.0** | Added Shared Cache with UDP sync for clustered deployment | JS ≥0.7.4, Python ≥0.7.4 |
| **0.7.5** | First stable release | JS ≥0.7.4, Python ≥0.7.4 |

## Release Details

### Version 0.8.0

**Highlights:**
- Added Shared Cache with UDP sync for clustered deployment
- Enables multi-server deployments to share state efficiently
- Reduces database queries by 80-90% in clustered environments

**Compatible Clients:**
- JavaScript Client: `queen-mq` ≥0.7.4
- Python Client: `queen-mq` ≥0.7.4

---

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

## Upgrading

When upgrading the Queen MQ server, ensure your client libraries are at or above the minimum compatible version listed in the table above.

:::tip Backward Compatibility
Client versions are generally backward compatible with newer server versions. Always check this table when upgrading the server to ensure your clients remain compatible.
:::

