# Queen Dashboard

A beautiful, modern web dashboard for monitoring the Queen Message Queue system, built with Vue 3 and Tailwind CSS.

## Features

- 📊 **Real-time Monitoring** - Auto-refreshing metrics and charts
- 🎨 **Modern UI** - Clean, responsive design with dark mode
- 📈 **Bar Charts** - Visual data representation using Chart.js
- 🔍 **Queue Management** - View and filter queues, partitions, and messages
- 📱 **Responsive** - Works on mobile, tablet, and desktop
- ⚡ **Fast** - Built with Vite for optimal performance

## Tech Stack

- **Vue 3** - Progressive JavaScript framework
- **Tailwind CSS** - Utility-first CSS framework
- **Chart.js + vue-chartjs** - Bar charts for data visualization
- **Vue Router** - Client-side routing
- **Vite** - Next generation frontend tooling

## Prerequisites

Before you begin, ensure you have:

- Node.js 22.x or higher
- npm or pnpm package manager
- Queen API server running on `localhost:6632`

## Installation

1. Install dependencies:

```bash
npm install
```

## Development

Start the development server on port 4000:

```bash
npm run dev
```

The dashboard will be available at `http://localhost:4000`

## Configuration

### API Server

The dashboard connects to the Queen API server at `http://localhost:6632` by default. This is configured in:

- **Vite proxy**: `vite.config.js`
- **API client**: `src/api/client.js`

### Port Configuration

The Vite dev server runs on port 4000 as specified in `package.json`:

```json
{
  "scripts": {
    "dev": "vite --port 4000"
  }
}
```

## Build for Production

Build the dashboard for production:

```bash
npm run build
```

Preview the production build:

```bash
npm run preview
```

## Project Structure

```
dashboard/
├── src/
│   ├── api/              # API client
│   ├── assets/           # Static assets (CSS)
│   ├── components/       # Vue components
│   │   ├── common/       # Reusable components
│   │   └── layout/       # Layout components
│   ├── composables/      # Vue composables
│   ├── views/            # Page components
│   ├── App.vue           # Root component
│   ├── main.js           # Application entry point
│   ├── router.js         # Router configuration
│   └── app.config.ts     # UI configuration
├── index.html            # HTML entry point
├── package.json          # Dependencies
├── vite.config.js        # Vite configuration
└── tailwind.config.js    # Tailwind configuration
```

## Pages

- **Dashboard** (`/`) - System overview with metrics and charts
- **Queues** (`/queues`) - List of all queues with filtering
- **Queue Detail** (`/queues/:name`) - Detailed view of a specific queue
- **Messages** (`/queues/:name/messages`) - Browse messages in a queue
- **Analytics** (`/analytics`) - Advanced analytics and performance metrics
- **Settings** (`/settings`) - User preferences and configuration

## Features Detail

### Auto-Refresh

The dashboard automatically refreshes data at configurable intervals:

- Dashboard: 5 seconds
- Queues: 10 seconds
- Queue Detail: 5 seconds
- Analytics: 10 seconds

You can disable auto-refresh in Settings.

### Dark Mode

Toggle between light and dark themes using the button in the top bar. Your preference is saved to localStorage.

### Charts

All charts are implemented using Chart.js with bar chart visualization (no lines or splines) for clarity and impact.

## API Endpoints

The dashboard consumes these Queen API endpoints:

- `GET /api/v1/status` - System overview
- `GET /api/v1/status/queues` - All queues
- `GET /api/v1/status/queues/:queueName` - Queue details
- `GET /api/v1/status/queues/:queueName/messages` - Queue messages
- `GET /api/v1/status/analytics` - Analytics data

## Troubleshooting

### Connection Issues

If you can't connect to the API server:

1. Ensure the Queen server is running on `localhost:6632`
2. Check the browser console for errors
3. Verify the API base URL in Settings

### Build Issues

If you encounter build errors:

1. Clear `node_modules` and reinstall: `rm -rf node_modules && npm install`
2. Clear Vite cache: `rm -rf node_modules/.vite`
3. Ensure you're using Node.js 22.x or higher

## License

Same as Queen Message Queue system.

