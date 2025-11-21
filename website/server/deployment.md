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
              value: "150"
            - name: NUM_WORKERS
              value: "10"
            - name: LOG_LEVEL
              value: "info"
          
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

## See Also

- [Server Installation](/server/installation) - Build and install Queen
- [Configuration](/server/configuration) - Configure the server
- [Environment Variables](/server/environment-variables) - Complete variable reference
- [Monitoring](/server/monitoring) - Set up monitoring
- [Troubleshooting](/server/troubleshooting) - Common issues
