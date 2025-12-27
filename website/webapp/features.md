# Webapp Features

The Queen MQ web dashboard provides real-time monitoring and management.

## Messages

Browse and inspect messages across all queues.

- View message payload and metadata
- See processing timeline with traces
- Filter by queue, namespace, and status
- Track message state (pending, processing, completed)

## Consumer Groups

Manage consumer groups and their cursors.

- View all consumer groups
- Monitor partition assignments
- Track lag per queue
- **Move to Now** - Skip all pending messages
- **Seek** - Move cursor to specific timestamp
- **Delete** - Remove consumer group state

## Traces

Debug distributed workflows with message tracing.

- Search traces by name
- View cross-service timelines
- See trace statistics
- Drill into individual trace events

## System Status

Monitor Queen health and performance.

- Real-time throughput metrics
- Active connections
- Queue statistics
- **Push Maintenance** - Buffer messages to disk
- **Pop Maintenance** - Pause all consumption

## Maintenance Toggles

Quick access to emergency maintenance modes:

| Toggle | Effect |
|--------|--------|
| Push Maint. | Routes PUSH to file buffer |
| Pop Maint. | Returns empty arrays for POP |

## Related

- [Maintenance Operations](/guide/maintenance-operations) - Emergency procedures
- [Consumer Groups](/guide/consumer-groups) - Consumer group concepts
- [Message Tracing](/guide/tracing) - Add traces to messages
- [Monitoring](/server/monitoring) - Server metrics

