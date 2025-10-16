# Queen MQ - Deployment Guide

## Quick Start: Production Deployment

### Option 1: Single-Server Deployment (Recommended) ⭐

Deploy Queen with the built-in dashboard on a single port:

```bash
# 1. Build the frontend
cd webapp
npm install
npm run build
cd ..

# 2. Start server with frontend serving enabled
node -e "import('./src/server.js').then(m => m.QueenServer({ serveWebapp: true }))"
```

**Or create a deployment script (`deploy.js`):**

```javascript
import { QueenServer } from './src/server.js';

await QueenServer({
  port: process.env.PORT || 3000,
  host: '0.0.0.0',
  serveWebapp: true
});
```

Then run: `node deploy.js`

**What you get:**
- Frontend Dashboard: `http://your-server:3000/`
- API Endpoints: `http://your-server:3000/api/v1/*`
- WebSocket: `ws://your-server:3000/ws/dashboard`
- Health Check: `http://your-server:3000/health`
- Metrics: `http://your-server:3000/metrics`

**Benefits:**
- ✅ Single port, single process
- ✅ No CORS configuration needed
- ✅ No separate web server (nginx, apache, etc.)
- ✅ Simpler firewall rules
- ✅ Production-ready caching headers

### Option 2: Separate Frontend Server

Keep frontend and backend separate (useful for development or specific architectures):

**Backend (Queen Server):**
```bash
npm start
# Runs on http://localhost:6632
```

**Frontend (Static Server):**
```bash
cd webapp
npm run build
npx serve dist -p 4000
# Runs on http://localhost:4000
```

Configure CORS in Queen config if needed.

## Environment Configuration

### Required Environment Variables

```bash
# Database
export PG_USER=postgres
export PG_HOST=localhost
export PG_DB=queen
export PG_PASSWORD=your_password
export PG_PORT=5432

# Optional: Enable encryption
export QUEEN_ENCRYPTION_KEY=$(openssl rand -hex 32)

# Optional: Server configuration
export QUEEN_PORT=3000
export QUEEN_HOST=0.0.0.0
```

### Initialize Database

Before first run:

```bash
node init-db.js
```

## Docker Deployment

### Dockerfile Example

```dockerfile
FROM node:22-alpine

WORKDIR /app

# Copy package files
COPY package*.json ./
COPY webapp/package*.json ./webapp/

# Install dependencies
RUN npm install
RUN cd webapp && npm install

# Copy source
COPY . .

# Build frontend
RUN cd webapp && npm run build

# Expose port
EXPOSE 3000

# Run server with frontend
CMD ["node", "-e", "import('./src/server.js').then(m => m.QueenServer({ port: 3000, host: '0.0.0.0', serveWebapp: true }))"]
```

### Docker Compose Example

```yaml
version: '3.8'

services:
  postgres:
    image: postgres:15
    environment:
      POSTGRES_DB: queen
      POSTGRES_USER: postgres
      POSTGRES_PASSWORD: postgres
    ports:
      - "5432:5432"
    volumes:
      - pgdata:/var/lib/postgresql/data

  queen:
    build: .
    ports:
      - "3000:3000"
    environment:
      PG_HOST: postgres
      PG_USER: postgres
      PG_PASSWORD: postgres
      PG_DB: queen
      PG_PORT: 5432
      QUEEN_PORT: 3000
      QUEEN_HOST: 0.0.0.0
    depends_on:
      - postgres

volumes:
  pgdata:
```

Run with: `docker-compose up`

## Process Management

### PM2 (Recommended for Production)

```bash
# Install PM2
npm install -g pm2

# Create ecosystem.config.js
cat > ecosystem.config.js << 'EOF'
module.exports = {
  apps: [{
    name: 'queen-mq',
    script: './src/server.js',
    instances: 1,
    exec_mode: 'fork',
    env: {
      NODE_ENV: 'production',
      QUEEN_PORT: 3000,
      QUEEN_HOST: '0.0.0.0'
    }
  }]
};
EOF

# Start with PM2
pm2 start ecosystem.config.js

# Save process list
pm2 save

# Setup auto-restart on server reboot
pm2 startup
```

### Systemd Service

Create `/etc/systemd/system/queen.service`:

```ini
[Unit]
Description=Queen MQ Server
After=network.target postgresql.service

[Service]
Type=simple
User=queen
WorkingDirectory=/opt/queen
ExecStart=/usr/bin/node src/server.js
Restart=always
Environment="NODE_ENV=production"
Environment="QUEEN_PORT=3000"
Environment="PG_HOST=localhost"
Environment="PG_USER=queen"
Environment="PG_DB=queen"

[Install]
WantedBy=multi-user.target
```

Enable and start:
```bash
sudo systemctl enable queen
sudo systemctl start queen
sudo systemctl status queen
```

## Reverse Proxy (Optional)

### Nginx

```nginx
server {
    listen 80;
    server_name queen.example.com;

    location / {
        proxy_pass http://localhost:3000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host $host;
        proxy_cache_bypass $http_upgrade;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }

    # WebSocket support
    location /ws/ {
        proxy_pass http://localhost:3000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_read_timeout 3600s;
        proxy_send_timeout 3600s;
    }
}
```

### Traefik (Docker)

```yaml
labels:
  - "traefik.enable=true"
  - "traefik.http.routers.queen.rule=Host(`queen.example.com`)"
  - "traefik.http.services.queen.loadbalancer.server.port=3000"
```

## Monitoring

### Health Check Endpoint

```bash
curl http://localhost:3000/health
```

Response:
```json
{
  "status": "healthy",
  "uptime": "3600s",
  "connections": 5,
  "stats": {
    "requests": 12345,
    "messages": 54321,
    "requestsPerSecond": "3.43",
    "messagesPerSecond": "15.09"
  }
}
```

### Metrics Endpoint

```bash
curl http://localhost:3000/metrics
```

## Performance Tuning

### PostgreSQL Configuration

Add to `postgresql.conf`:
```ini
# Increase connection limit
max_connections = 200

# Increase shared buffers
shared_buffers = 256MB

# Increase work memory
work_mem = 16MB

# Enable query planning stats
shared_preload_libraries = 'pg_stat_statements'
```

### Node.js Options

```bash
# Increase memory limit if needed
NODE_OPTIONS="--max-old-space-size=4096" node src/server.js
```

## Scaling

### Horizontal Scaling (Multiple Servers)

Run multiple Queen instances with load balancer:

```javascript
// Server 1
await QueenServer({ 
  port: 3001, 
  workerId: 'worker-1',
  serveWebapp: true 
});

// Server 2
await QueenServer({ 
  port: 3002, 
  workerId: 'worker-2',
  serveWebapp: false  // Only one needs to serve frontend
});
```

Use nginx or HAProxy for load balancing.

### Clustering (Single Server)

Use Node.js cluster mode:

```bash
node src/cluster-server.js
```

This will spawn workers based on CPU cores.

## Security Checklist

- [ ] Enable encryption: Set `QUEEN_ENCRYPTION_KEY`
- [ ] Use strong PostgreSQL password
- [ ] Configure firewall rules
- [ ] Enable HTTPS (via reverse proxy)
- [ ] Restrict PostgreSQL access
- [ ] Set up regular backups
- [ ] Monitor error logs
- [ ] Keep dependencies updated

## Backup

### PostgreSQL Backup

```bash
# Backup
pg_dump -U postgres queen > queen_backup.sql

# Restore
psql -U postgres queen < queen_backup.sql
```

### Automated Backups

```bash
# Add to crontab (daily backup at 2 AM)
0 2 * * * pg_dump -U postgres queen | gzip > /backups/queen_$(date +\%Y\%m\%d).sql.gz
```

## Troubleshooting

### Check server logs
```bash
pm2 logs queen
# or
journalctl -u queen -f
```

### Test database connection
```bash
psql -U postgres -d queen -c "SELECT 1"
```

### Check port availability
```bash
netstat -tulpn | grep :3000
```

### Clear stale leases
```sql
UPDATE queen.messages 
SET status = 'ready' 
WHERE status = 'processing' 
  AND lease_expires_at < NOW();
```

## See Also

- [PROGRAMMATIC_SERVER.md](./PROGRAMMATIC_SERVER.md) - Programmatic API usage
- [README.md](./README.md) - Full documentation
- [API.md](./API.md) - HTTP API reference

