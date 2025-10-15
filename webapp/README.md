# Queen Message Queue - Web Application

Modern web interface for managing and monitoring the Queen Message Queue system.

## Prerequisites

- Node.js 18+ 
- npm or yarn
- Queen Message Queue server running on `http://localhost:6632`

## Installation

Install dependencies:

```bash
npm install vue@latest vue-router@latest axios@latest
npm install chart.js@latest vue-chartjs@latest
npm install -D vite@latest @vitejs/plugin-vue@latest
npm install -D tailwindcss@latest postcss@latest autoprefixer@latest
```

Or all at once:

```bash
npm install vue vue-router axios chart.js vue-chartjs && npm install -D vite @vitejs/plugin-vue tailwindcss postcss autoprefixer
```

## Configuration

The application connects to the Queen API at `http://localhost:6632` by default.

To change this, create a `.env` file:

```
VITE_API_BASE_URL=http://your-api-url:port
```

## Development

Start the development server on port 4000:

```bash
npm run dev
```

The application will be available at `http://localhost:4000`

## Build for Production

```bash
npm run build
```

Output will be in the `dist/` directory.

## Features

### ✅ Implemented (Dashboard)
- Real-time health monitoring
- System metrics and statistics
- Message throughput visualization
- Performance monitoring
- Top queues overview
- Dark/light theme support

### 🚧 Coming Soon
- Queues management
- Queue detail views
- Consumer groups monitoring
- Message browser
- Analytics and insights

## Architecture

### Technology Stack
- **Vue 3** - Composition API
- **Vue Router** - Client-side routing
- **Axios** - HTTP client
- **Chart.js** - Data visualization
- **Tailwind CSS** - Styling
- **Vite** - Build tool

### Project Structure

```
webapp/
├── public/          # Static assets
├── src/
│   ├── api/         # API client modules
│   ├── assets/      # Styles and fonts
│   ├── components/  # Vue components
│   │   ├── common/      # Reusable components
│   │   ├── dashboard/   # Dashboard-specific
│   │   └── layout/      # Layout components
│   ├── composables/ # Vue composables
│   ├── router/      # Router configuration
│   ├── utils/       # Utility functions
│   └── views/       # Page components
```

## API Integration

The application integrates with the following Queen API endpoints:

**Dashboard:**
- `GET /health` - Server health
- `GET /metrics` - System metrics
- `GET /api/v1/resources/overview` - System overview
- `GET /api/v1/status` - Status with throughput
- `GET /api/v1/resources/queues` - Queue list

All API calls use real data - no mocking!

## Development Notes

- **No WebSocket** - Currently using manual refresh only
- **No Auto-refresh** - User-controlled data updates
- **Clean Design** - Minimal animations, focus on clarity
- **Responsive** - Works on desktop and tablet
- **Theme Support** - Full light/dark mode

## Troubleshooting

**CORS Errors:**
Ensure the Queen server has CORS enabled. The server should include:
```
Access-Control-Allow-Origin: *
```

**API Connection:**
Check that the Queen server is running on port 6632:
```bash
curl http://localhost:6632/health
```

**Port 4000 in use:**
Change the port in `vite.config.js`:
```javascript
server: {
  port: 4001, // or another port
}
```

