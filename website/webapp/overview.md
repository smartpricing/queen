# Web Dashboard Overview

The Queen MQ web dashboard is bundled with the server and provides real-time monitoring and management capabilities.

## Accessing the Dashboard

Once Queen is running, access the dashboard at:

```
http://localhost:6632
```

No additional installation required - it's built into the server.

## Main Features

### Messages View

- Browse messages across all queues
- View message details and payload
- See processing timeline with traces
- Filter by queue, namespace, and status

### Consumer Groups

- View all consumer groups and their status
- Monitor partition assignments
- Track consumer lag per queue
- Perform seek and move operations

### Traces

- Search traces by name
- View cross-service workflows
- Debug distributed message flows
- See trace statistics and timelines

### System Status

- Real-time throughput metrics
- Connection statistics
- Queue depth monitoring
- Maintenance mode toggles

## Related

- [Installation](/guide/installation) - Get Queen running
- [Message Tracing](/guide/tracing) - Add traces to messages
- [Maintenance Operations](/guide/maintenance-operations) - Emergency procedures
- [Monitoring](/server/monitoring) - Server metrics and health checks

