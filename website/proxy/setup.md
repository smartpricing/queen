# Proxy Setup

Install and configure the Queen MQ proxy server.

## Installation

```bash
cd proxy
npm install
```

## Configuration

Create `.env`:

```env
PORT=3000
QUEEN_URL=http://localhost:6632
PG_HOST=localhost
PG_PORT=5432
PG_USER=queen
PG_PASSWORD=password
PG_DB=queen
JWT_SECRET=your-secret-key
SESSION_SECRET=your-session-secret
```

## Create Users

```bash
node src/create-user.js admin@example.com password123 admin
```

## Run

```bash
npm start
```

Proxy available at `http://localhost:3000`.

## Docker

```bash
docker build -t queen-proxy .
docker run -p 3000:3000 \
  -e QUEEN_URL=http://queen:6632 \
  -e PG_HOST=postgres \
  queen-proxy
```

## nginx SSL

```nginx
server {
    listen 443 ssl;
    server_name queen.example.com;
    
    ssl_certificate /path/to/cert.pem;
    ssl_certificate_key /path/to/key.pem;
    
    location / {
        proxy_pass http://localhost:3000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
    }
}
```

[Complete guide](https://github.com/smartpricing/queen/tree/master/proxy)
