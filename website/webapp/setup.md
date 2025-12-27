# Webapp Setup

The web dashboard is bundled with the Queen server - no separate setup required.

## Default Access

When Queen is running, access the dashboard at:

```
http://localhost:6632
```

## Development Setup

If you want to run the webapp separately for development:

```bash
cd app
npm install
npm run dev
```

The dev server runs on port 5173 by default.

### Configuration

Create or edit `app/.env` to point to your Queen server:

```env
VITE_QUEEN_URL=http://localhost:6632
```

### Building for Production

```bash
cd app
npm run build
```

The built files go to `app/dist/` and are automatically served by the Queen server.

## Features

The webapp provides:

- **Messages** - Browse and inspect queue messages
- **Consumer Groups** - Manage consumer group cursors
- **Traces** - Search and view message traces
- **System** - Monitor health and toggle maintenance modes

## Related

- [Installation](/guide/installation) - Server installation
- [Web Dashboard Overview](/webapp/overview) - Feature overview
- [Maintenance Operations](/guide/maintenance-operations) - Emergency procedures

