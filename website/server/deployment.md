# Deployment

Deploy Queen MQ to production environments using Docker, Kubernetes, or systemd.

## Docker

The simplest way to deploy Queen in production.

### Basic Deployment

```bash
docker run -p 6632:6632 \
  -e PG_HOST=postgres \
  -e PG_USER=queen \
  -e PG_PASSWORD=secure_password \
  -e PG_DB=queen_production \
  -e DB_POOL_SIZE=150 \
  -e NUM_WORKERS=10 \
  smartnessai/queen-mq:{{VERSION}}
```

### Production Configuration

```bash
docker run -d \
  --name queen-server \
  --restart unless-stopped \
  -p 6632:6632 \
  -v /var/lib/queen/buffers:/var/lib/queen/buffers \
  -e PG_HOST=db.production.example.com \
  -e PG_PORT=5432 \
  -e PG_USER=queen_user \
  -e PG_PASSWORD=secure_password \
  -e PG_DB=queen_production \
  -e PG_USE_SSL=true \
  -e PG_SSL_REJECT_UNAUTHORIZED=true \
  -e DB_POOL_SIZE=300 \
  -e NUM_WORKERS=20 \
  -e LOG_LEVEL=info \
  -e LOG_FORMAT=json \
  -e QUEEN_ENCRYPTION_KEY=your_64_char_hex_key \
  -e FILE_BUFFER_DIR=/var/lib/queen/buffers \
  smartnessai/queen-mq:{{VERSION}}
```

---

## Kubernetes

Deploy Queen on Kubernetes with StatefulSet for production reliability.
The following is our production deployment configuration. Keep in mind that the kind of service session affinity change somewhat the behavior of the server replicas. With None, all the server will poll the same consumer groups in random way, increasing CPU and DB load, but with client IP, some replicas will could be under or overused.

```yaml
---
# Source: queen-mq/templates/service.yaml
apiVersion: v1
kind: Service
metadata:
  labels:
    run:  queen-mq
  name:  queen-mq
  namespace: smartchat
spec:
  sessionAffinity: ClientIP # ClientIP or None
  sessionAffinityConfig:
    clientIP:
      timeoutSeconds: 300
  selector:
    run: queen-mq
  ports:
    - name: port-1
      port: 6632
      protocol: TCP
      targetPort: 6632
---
# Source: queen-mq/templates/statefulset.yaml
apiVersion: apps/v1
kind: StatefulSet
metadata:
  labels:
    run: queen-mq
  name: queen-mq
  namespace: queen
spec:
  replicas: 3
  selector:
    matchLabels:
      run: queen-mq
  template:
    metadata:
      labels:
        run: queen-mq
        app.kubernetes.io/name: queen-mq
      namespace: queen
    spec:
      terminationGracePeriodSeconds: 40
      topologySpreadConstraints:
        - maxSkew: 1
          topologyKey: kubernetes.io/hostname
          whenUnsatisfiable: ScheduleAnyway
          labelSelector:
            matchLabels:
              run: queen-mq
      containers:
        - name: queen-mq
          imagePullPolicy: Always
          image: smartnessai/queen-mq:{{VERSION}}
          ports:
            - containerPort: 6632
          volumeMounts:
            - mountPath: "/var/lib/queen/buffers"
              name: queen-mq-storage-prod            
          resources:
            limits:
              memory: 1000Mi
            requests:
              memory: 1000Mi
          command: ['./bin/queen-server']
          startupProbe:
            httpGet:
              path: /health
              port: 6632
            initialDelaySeconds: 5
            periodSeconds: 5
            timeoutSeconds: 3
            failureThreshold: 12
          livenessProbe:
            httpGet:
              path: /health
              port: 6632
            initialDelaySeconds: 10
            periodSeconds: 10
            timeoutSeconds: 3
            failureThreshold: 3
          readinessProbe:
            httpGet:
              path: /health
              port: 6632
            initialDelaySeconds: 5
            periodSeconds: 5
            timeoutSeconds: 3
            failureThreshold: 3
          envFrom:
            - secretRef:
                name: prod
            - secretRef:
                name: postgres                
          env:
            - name: PG_DB
              value: queen
            - name: DB_POOL_SIZE
              value: "40"
            - name: NUM_WORKERS
              value: "2"
            - name: QUEUE_POLL_INTERVAL
              value: "1000"
            - name: POLL_DB_INTERVAL
              value: "500"
            - name: POLL_WORKER_COUNT
              value: "1"
            - name: POLL_WORKER_INTERVAL
              value: "500"
            - name: QUEUE_MAX_POLL_INTERVAL
              value: "5000"
            - name: LOG_LEVEL
              value: "info"
            - name: DEFAULT_SUBSCRIPTION_MODE
              value: new
            - name: PARTITION_CLEANUP_DAYS
              value: "7"
            - name: RETENTION_BATCH_SIZE
              value: "1000"
            - name: RETENTION_INTERVAL
              value: "600000"
  volumeClaimTemplates:
    - metadata:
        name: queen-mq-storage-prod
      spec:
        accessModes:
          - ReadWriteOnce
        resources:
          requests:
            storage: 10Gi
        storageClassName: standard-rwo
```

### Resource Recommendations

| Deployment Size | Replicas | CPU Request | CPU Limit | Memory Request | Memory Limit | DB_POOL_SIZE |
|-----------------|----------|-------------|-----------|----------------|--------------|--------------|
| **Small** | 2 | 250m | 1000m | 256Mi | 1Gi | 50 |
| **Medium** | 3 | 500m | 2000m | 512Mi | 2Gi | 150 |
| **Large** | 5 | 1000m | 4000m | 1Gi | 4Gi | 300 |
| **Very Large** | 10 | 2000m | 8000m | 2Gi | 8Gi | 500 |


## See Also

- [Server Installation](/server/installation) - Build and install Queen
- [Configuration](/server/configuration) - Configure the server
- [Environment Variables](/server/environment-variables) - Complete variable reference
- [Monitoring](/server/monitoring) - Set up monitoring
- [Troubleshooting](/server/troubleshooting) - Common issues
