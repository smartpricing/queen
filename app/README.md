# Queen Dashboard

A beautiful, modern dashboard for monitoring and managing Queen message queues.

## Features

- 🎨 **Beautiful Design** - Modern UI with vibrant magenta, cyan, and gold accents inspired by the Queen logo
- 🌓 **Dark/Light Theme** - Seamless theme switching with system preference detection
- 📊 **Real-time Charts** - Live throughput, latency, and resource monitoring
- 📋 **Queue Management** - View, search, and manage all your queues
- 💬 **Message Browser** - Browse, search, retry, and manage messages
- 👥 **Consumer Groups** - Monitor consumer health and lag
- 📈 **Analytics** - Deep performance insights and error tracking
- ⚙️ **System Monitoring** - Server health, memory, CPU, and worker status

## Tech Stack

- **Vue 3** - Progressive JavaScript framework
- **Vite** - Lightning fast build tool
- **Tailwind CSS** - Utility-first CSS framework
- **Chart.js** - Beautiful responsive charts
- **Vue Router** - Client-side routing
- **Axios** - HTTP client

## Getting Started

### Prerequisites

- Node.js 18+ (we recommend using nvm)
- npm or yarn

### Installation

```bash
# Navigate to the app directory
cd app

# Install dependencies
npm install

# Start development server
npm run dev
```

The app will be available at `http://localhost:5173`

### Build for Production

```bash
npm run build
```

The built files will be in the `dist` directory.

## Configuration

### Environment Variables

Create a `.env` file in the app directory:

```env
# API Base URL (defaults to '' which uses the proxy)
VITE_API_BASE_URL=

# Optional: Override API endpoint for production
VITE_API_BASE_URL=http://your-queen-server:6632
```

### API Proxy

In development, the Vite dev server proxies API requests to `http://localhost:6632`. You can change this in `vite.config.js`.

## Project Structure

```
app/
├── public/           # Static assets
├── src/
│   ├── api/          # API client and endpoints
│   ├── components/   # Reusable Vue components
│   │   ├── BaseChart.vue
│   │   ├── DataTable.vue
│   │   ├── Header.vue
│   │   ├── MetricCard.vue
│   │   └── Sidebar.vue
│   ├── composables/  # Vue composables
│   │   ├── useApi.js
│   │   └── useTheme.js
│   ├── router/       # Vue Router configuration
│   ├── views/        # Page components
│   │   ├── Analytics.vue
│   │   ├── Consumers.vue
│   │   ├── Dashboard.vue
│   │   ├── Messages.vue
│   │   ├── QueueDetail.vue
│   │   ├── Queues.vue
│   │   └── System.vue
│   ├── App.vue       # Root component
│   ├── main.js       # Entry point
│   └── style.css     # Global styles & design system
├── index.html
├── package.json
├── tailwind.config.js
└── vite.config.js
```

## Design System

### Colors

- **Queen (Primary)** - Vibrant magenta/pink `#EC4899`
- **Cyber (Secondary)** - Cyan/teal `#06B6D4`
- **Crown (Accent)** - Gold/yellow `#F59E0B`

### Components

The app includes several reusable components:

- `MetricCard` - Display metrics with icons, trends, and progress bars
- `BaseChart` - Wrapper for Chart.js with theme support
- `DataTable` - Sortable, paginated tables with custom templates
- `Sidebar` - Navigation with health status
- `Header` - Search, theme toggle, and notifications

## License

MIT

