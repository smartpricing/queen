# Deployment

Deploy Queen MQ in production.

## Docker

```bash
docker run -p 6632:6632 \
  -e PG_HOST=postgres \
  -e PG_PASSWORD=password \
  -e DB_POOL_SIZE=50 \
  smartnessai/queen-mq:0.6.5
```

## Kubernetes

```bash
cd helm
helm install queen . -f prod.yaml
```

## systemd

```ini
[Unit]
Description=Queen MQ Server
After=network.target postgresql.service

[Service]
Type=simple
User=queen
WorkingDirectory=/opt/queen
Environment="DB_POOL_SIZE=50"
Environment="NUM_WORKERS=10"
ExecStart=/opt/queen/bin/queen-server
Restart=always

[Install]
WantedBy=multi-user.target
```

[K8s examples](https://github.com/smartpricing/queen/tree/master/helm)
