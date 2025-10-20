# Database Pool Health Tests

This directory contains comprehensive tests and monitoring tools for the Queen server database connection pool.

## Overview

The connection pool is critical for server stability. These tests ensure:
- Connections have proper timeout settings
- Pool size is maintained when connections fail
- Concurrent access is handled correctly
- Invalid connections are replaced automatically
- Pool recovery from database failures

## Test Suite

### 1. Unit Tests (`pool_health_test.cpp`)

Comprehensive C++ test suite that validates pool behavior.

**Build and run:**
```bash
cd /Users/alice/Work/queen/server/tests
make test
```

**Tests included:**
- ✅ Connection string includes timeout parameters
- ✅ Pool maintains size when connections become invalid
- ✅ Concurrent access to pool (20 threads × 10 ops)
- ✅ Pool recovery from database failure
- ✅ Connection validity checks

**Configuration:**
Set these environment variables to test against your database:
```bash
export PG_HOST=localhost
export PG_PORT=5432
export PG_DB=postgres
export PG_USER=postgres
export PG_PASSWORD=postgres
```

If not set, some tests will be skipped (which is fine for CI/CD without a database).

### 2. Log Monitor (`monitor_pool_health.sh`)

Real-time monitoring script that analyzes Queen server logs for pool health issues.

**Usage:**

Monitor live logs:
```bash
tail -f /var/log/queen/server.log | ./monitor_pool_health.sh
```

Monitor for specific duration (30 seconds):
```bash
./monitor_pool_health.sh /var/log/queen/server.log 30
```

Analyze past logs:
```bash
./monitor_pool_health.sh /var/log/queen/server.log
```

**What it detects:**
- 🚨 Pool timeout errors (CRITICAL)
- 💔 Health check failures (CRITICAL)
- ⚠️  Invalid connections returned to pool
- 📉 Pool size shrinkage
- 🔄 Connection replacement success/failure
- 📊 Pool utilization stats

**Exit codes:**
- `0` - Healthy
- `1` - Critical issues (timeouts or health check failures)
- `2` - Warnings (multiple invalid connections)

## Integration with CI/CD

### GitHub Actions / GitLab CI

```yaml
test-pool-health:
  runs-on: ubuntu-latest
  services:
    postgres:
      image: postgres:16
      env:
        POSTGRES_PASSWORD: testpass
      options: >-
        --health-cmd pg_isready
        --health-interval 10s
        --health-timeout 5s
        --health-retries 5
  steps:
    - uses: actions/checkout@v3
    - name: Build tests
      run: cd server/tests && make
    - name: Run pool health tests
      env:
        PG_HOST: postgres
        PG_PORT: 5432
        PG_DB: postgres
        PG_USER: postgres
        PG_PASSWORD: testpass
      run: cd server/tests && make test
```

## Production Monitoring

### Set up continuous monitoring

**Option 1: Cron job**
```bash
# Check pool health every 5 minutes
*/5 * * * * tail -n 1000 /var/log/queen/server.log | /opt/queen/tests/monitor_pool_health.sh || echo "Pool health issue detected!"
```

**Option 2: Systemd service**
```ini
[Unit]
Description=Queen Pool Health Monitor
After=queen-server.service

[Service]
Type=simple
ExecStart=/bin/bash -c 'journalctl -u queen-server -f | /opt/queen/tests/monitor_pool_health.sh'
Restart=always

[Install]
WantedBy=multi-user.target
```

**Option 3: Docker / K8s sidecar**
```yaml
apiVersion: v1
kind: Pod
metadata:
  name: queen-server
spec:
  containers:
  - name: server
    image: queen-server:latest
    # ... main container config ...
  
  - name: pool-monitor
    image: queen-server:latest
    command: ["/bin/bash", "-c"]
    args:
      - tail -f /var/log/queen/server.log | /app/tests/monitor_pool_health.sh
    volumeMounts:
    - name: logs
      mountPath: /var/log/queen
  volumes:
  - name: logs
    emptyDir: {}
```

## Troubleshooting

### Issue: Pool exhaustion detected

**Symptoms:**
```
🚨 POOL EXHAUSTED! 0/5 available, 5 in use
❌ Pool timeout! (count: 3)
```

**Solutions:**
1. Increase pool size: `export DB_POOL_SIZE=20`
2. Check for long-running queries
3. Verify database is responding
4. Check if clients are holding connections too long

### Issue: Pool size shrinking

**Symptoms:**
```
📉 Pool size decreased: 5 → 3
❌ Failed to replace connection - pool shrinking!
```

**Solutions:**
1. Check database connectivity
2. Verify timeout settings aren't too aggressive
3. Check PostgreSQL `max_connections` setting
4. Review recent database/network issues

### Issue: Invalid connections

**Symptoms:**
```
⚠️  Invalid connection returned to pool (count: 8)
```

**Solutions:**
1. ✅ Already fixed in latest version (auto-replacement)
2. If persisting, check for:
   - Network instability
   - Database restarts
   - PostgreSQL killing connections (check `statement_timeout`, `idle_in_transaction_session_timeout`)

## Key Metrics to Monitor

1. **Pool Size Stability**: `current_size` should equal `pool_size`
2. **Invalid Connection Rate**: Should be < 1% under normal load
3. **Pool Utilization**: Available connections / Total connections
4. **Timeout Errors**: Should be 0 in steady state
5. **Connection Replacement Success**: Should be 100%

## Performance Benchmarks

Expected performance on reasonable hardware:
- **Concurrent access**: 20 threads × 10 ops = 200 operations with 0 errors
- **Pool acquisition latency**: < 1ms when connections available
- **Connection replacement time**: < 100ms
- **Recovery from invalid connection**: Automatic, no service impact

## Files

- `pool_health_test.cpp` - Main test suite
- `Makefile` - Build configuration
- `monitor_pool_health.sh` - Real-time log monitoring
- `README.md` - This file

## Contributing

When adding new pool features:
1. Add a test case to `pool_health_test.cpp`
2. Update monitoring script if new log patterns are added
3. Run `make test` before committing
4. Update this README with new metrics/patterns

## License

Same as Queen server project.

