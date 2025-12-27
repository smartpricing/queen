# Failover & Recovery

Automatic failover to disk when PostgreSQL is unavailable - zero message loss guaranteed.

## How It Works

```
Normal Operation:
Client → Queen Server → PostgreSQL ✅

PostgreSQL Down:
Client → Queen Server → File Buffer ✅
                     ↓ (retry in background)
                     → PostgreSQL (when recovered)
```

## Zero Message Loss

Queen automatically:
1. Detects PostgreSQL unavailability
2. Buffers messages to disk
3. Returns success to client
4. Replays messages when database recovers

**No configuration needed** - failover is automatic!

## File Buffer Location

**Linux:** `/var/lib/queen/buffers/`  
**macOS:** `/tmp/queen/`

Custom location:
```bash
FILE_BUFFER_DIR=/custom/path ./bin/queen-server
```

## Features

- ✅ Zero message loss
- ✅ FIFO ordering preserved
- ✅ Automatic recovery
- ✅ Survives server crashes
- ✅ No client changes needed

## Monitoring

```bash
# Check buffer files
ls -lh /var/lib/queen/buffers/

# Count buffered events
wc -l /var/lib/queen/buffers/failover_*.buf
```

## Recovery Performance

- **Recovery rate**: ~10,000 events/second
- **Example**: 1M buffered events = ~100 seconds recovery

## Best Practices

1. **Provision disk space** for peak traffic (2-4 hours)
2. **Monitor buffer directory** for file buildup
3. **Alert on database outages** proactively
4. **Test failover** regularly

## Related

- [Server Architecture](/server/architecture)

[Complete failover documentation](https://github.com/smartpricing/queen/blob/master/documentation/FAILOVER.md)
