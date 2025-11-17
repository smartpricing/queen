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

### Docker Compose

Create `docker-compose.yml`:

```yaml
version: '3.8'

services:
  postgres:
    image: postgres:15
    environment:
      POSTGRES_DB: queen
      POSTGRES_USER: queen
      POSTGRES_PASSWORD: queen_password
    ports:
      - "5432:5432"
    volumes:
      - postgres_data:/var/lib/postgresql/data
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U queen"]
      interval: 5s
      timeout: 5s
      retries: 5

  queen:
    image: smartnessai/queen-mq:{{VERSION}}
    depends_on:
      postgres:
        condition: service_healthy
    ports:
      - "6632:6632"
    volumes:
      - queen_buffers:/var/lib/queen/buffers
    environment:
      PG_HOST: postgres
      PG_PORT: 5432
      PG_USER: queen
      PG_PASSWORD: queen_password
      PG_DB: queen
      DB_POOL_SIZE: 150
      NUM_WORKERS: 10
      PORT: 6632
      LOG_LEVEL: info
      LOG_FORMAT: json
    restart: unless-stopped
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:6632/health"]
      interval: 30s
      timeout: 10s
      retries: 3
      start_period: 40s

volumes:
  postgres_data:
  queen_buffers:
```

Start everything:
```bash
docker-compose up -d
```

---

## Kubernetes

Deploy Queen on Kubernetes with StatefulSet for production reliability.

### Quick Start with Helm

```bash
# Clone the repository
git clone https://github.com/smartpricing/queen.git
cd queen/helm

# Install with production values
helm install queen . -f prod.yaml

# Or customize inline
helm install queen . \
  --set postgres.host=your-postgres-host \
  --set postgres.password=your-password \
  --set replicaCount=3 \
  --set resources.poolSize=150
```

### StatefulSet Example

Create `queen-statefulset.yaml`:

```yaml
---
apiVersion: v1
kind: Secret
metadata:
  name: queen-postgres
  namespace: production
type: Opaque
stringData:
  PG_HOST: "postgres.production.svc.cluster.local"
  PG_PORT: "5432"
  PG_USER: "queen"
  PG_PASSWORD: "your-secure-password"
  PG_DB: "queen_production"

---
apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: queen-mq
  namespace: production
  labels:
    app: queen-mq
spec:
  replicas: 3
  serviceName: queen-mq
  selector:
    matchLabels:
      app: queen-mq
  template:
    metadata:
      labels:
        app: queen-mq
    spec:
      # Topology spread for high availability
      topologySpreadConstraints:
        - maxSkew: 1
          topologyKey: kubernetes.io/hostname
          whenUnsatisfiable: ScheduleAnyway
          labelSelector:
            matchLabels:
              app: queen-mq
      
      containers:
        - name: queen-mq
          image: smartnessai/queen-mq:0.6.6
          imagePullPolicy: Always
          
          ports:
            - name: http
              containerPort: 6632
              protocol: TCP
          
          env:
            - name: DB_POOL_SIZE
              value: "150"
            - name: NUM_WORKERS
              value: "10"
            - name: LOG_LEVEL
              value: "info"
            - name: LOG_FORMAT
              value: "json"
            - name: FILE_BUFFER_DIR
              value: "/var/lib/queen/buffers"
          
          envFrom:
            - secretRef:
                name: queen-postgres
          
          resources:
            requests:
              memory: "512Mi"
              cpu: "500m"
            limits:
              memory: "2Gi"
              cpu: "2000m"
          
          volumeMounts:
            - name: buffer-storage
              mountPath: /var/lib/queen/buffers
          
          livenessProbe:
            httpGet:
              path: /health
              port: 6632
            initialDelaySeconds: 30
            periodSeconds: 10
            timeoutSeconds: 5
            failureThreshold: 3
          
          readinessProbe:
            httpGet:
              path: /health
              port: 6632
            initialDelaySeconds: 10
            periodSeconds: 5
            timeoutSeconds: 3
            failureThreshold: 3
  
  volumeClaimTemplates:
    - metadata:
        name: buffer-storage
      spec:
        accessModes: ["ReadWriteOnce"]
        storageClassName: "fast-ssd"
        resources:
          requests:
            storage: 10Gi

---
apiVersion: v1
kind: Service
metadata:
  name: queen-mq
  namespace: production
  labels:
    app: queen-mq
spec:
  type: ClusterIP
  ports:
    - name: http
      port: 6632
      targetPort: 6632
      protocol: TCP
  selector:
    app: queen-mq
  sessionAffinity: None

---
apiVersion: v1
kind: Service
metadata:
  name: queen-mq-headless
  namespace: production
  labels:
    app: queen-mq
spec:
  type: ClusterIP
  clusterIP: None
  ports:
    - name: http
      port: 6632
      targetPort: 6632
      protocol: TCP
  selector:
    app: queen-mq
```

Deploy:
```bash
kubectl apply -f queen-statefulset.yaml
```

### Ingress Configuration

```yaml
apiVersion: networking.k8s.io/v1
kind: Ingress
metadata:
  name: queen-mq-ingress
  namespace: production
  annotations:
    cert-manager.io/cluster-issuer: "letsencrypt-prod"
    nginx.ingress.kubernetes.io/proxy-body-size: "100m"
    nginx.ingress.kubernetes.io/proxy-connect-timeout: "60"
    nginx.ingress.kubernetes.io/proxy-send-timeout: "60"
    nginx.ingress.kubernetes.io/proxy-read-timeout: "60"
spec:
  ingressClassName: nginx
  tls:
    - hosts:
        - queen.example.com
      secretName: queen-tls
  rules:
    - host: queen.example.com
      http:
        paths:
          - path: /
            pathType: Prefix
            backend:
              service:
                name: queen-mq
                port:
                  number: 6632
```

### Horizontal Pod Autoscaler

```yaml
apiVersion: autoscaling/v2
kind: HorizontalPodAutoscaler
metadata:
  name: queen-mq-hpa
  namespace: production
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: StatefulSet
    name: queen-mq
  minReplicas: 3
  maxReplicas: 10
  metrics:
    - type: Resource
      resource:
        name: cpu
        target:
          type: Utilization
          averageUtilization: 70
    - type: Resource
      resource:
        name: memory
        target:
          type: Utilization
          averageUtilization: 80
```

### Network Policies

```yaml
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: queen-mq-network-policy
  namespace: production
spec:
  podSelector:
    matchLabels:
      app: queen-mq
  policyTypes:
    - Ingress
    - Egress
  ingress:
    - from:
        - namespaceSelector:
            matchLabels:
              name: production
      ports:
        - protocol: TCP
          port: 6632
  egress:
    - to:
        - namespaceSelector:
            matchLabels:
              name: production
      ports:
        - protocol: TCP
          port: 5432  # PostgreSQL
    - to:  # Allow DNS
        - namespaceSelector: {}
          podSelector:
            matchLabels:
              k8s-app: kube-dns
      ports:
        - protocol: UDP
          port: 53
```

### Resource Recommendations

| Deployment Size | Replicas | CPU Request | CPU Limit | Memory Request | Memory Limit | DB_POOL_SIZE |
|-----------------|----------|-------------|-----------|----------------|--------------|--------------|
| **Small** | 2 | 250m | 1000m | 256Mi | 1Gi | 50 |
| **Medium** | 3 | 500m | 2000m | 512Mi | 2Gi | 150 |
| **Large** | 5 | 1000m | 4000m | 1Gi | 4Gi | 300 |
| **Very Large** | 10 | 2000m | 8000m | 2Gi | 8Gi | 500 |

### Monitoring with Prometheus

```yaml
apiVersion: v1
kind: ServiceMonitor
metadata:
  name: queen-mq
  namespace: production
spec:
  selector:
    matchLabels:
      app: queen-mq
  endpoints:
    - port: http
      path: /metrics
      interval: 30s
```

---

## systemd

For traditional Linux deployments.

### Service File

Create `/etc/systemd/system/queen-server.service`:

```ini
[Unit]
Description=Queen MQ Message Queue Server
Documentation=https://github.com/smartpricing/queen
After=network.target postgresql.service
Requires=postgresql.service

[Service]
Type=simple
User=queen
Group=queen
WorkingDirectory=/opt/queen
ExecStart=/opt/queen/bin/queen-server
Restart=always
RestartSec=10

# Environment variables
Environment="PORT=6632"
Environment="HOST=0.0.0.0"
Environment="PG_HOST=localhost"
Environment="PG_PORT=5432"
Environment="PG_DB=queen_production"
Environment="PG_USER=queen"
Environment="PG_PASSWORD=secure_password"
Environment="DB_POOL_SIZE=150"
Environment="NUM_WORKERS=10"
Environment="LOG_LEVEL=info"
Environment="LOG_FORMAT=json"
Environment="FILE_BUFFER_DIR=/var/lib/queen/buffers"

# Security hardening
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/queen/buffers
ReadWritePaths=/var/log/queen

# Resource limits
LimitNOFILE=65536
LimitNPROC=4096
LimitMEMLOCK=infinity

[Install]
WantedBy=multi-user.target
```

### Setup

```bash
# Create user
sudo useradd -r -s /bin/false queen

# Create directories
sudo mkdir -p /opt/queen
sudo mkdir -p /var/lib/queen/buffers
sudo mkdir -p /var/log/queen

# Set permissions
sudo chown -R queen:queen /opt/queen
sudo chown -R queen:queen /var/lib/queen
sudo chown -R queen:queen /var/log/queen

# Install binary
sudo cp bin/queen-server /opt/queen/

# Enable and start service
sudo systemctl daemon-reload
sudo systemctl enable queen-server
sudo systemctl start queen-server

# Check status
sudo systemctl status queen-server

# View logs
sudo journalctl -u queen-server -f
```

---

## Load Balancing

For horizontal scaling, run multiple Queen instances behind a load balancer.

### nginx Configuration

```nginx
upstream queen_cluster {
    least_conn;  # or ip_hash for session affinity
    
    server queen1.local:6632 max_fails=3 fail_timeout=30s;
    server queen2.local:6632 max_fails=3 fail_timeout=30s;
    server queen3.local:6632 max_fails=3 fail_timeout=30s;
}

server {
    listen 80;
    server_name queen.example.com;
    
    # Redirect to HTTPS
    return 301 https://$server_name$request_uri;
}

server {
    listen 443 ssl http2;
    server_name queen.example.com;
    
    ssl_certificate /etc/ssl/certs/queen.crt;
    ssl_certificate_key /etc/ssl/private/queen.key;
    
    # Modern SSL configuration
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers HIGH:!aNULL:!MD5;
    ssl_prefer_server_ciphers on;
    
    client_max_body_size 100M;
    
    location / {
        proxy_pass http://queen_cluster;
        proxy_http_version 1.1;
        
        # WebSocket support
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        
        # Headers
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        
        # Timeouts
        proxy_connect_timeout 60s;
        proxy_send_timeout 60s;
        proxy_read_timeout 60s;
        
        # Buffering
        proxy_buffering off;
        proxy_request_buffering off;
    }
    
    # Health check endpoint (for load balancer)
    location /health {
        proxy_pass http://queen_cluster/health;
        access_log off;
    }
}
```

### HAProxy Configuration

```haproxy
global
    log /dev/log local0
    maxconn 4096
    daemon

defaults
    log global
    mode http
    option httplog
    option dontlognull
    timeout connect 10s
    timeout client 60s
    timeout server 60s

frontend queen_frontend
    bind *:80
    bind *:443 ssl crt /etc/ssl/certs/queen.pem
    
    # Redirect HTTP to HTTPS
    redirect scheme https code 301 if !{ ssl_fc }
    
    default_backend queen_backend

backend queen_backend
    balance roundrobin
    option httpchk GET /health
    http-check expect status 200
    
    server queen1 queen1.local:6632 check inter 5s rise 2 fall 3
    server queen2 queen2.local:6632 check inter 5s rise 2 fall 3
    server queen3 queen3.local:6632 check inter 5s rise 2 fall 3
```

---

## High Availability Setup

### Multi-Instance Deployment

```
┌─────────────┐
│   Clients   │
└──────┬──────┘
       │
┌──────┴────────┐
│ Load Balancer │
│   (nginx)     │
└───────┬───────┘
        │
    ┌───┴───┬───────┬────────┐
    ↓       ↓       ↓        ↓
┌────────┐ ┌────────┐ ┌────────┐
│Queen #1│ │Queen #2│ │Queen #3│
└───┬────┘ └───┬────┘ └───┬────┘
    └──────────┴──────────┴────────┐
                 ↓
         ┌─────────────────┐
         │   PostgreSQL    │
         │  (with replica) │
         └─────────────────┘
```

**Benefits:**
- ✅ No single point of failure
- ✅ Horizontal scalability
- ✅ Session-less design (any server handles any request)
- ✅ Automatic failover

**Considerations:**
- FIFO ordering guaranteed per partition per server
- For strict global ordering, coordinate partitions or use single server

---

## Production Checklist

### Before Deployment

- [ ] PostgreSQL tuned for production workload
- [ ] SSL/TLS configured for database connections
- [ ] Encryption key generated and secured
- [ ] Resource limits configured appropriately
- [ ] Monitoring and alerting set up
- [ ] Backup strategy in place
- [ ] Log aggregation configured
- [ ] Health check endpoints tested

### Configuration

- [ ] `DB_POOL_SIZE` sized for workload
- [ ] `NUM_WORKERS` matches CPU cores
- [ ] `LOG_LEVEL=info` or `warn` in production
- [ ] `LOG_FORMAT=json` for log aggregation
- [ ] File buffer directory has sufficient disk space
- [ ] Network policies/firewall rules configured

### Security

- [ ] PostgreSQL access restricted
- [ ] TLS/SSL enabled for all connections
- [ ] Authentication proxy deployed (if needed)
- [ ] Secrets managed securely (not in config files)
- [ ] Regular security updates scheduled

### Monitoring

- [ ] Prometheus metrics endpoint exposed
- [ ] Grafana dashboards configured
- [ ] Alerts for high queue depth
- [ ] Alerts for database connection pool exhaustion
- [ ] Alerts for disk space (file buffer directory)
- [ ] Alerts for error rates

---

## Troubleshooting

### Pods/Containers Failing

```bash
# Check logs
kubectl logs -f deployment/queen-mq
docker logs -f queen-server

# Check events
kubectl describe pod queen-mq-0

# Check database connectivity
kubectl exec -it queen-mq-0 -- curl -v postgres:5432
```

### High Memory Usage

```bash
# Reduce pool size
DB_POOL_SIZE=50

# Reduce workers
NUM_WORKERS=4
```

### Database Connection Issues

```bash
# Test connectivity
psql -h postgres -U queen -d queen_production

# Check max_connections
kubectl exec -it postgres-0 -- psql -U postgres -c "SHOW max_connections"
```

---

## See Also

- [Server Installation](/server/installation) - Build and install Queen
- [Configuration](/server/configuration) - Configure the server
- [Environment Variables](/server/environment-variables) - Complete variable reference
- [Monitoring](/server/monitoring) - Set up monitoring
- [Troubleshooting](/server/troubleshooting) - Common issues
