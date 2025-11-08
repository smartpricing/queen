# Troubleshooting

Common issues and solutions.

## Server Won't Start

**Check PostgreSQL:**
```bash
psql -h localhost -U queen -d queen
```

**Check logs:**
```bash
docker logs queen-server
```

## Connection Errors

**Allow port:**
```bash
sudo ufw allow 6632
```

**Check server:**
```bash
curl http://localhost:6632/health
```

## High Latency

1. **Check database performance**
2. **Increase DB_POOL_SIZE**
3. **Increase NUM_WORKERS**
4. **Monitor with** `/metrics`

## Memory Issues

1. Reduce DB_POOL_SIZE
2. Reduce NUM_WORKERS
3. Check for leaks in logs

## Message Loss

Check failover:
```bash
ls -lh /var/lib/queen/buffers/
```

## Getting Help

- [GitHub Issues](https://github.com/smartpricing/queen/issues)
- [LinkedIn](https://www.linkedin.com/company/smartness-com/)
- [Documentation](/)
