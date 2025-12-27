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
  -e DB_POOL_SIZE=10 \
  -e DEFAULT_SUBSCRIPTION_MODE=new \
  -e LOG_LEVEL=info \
  -e RETENTION_INTERVAL=10000 \
  -e RETENTION_BATCH_SIZE=50000 \
  -e QUEEN_SYNC_ENABLED=true \
  -e QUEEN_UDP_PEERS=localhost:6634,localhost:6635 \
  -e QUEEN_UDP_NOTIFY_PORT=6634 \
  -e FILE_BUFFER_DIR=/tmp/queen/s1 \
  -e SIDECAR_POOL_SIZE=70 \
  -e SIDECAR_MICRO_BATCH_WAIT_MS=10 \
  -e NUM_WORKERS=4 \
  -e POP_WAIT_INITIAL_INTERVAL_MS=10 \
  -e POP_WAIT_MAX_INTERVAL_MS=1000 \
  -e POP_WAIT_BACKOFF_MULTIPLIER=2 \
  -e POP_WAIT_BACKOFF_THRESHOLD=1 \
  -e DB_STATEMENT_TIMEOUT=300000 \
  -e STATS_RECONCILE_INTERVAL_MS=30000 \
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
  -e DB_POOL_SIZE=10 \
  -e DEFAULT_SUBSCRIPTION_MODE=new \
  -e LOG_LEVEL=info \
  -e LOG_FORMAT=json \
  -e RETENTION_INTERVAL=10000 \
  -e RETENTION_BATCH_SIZE=50000 \
  -e QUEEN_SYNC_ENABLED=true \
  -e QUEEN_UDP_PEERS=localhost:6634,localhost:6635 \
  -e QUEEN_UDP_NOTIFY_PORT=6634 \
  -e FILE_BUFFER_DIR=/var/lib/queen/buffers \
  -e SIDECAR_POOL_SIZE=70 \
  -e SIDECAR_MICRO_BATCH_WAIT_MS=10 \
  -e NUM_WORKERS=4 \
  -e POP_WAIT_INITIAL_INTERVAL_MS=10 \
  -e POP_WAIT_MAX_INTERVAL_MS=1000 \
  -e POP_WAIT_BACKOFF_MULTIPLIER=2 \
  -e POP_WAIT_BACKOFF_THRESHOLD=1 \
  -e DB_STATEMENT_TIMEOUT=300000 \
  -e STATS_RECONCILE_INTERVAL_MS=30000 \
  -e QUEEN_ENCRYPTION_KEY=your_64_char_hex_key \
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
# Source: queen-mq/templates/service.yaml
apiVersion: v1
kind: Service
metadata:
  labels:
    run:  queen-mq
  name:  queen-mq-headless
  namespace: smartchat
spec:
  clusterIP: None
  selector:
    run: queen-mq
  ports:
    - name: http
      port: 6632
      protocol: TCP
      targetPort: 6632
    - name: udp-notify
      port: 6633
      protocol: UDP
      targetPort: 6633    
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
              value: "10"
            - name: DEFAULT_SUBSCRIPTION_MODE
              value: new
            - name: LOG_LEVEL
              value: "info"
            - name: RETENTION_INTERVAL
              value: "10000"
            - name: RETENTION_BATCH_SIZE
              value: "50000"
            - name: QUEEN_SYNC_ENABLED
              value: "true"
            - name: QUEEN_UDP_PEERS
              value: "queen-mq-0.queen-mq-headless:6634,queen-mq-1.queen-mq-headless:6634,queen-mq-2.queen-mq-headless:6634"
            - name: QUEEN_UDP_NOTIFY_PORT
              value: "6634"
            - name: FILE_BUFFER_DIR
              value: "/var/lib/queen/buffers"
            - name: SIDECAR_POOL_SIZE
              value: "70"
            - name: SIDECAR_MICRO_BATCH_WAIT_MS
              value: "10"
            - name: NUM_WORKERS
              value: "4"
            - name: POP_WAIT_INITIAL_INTERVAL_MS
              value: "10"
            - name: POP_WAIT_MAX_INTERVAL_MS
              value: "1000"
            - name: POP_WAIT_BACKOFF_MULTIPLIER
              value: "2"
            - name: POP_WAIT_BACKOFF_THRESHOLD
              value: "1"
            - name: DB_STATEMENT_TIMEOUT
              value: "300000"
            - name: STATS_RECONCILE_INTERVAL_MS
              value: "30000"
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

---

## Kubernetes with Headless Service (Recommended)

For optimal performance with client-side affinity routing, use a **headless service** instead of a regular LoadBalancer or ClusterIP service. This allows clients to connect directly to individual Pod IPs and use affinity-based load balancing.

### Why Headless Services?

**Problem with Regular Services:**
- Regular Kubernetes services provide a single VIP (Virtual IP)
- Clients connect to the VIP, K8s kube-proxy does load balancing
- Client-side affinity routing can't work because client only sees one IP
- Defeats the purpose of affinity routing

**Solution with Headless Services:**
- Headless service returns **all Pod IPs** via DNS
- Client connects directly to individual Pods
- Client-side affinity routing works perfectly
- Same consumer group routes to same Pod (poll intention consolidation)

### Headless Service Configuration

```yaml
---
# Headless Service (no ClusterIP)
apiVersion: v1
kind: Service
metadata:
  name: queen-mq-headless
  namespace: queen
  labels:
    run: queen-mq
spec:
  clusterIP: None  # 👈 This makes it headless
  selector:
    run: queen-mq
  ports:
    - name: http
      port: 6632
      protocol: TCP
      targetPort: 6632
---
# StatefulSet (same as above)
apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: queen-mq
  namespace: queen
spec:
  serviceName: queen-mq-headless  # 👈 Link to headless service
  replicas: 3
  selector:
    matchLabels:
      run: queen-mq
  template:
    metadata:
      labels:
        run: queen-mq
    spec:
      # ... (same configuration as above)
```

### Production Example

Complete production setup with headless service:

```yaml
---
# Headless Service
apiVersion: v1
kind: Service
metadata:
  name: queen-mq-headless
  namespace: queen
  labels:
    app: queen-mq
spec:
  clusterIP: None
  publishNotReadyAddresses: false  # Only return ready Pods
  selector:
    app: queen-mq
  ports:
    - name: http
      port: 6632
      protocol: TCP
      targetPort: 6632
---
# StatefulSet with 3 replicas
apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: queen-mq
  namespace: queen
spec:
  serviceName: queen-mq-headless
  replicas: 3
  podManagementPolicy: Parallel  # Start all Pods simultaneously
  selector:
    matchLabels:
      app: queen-mq
  template:
    metadata:
      labels:
        app: queen-mq
    spec:
      terminationGracePeriodSeconds: 40
      
      # Spread Pods across different nodes
      topologySpreadConstraints:
        - maxSkew: 1
          topologyKey: kubernetes.io/hostname
          whenUnsatisfiable: ScheduleAnyway
          labelSelector:
            matchLabels:
              app: queen-mq
      
      containers:
        - name: queen-mq
          image: smartnessai/queen-mq:latest
          ports:
            - name: http
              containerPort: 6632
              protocol: TCP
          
          # Health checks
          startupProbe:
            httpGet:
              path: /health
              port: 6632
            initialDelaySeconds: 5
            periodSeconds: 5
            failureThreshold: 12
          
          livenessProbe:
            httpGet:
              path: /health
              port: 6632
            periodSeconds: 10
            failureThreshold: 3
          
          readinessProbe:
            httpGet:
              path: /health
              port: 6632
            periodSeconds: 5
            failureThreshold: 3
          
          resources:
            requests:
              memory: "512Mi"
              cpu: "500m"
            limits:
              memory: "2Gi"
              cpu: "2000m"
          
          env:
            - name: PG_HOST
              value: postgres.database.svc.cluster.local
            - name: PG_PORT
              value: "5432"
            - name: PG_DB
              value: queen
            - name: DB_POOL_SIZE
              value: "10"
            - name: DEFAULT_SUBSCRIPTION_MODE
              value: new
            - name: LOG_LEVEL
              value: "info"
            - name: RETENTION_INTERVAL
              value: "10000"
            - name: RETENTION_BATCH_SIZE
              value: "50000"
            - name: QUEEN_SYNC_ENABLED
              value: "true"
            - name: QUEEN_UDP_PEERS
              value: "queen-mq-0.queen-mq-headless:6634,queen-mq-1.queen-mq-headless:6634,queen-mq-2.queen-mq-headless:6634"
            - name: QUEEN_UDP_NOTIFY_PORT
              value: "6634"
            - name: FILE_BUFFER_DIR
              value: "/var/lib/queen/buffers"
            - name: SIDECAR_POOL_SIZE
              value: "70"
            - name: SIDECAR_MICRO_BATCH_WAIT_MS
              value: "10"
            - name: NUM_WORKERS
              value: "4"
            - name: POP_WAIT_INITIAL_INTERVAL_MS
              value: "10"
            - name: POP_WAIT_MAX_INTERVAL_MS
              value: "1000"
            - name: POP_WAIT_BACKOFF_MULTIPLIER
              value: "2"
            - name: POP_WAIT_BACKOFF_THRESHOLD
              value: "1"
            - name: DB_STATEMENT_TIMEOUT
              value: "300000"
            - name: STATS_RECONCILE_INTERVAL_MS
              value: "30000"
          
          envFrom:
            - secretRef:
                name: queen-db-credentials
```

### Client Deployment Pattern

For Node.js applications running in the same cluster:

```javascript
// lib/queen.js - Shared Queen instance
import { Queen } from 'queen-mq'
import dns from 'dns/promises'

let queenInstance = null

export async function getQueen() {
  if (queenInstance) return queenInstance
  
  // Resolve headless service
  const serviceName = process.env.QUEEN_SERVICE || 'queen-mq-headless.queen.svc.cluster.local'
  let urls
  
  try {
    const ips = await dns.resolve(serviceName)
    urls = ips.map(ip => `http://${ip}:6632`)
    console.log(`Resolved ${urls.length} Queen Pods:`, urls)
  } catch (error) {
    console.error('DNS resolution failed, using service name:', error)
    urls = [`http://${serviceName}:6632`]
  }
  
  queenInstance = new Queen({
    urls,
    loadBalancingStrategy: 'affinity',
    affinityHashRing: 150,
    enableFailover: true,
    healthRetryAfterMillis: 5000
  })
  
  return queenInstance
}

// Usage in your app
import { getQueen } from './lib/queen.js'

const queen = await getQueen()

await queen.queue('orders')
  .group('order-processor')
  .consume(async (message) => {
    // This will consistently route to the same Pod
    // All order-processor workers hit the same backend
  })
```

### Monitoring

Check that DNS resolution is working:

```bash
# Inside a Pod in the same namespace
nslookup queen-mq-headless.queen.svc.cluster.local

# Should return multiple IPs:
# Name:   queen-mq-headless.queen.svc.cluster.local
# Address: 10.1.2.3
# Address: 10.1.2.4  
# Address: 10.1.2.5
```

Check client connection distribution:

```javascript
// In your application
const httpClient = queen._httpClient
const loadBalancer = httpClient.getLoadBalancer()

console.log('Strategy:', loadBalancer.getStrategy())
console.log('Backends:', loadBalancer.getAllUrls())
console.log('Virtual nodes:', loadBalancer.getVirtualNodeCount())
console.log('Health:', loadBalancer.getHealthStatus())
```

### Troubleshooting

**Problem:** Client only connects to one Pod

```javascript
// Check DNS resolution
const addresses = await dns.resolve('queen-mq-headless.queen.svc.cluster.local')
console.log('Resolved IPs:', addresses)

// If it returns only one IP, check:
// 1. Service has clusterIP: None
// 2. Service selector matches Pod labels
// 3. Pods are ready (readinessProbe passing)
```

**Problem:** Uneven load distribution

```javascript
// With affinity routing and 3 Pods, distribution depends on:
// 1. Number of unique consumer groups
// 2. Virtual node count (increase for better distribution)

const queen = new Queen({
  urls,
  loadBalancingStrategy: 'affinity',
  affinityHashRing: 300  // 👈 Increase for better distribution
})
```

**Problem:** Pods not recovering after restart

```javascript
// Check health retry interval
const queen = new Queen({
  urls,
  healthRetryAfterMillis: 5000  // 👈 Retry unhealthy Pods after 5s
})

// Check logs for:
// "[LoadBalancer] Retrying unhealthy backend: http://10.1.2.3:6632"
// "[LoadBalancer] Backend http://10.1.2.3:6632 recovered and marked as healthy"
```

### Best Practices

1. ✅ **Use headless services** for client-side affinity routing
2. ✅ **Deploy 3+ replicas** for high availability (odd number for better distribution)
3. ✅ **Set podManagementPolicy: Parallel** for faster startup
4. ✅ **Use topologySpreadConstraints** to spread Pods across nodes
5. ✅ **Configure readinessProbe** so DNS only returns ready Pods
6. ✅ **Set healthRetryAfterMillis: 5000** for quick recovery detection
7. ✅ **Monitor health status** in your application logs
8. ✅ **Resolve DNS at startup** and periodically refresh (optional)

---

## Distributed Cache (UDPSYNC)

When running multiple Queen servers, enable the distributed cache to share state between instances via UDP. This significantly reduces database queries and enables targeted notifications.

### Benefits

- **80-90% fewer DB queries** - Cache partition IDs, queue configs, and lease hints
- **Instant notifications** - Only notify servers with active consumers
- **Faster failover** - Heartbeat-based dead server detection

### How It Works

```
┌─────────────┐
│    Load     │
│  Balancer   │
└──────┬──────┘
       │
   ┌───┼────┬────────┐
   ↓   ↓    ↓        ↓
Server1 Server2 Server3 ... ServerN
   │   │    │        │
   └───┴────┴────────┘
     ↕ UDP Sync (UDPSYNC)
          ↓
    PostgreSQL
```

Servers communicate via UDP to sync:
- **Queue configurations** (lease time, encryption settings, etc.)
- **Consumer presence** (which servers have active consumers)
- **Partition ID cache** (partition name → UUID mappings)
- **Lease hints** (which server likely holds a lease)
- **Health status** (heartbeats every 1 second)

### Configuration

Enable by setting `QUEEN_UDP_PEERS` with the list of peer hostnames and UDP ports:

```bash
# Server 1
export QUEEN_UDP_PEERS="queen-2:6634,queen-3:6634"
export QUEEN_UDP_NOTIFY_PORT=6634
./bin/queen-server

# Server 2
export QUEEN_UDP_PEERS="queen-1:6634,queen-3:6634"
export QUEEN_UDP_NOTIFY_PORT=6634
./bin/queen-server

# Server 3
export QUEEN_UDP_PEERS="queen-1:6634,queen-2:6634"
export QUEEN_UDP_NOTIFY_PORT=6634
./bin/queen-server
```

> **Note:** Self-detection is automatic. Each server excludes itself from the peer list based on hostname matching.

### Kubernetes Configuration

For Kubernetes StatefulSet deployments, add the UDP port and peer configuration:

```yaml
apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: queen-mq
spec:
  serviceName: queen-mq-headless
  replicas: 3
  template:
    spec:
      containers:
        - name: queen-mq
          image: smartnessai/queen-mq:latest
          ports:
            - containerPort: 6632  # HTTP API
              name: http
            - containerPort: 6634  # UDP sync
              name: udp
              protocol: UDP
          env:
            # Database
            - name: PG_HOST
              value: "postgres.default.svc.cluster.local"
            
            # UDP Peers (list all replicas)
            - name: QUEEN_UDP_PEERS
              value: "queen-mq-0.queen-mq-headless:6634,queen-mq-1.queen-mq-headless:6634,queen-mq-2.queen-mq-headless:6634"
            - name: QUEEN_UDP_NOTIFY_PORT
              value: "6634"
            
            # Security (recommended for production)
            - name: QUEEN_SYNC_SECRET
              valueFrom:
                secretKeyRef:
                  name: queen-secrets
                  key: sync-secret
```

### Security

For production, sign UDP packets with HMAC-SHA256:

```bash
# Generate a 64-character hex secret
export QUEEN_SYNC_SECRET=$(openssl rand -hex 32)
```

Without a secret, packets are unsigned (acceptable for development only).

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `QUEEN_UDP_PEERS` | "" | Comma-separated peer hostnames with ports |
| `QUEEN_UDP_NOTIFY_PORT` | 6633 | UDP port for peer communication |
| `QUEEN_SYNC_ENABLED` | true | Enable/disable distributed cache |
| `QUEEN_SYNC_SECRET` | "" | HMAC-SHA256 secret for packet signing |
| `QUEEN_CACHE_PARTITION_MAX` | 10000 | Max partition IDs to cache |
| `QUEEN_CACHE_PARTITION_TTL_MS` | 300000 | Partition cache TTL (5 minutes) |
| `QUEEN_SYNC_HEARTBEAT_MS` | 1000 | Heartbeat interval |
| `QUEEN_SYNC_DEAD_THRESHOLD_MS` | 5000 | Server dead threshold |

### Monitoring

View cache statistics in the dashboard or via API:

```bash
curl http://localhost:6632/api/v1/system/shared-state
```

Response includes:
- Cache hit rates for each tier
- Peer connectivity status
- Server health information

### Correctness Guarantee

The distributed cache is **always advisory**. Even with stale or missing cache data:
- ✅ Messages are never lost
- ✅ Duplicate deliveries never occur
- ✅ Leases are always correct (PostgreSQL is authoritative)
- ✅ Only impact is slightly increased DB queries

### Local Development (Multiple Servers)

When running multiple servers on the same machine, use different buffer directories:

```bash
# Terminal 1
export QUEEN_UDP_PEERS="127.0.0.1:6635"
export QUEEN_UDP_NOTIFY_PORT=6634
export FILE_BUFFER_DIR=/tmp/queen-s1
./bin/queen-server --port 6632 --internal-port 6634

# Terminal 2
export QUEEN_UDP_PEERS="127.0.0.1:6634"
export QUEEN_UDP_NOTIFY_PORT=6635
export FILE_BUFFER_DIR=/tmp/queen-s2
./bin/queen-server --port 6633 --internal-port 6635
```

> **Important:** Each server must have its own `FILE_BUFFER_DIR` to prevent file conflicts.

---

## See Also

- [Server Installation](/server/installation) - Build and install Queen
- [Monitoring](/server/monitoring) - Set up monitoring
