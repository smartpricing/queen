# Quickstart Guide for trying out Queen 

A simple guide to try out Queen in a few minutes.

## Run PG and Queen server

```bash
# Network to connect PG and Queen server
docker network create queen 

# Postgres
docker run --name postgres --network queen -e POSTGRES_PASSWORD=postgres -p 5432:5432 -d postgres

# Queen server
docker run -it -p 6632:6632 --network queen -e PG_HOST=postgres -e PG_PORT=5432 -e PG_USER=postgres -e PG_PASSWORD=postgres -e PG_DB=postgres -e DB_POOL_SIZE=20 -e NUM_WORKERS=2 -e DEFAULT_SUBSCRIPTION_MODE=new smartnessai/queen-mq:latest
```

## Run Queen client

```bash
npm install queen-mq
```

```js
import { Queen } from 'queen-mq'
const queen = new Queen('http://localhost:6632')
await queen.queue('my-queue').create()
await queen.queue('my-queue').push([{ data: { message: 'Hello, world!' } }])
const messages = await queen.queue('my-queue').pop()
console.log(messages)
```

## Multi-Server Setup (Optional)

For horizontal scaling, run multiple Queen servers with distributed cache:

```bash
# Server 1
docker run -it -p 6632:6632 -p 6634:6634/udp --network queen \
  -e PG_HOST=postgres \
  -e QUEEN_UDP_PEERS="queen-2:6634" \
  -e QUEEN_UDP_NOTIFY_PORT=6634 \
  --name queen-1 smartnessai/queen-mq:latest

# Server 2
docker run -it -p 6633:6632 -p 6635:6634/udp --network queen \
  -e PG_HOST=postgres \
  -e QUEEN_UDP_PEERS="queen-1:6634" \
  -e QUEEN_UDP_NOTIFY_PORT=6634 \
  --name queen-2 smartnessai/queen-mq:latest
```

Benefits:
- **Distributed cache** reduces database queries by 80-90%
- **Real-time notifications** between servers for instant message delivery
- **Automatic failover** if a server goes down

See [ARCHITECTURE.md](ARCHITECTURE.md) for detailed multi-server configuration.

Then watch the client guide for more details.