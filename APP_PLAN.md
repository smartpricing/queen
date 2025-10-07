# Queen Dashboard Application Plan

## Overview
A beautiful, dark-themed Vue 3 dashboard application for monitoring and managing the Queen message queue system in real-time.

## Quick Start
```bash
# Create the app
cd src/app
npm create vite@latest . -- --template vue
npm install

# Install dependencies
npm install primevue primeicons primeflex
npm install vue-router pinia axios
npm install vue-chartjs chart.js date-fns @vueuse/core

# Start development
npm run dev
```

## Tech Stack
- **Framework**: Vue 3 + Vite
- **UI Components**: PrimeVue 3 with Lara Dark theme
- **CSS Framework**: PrimeFlex (PrimeVue's flexbox utilities) + custom CSS
- **Charts**: vue-chartjs (Chart.js for Vue)
- **Router**: Vue Router 4
- **WebSocket**: Native WebSocket API for real-time updates
- **State Management**: Pinia
- **Icons**: PrimeIcons + lucide-vue for extras
- **Date Handling**: date-fns
- **HTTP Client**: axios

## Project Structure
```
/src/app/
├── vite.config.js           # Vite configuration
├── package.json             # Dependencies
├── tailwind.config.js       # Tailwind configuration
├── index.html               # Entry HTML file
├── src/
│   ├── main.js             # Vue app initialization
│   ├── App.vue             # Root component
│   ├── router/
│   │   └── index.js        # Vue Router configuration
│   ├── views/              # Route views
│   │   ├── Dashboard.vue   # Dashboard home
│   │   ├── Queues.vue      # Queue list
│   │   ├── QueueDetail.vue # Queue details
│   │   ├── Namespaces.vue  # Namespace list
│   │   ├── NamespaceDetail.vue # Namespace details
│   │   ├── Messages.vue    # Message browser
│   │   ├── MessageDetail.vue # Message details
│   │   ├── Analytics.vue   # Analytics overview
│   │   ├── AnalyticsThroughput.vue  # Throughput metrics
│   │   ├── AnalyticsPerformance.vue # Performance metrics
│   │   ├── Workers.vue     # Worker monitoring
│   │   ├── Configure.vue   # Queue configuration
│   │   ├── TestTool.vue    # Message testing tool
│   │   └── Settings.vue    # Dashboard settings
│   ├── layouts/
│   │   └── MainLayout.vue  # Main layout with sidebar
│   ├── components/
│   │   ├── layout/
│   │   │   ├── Sidebar.vue     # Navigation sidebar
│   │   │   ├── Header.vue      # Top header
│   │   │   └── Footer.vue      # Footer
│   │   ├── dashboard/
│   │   │   ├── StatsCard.vue   # Statistics card
│   │   │   ├── QueueCard.vue   # Queue summary card
│   │   │   └── RealtimeChart.vue # Real-time chart
│   │   ├── queues/
│   │   │   ├── QueueList.vue   # Queue list table
│   │   │   ├── QueueDetails.vue # Queue detail view
│   │   │   ├── QueueConfig.vue # Queue configuration form
│   │   │   └── QueueDepth.vue  # Queue depth chart
│   │   ├── messages/
│   │   │   ├── MessageList.vue # Message list table
│   │   │   ├── MessageDetails.vue # Message detail view
│   │   │   ├── MessagePush.vue # Push message form
│   │   │   └── MessagePop.vue  # Pop message interface
│   │   ├── charts/
│   │   │   ├── ThroughputChart.vue # Messages/sec chart
│   │   │   ├── LatencyChart.vue    # Processing latency
│   │   │   ├── QueueDepthChart.vue # Queue depths over time
│   │   │   └── StatusPieChart.vue  # Message status distribution
│   │   ├── workers/
│   │   │   ├── WorkerList.vue  # Active workers
│   │   │   └── WorkerCard.vue  # Worker details
│   │   └── common/
│   │       ├── DataTable.vue   # Reusable data table
│   │       ├── StatusBadge.vue # Status indicator
│   │       ├── JsonViewer.vue  # JSON payload viewer
│   │       └── TimeAgo.vue     # Relative time display
│   ├── composables/
│   │   ├── useWebSocket.js     # WebSocket connection
│   │   ├── useQueenApi.js      # API client wrapper
│   │   ├── useRealtime.js      # Real-time data subscriptions
│   │   └── useNotifications.js # Toast notifications
│   ├── stores/
│   │   ├── queues.js           # Queue state management
│   │   ├── messages.js         # Message state
│   │   ├── analytics.js        # Analytics data
│   │   ├── workers.js          # Worker state
│   │   └── settings.js         # App settings
│   ├── utils/
│   │   ├── formatters.js       # Data formatters
│   │   ├── constants.js        # App constants
│   │   └── api.js              # API helpers
│   └── assets/
│       ├── styles/
│       │   └── main.css        # Global styles
│       └── images/
│           └── logo.svg        # App logo
└── public/
    └── favicon.ico             # App favicon
```

## Routes & Features

### 1. Dashboard Home (`/`)
- **Overview Statistics**
  - Total messages (pending, processing, completed, failed)
  - Active queues count
  - Active workers count
  - Current throughput (msg/sec)
- **Real-time Charts**
  - Throughput line chart (last 5 minutes)
  - Queue depth stacked area chart
  - Message status pie chart
- **Recent Activity Feed**
  - Live WebSocket feed of message events
  - Color-coded by event type
- **Quick Actions**
  - Push test message
  - View all queues
  - Configure new queue

### 2. Queue Management (`/queues`)
- **Queue List** (`/queues`)
  - Table with columns: Name, Namespace/Task, Priority, Depth, Processing, Status
  - Real-time depth updates
  - Sorting and filtering
  - Bulk actions (pause, resume, clear)
- **Queue Details** (`/queues/:id`)
  - Queue configuration display
  - Real-time message flow visualization
  - Queue-specific metrics
  - Message browser for this queue
  - Actions: Configure, Clear, Delete

### 3. Namespace Management (`/namespaces`)
- **Namespace List** (`/namespaces`)
  - Hierarchical view of namespaces → tasks → queues
  - Aggregate statistics per namespace
  - Expandable tree view
- **Namespace Details** (`/namespaces/:id`)
  - All queues in namespace
  - Namespace-level analytics
  - Bulk operations

### 4. Message Browser (`/messages`)
- **Message List** (`/messages`)
  - Advanced filtering (status, queue, time range)
  - Payload preview
  - Bulk operations (retry, delete, move to DLQ)
- **Message Details** (`/messages/:id`)
  - Full payload viewer (JSON)
  - Processing history
  - Transaction ID
  - Actions: Retry, Delete, Move to DLQ

### 5. Analytics (`/analytics`)
- **Overview** (`/analytics`)
  - Key performance indicators
  - Trend analysis
- **Throughput** (`/analytics/throughput`)
  - Historical throughput charts
  - Peak/average analysis
  - Queue-by-queue breakdown
- **Performance** (`/analytics/performance`)
  - Processing latency distribution
  - Error rates
  - Retry rates
  - Dead letter queue analysis

### 6. Worker Monitoring (`/workers`)
- Active worker list
- Worker health status
- Messages processed per worker
- Worker connection history
- WebSocket connection status

### 7. Queue Configuration (`/configure`)
- Create new queue form
- Configure existing queue
- Options:
  - Lease time
  - Priority
  - Delayed processing
  - Window buffer
  - Retry limits
  - Dead letter queue settings

### 8. Testing Tool (`/test`)
- **Message Push Tester**
  - Form to push test messages
  - Batch push support
  - Payload templates
- **Message Pop Tester**
  - Pop messages with options
  - Long polling test
  - Performance benchmarking
- **Load Testing**
  - Generate load patterns
  - Measure system performance

### 9. Settings (`/settings`)
- Dashboard preferences
  - Theme customization (dark mode variations)
  - Refresh intervals
  - Notification preferences
- API configuration
  - Server URL
  - WebSocket settings
- Export/Import
  - Export analytics data
  - Backup queue configurations

## PrimeVue Components Usage

### Key Components for Dashboard
- **DataTable**: For queue lists, message browser, worker monitoring
  - Virtual scrolling for large datasets
  - Built-in sorting, filtering, pagination
  - Row expansion for details
  - Column reordering and resizing
- **Card**: For stats cards, queue cards
- **Chart**: Backup for vue-chartjs if needed
- **Sidebar**: Main navigation
- **Menu/TieredMenu**: Navigation menus
- **Dialog**: Modal windows for configuration
- **Toast**: Notifications
- **ConfirmDialog**: Confirmation prompts
- **TabView**: For analytics sections
- **Tree**: For namespace hierarchy
- **Timeline**: For message history
- **Badge/Tag**: Status indicators
- **ProgressBar**: Queue depth visualization
- **Skeleton**: Loading states
- **Panel**: Collapsible sections
- **Splitter**: Resizable panels
- **InputText/Textarea**: Forms
- **Dropdown/MultiSelect**: Filters
- **Button**: Actions with loading states
- **OverlayPanel**: Contextual info

### PrimeVue Theme Configuration
```javascript
// main.js
import 'primevue/resources/themes/lara-dark-purple/theme.css'  // or lara-dark-blue
import 'primevue/resources/primevue.min.css'
import 'primeicons/primeicons.css'
import 'primeflex/primeflex.css'
```

## UI/UX Design

### Color Palette (Lara Dark Theme + Custom)
```scss
// Background
$bg-primary: #0a0a0a;      // Main background
$bg-secondary: #141414;    // Card background
$bg-tertiary: #1f1f1f;     // Elevated elements

// Text
$text-primary: #ffffff;     // Primary text
$text-secondary: #a3a3a3;  // Secondary text
$text-muted: #737373;      // Muted text

// Brand Colors
$brand-primary: #8b5cf6;   // Purple (primary actions)
$brand-secondary: #3b82f6; // Blue (secondary)
$brand-accent: #06b6d4;    // Cyan (accent)

// Status Colors
$status-success: #10b981;  // Green (completed)
$status-warning: #f59e0b;  // Amber (processing)
$status-error: #ef4444;    // Red (failed)
$status-info: #6366f1;     // Indigo (pending)

// Chart Colors
$chart-colors: [
  '#8b5cf6', // Purple
  '#3b82f6', // Blue
  '#06b6d4', // Cyan
  '#10b981', // Green
  '#f59e0b', // Amber
  '#ef4444', // Red
  '#ec4899', // Pink
  '#6366f1'  // Indigo
];
```

### Component Design Patterns
- **Cards**: Rounded corners with subtle shadow, glass-morphism effect
- **Tables**: Alternating row colors, hover effects, sticky headers
- **Charts**: Smooth animations, gradient fills, interactive tooltips
- **Buttons**: Ghost, solid, and outline variants with hover animations
- **Badges**: Color-coded status indicators with pulse animations
- **Modals**: Backdrop blur, smooth transitions
- **Notifications**: Toast style with slide animations

### Responsive Design
- Desktop-first approach
- Collapsible sidebar for tablet/mobile
- Responsive grid layouts
- Touch-friendly controls
- Optimized chart sizes for different screens

## Real-time Features

### WebSocket Integration
```typescript
// Connection to ws://localhost:6632/ws/dashboard
interface DashboardEvent {
  event: string;
  data: any;
  timestamp: string;
}

// Subscribed events:
- message.pushed
- message.processing
- message.completed
- message.failed
- queue.created
- queue.depth
- worker.connected
- worker.disconnected
```

### Real-time Updates
- Queue depths update every 5 seconds
- Message counts update on events
- Worker status updates instantly
- Charts refresh with sliding window
- Activity feed shows events as they happen

## State Management (Pinia)

### Stores Structure
```typescript
// queues.ts
interface QueuesState {
  queues: Queue[];
  selectedQueue: Queue | null;
  loading: boolean;
  filters: QueueFilters;
}

// messages.ts
interface MessagesState {
  messages: Message[];
  selectedMessage: Message | null;
  recentActivity: ActivityEvent[];
  filters: MessageFilters;
}

// analytics.ts
interface AnalyticsState {
  throughput: ThroughputData[];
  queueDepths: QueueDepthData[];
  messageStats: MessageStats;
  timeRange: TimeRange;
}

// workers.ts
interface WorkersState {
  workers: Worker[];
  connections: number;
  lastHeartbeat: Date;
}

// settings.ts
interface SettingsState {
  apiUrl: string;
  wsUrl: string;
  refreshInterval: number;
  notifications: NotificationSettings;
  theme: ThemeSettings;
}
```

## API Integration

### API Client
```javascript
// composables/useQueenApi.js
const api = {
  // Configuration
  configure: (queue: QueueConfig) => POST('/api/v1/configure'),
  
  // Messages
  push: (items: MessageItem[]) => POST('/api/v1/push'),
  pop: (scope: PopScope, options: PopOptions) => GET('/api/v1/pop/...'),
  ack: (transactionId: string, status: string) => POST('/api/v1/ack'),
  ackBatch: (acknowledgments: Ack[]) => POST('/api/v1/ack/batch'),
  
  // Analytics
  getQueues: () => GET('/api/v1/analytics/queues'),
  getNamespaceStats: (ns: string) => GET(`/api/v1/analytics/ns/${ns}`),
  getTaskStats: (ns: string, task: string) => GET(`/api/v1/analytics/ns/${ns}/task/${task}`),
  getQueueDepths: () => GET('/api/v1/analytics/queue-depths'),
  getThroughput: () => GET('/api/v1/analytics/throughput'),
}
```

## Performance Optimizations

### Frontend Optimizations
- Virtual scrolling for large lists
- Lazy loading of chart data
- Debounced search inputs
- Memoized computed values
- Component code splitting
- Image lazy loading

### Data Management
- Pagination for large datasets
- Client-side caching with TTL
- Optimistic UI updates
- Request deduplication
- Smart polling intervals

## Development Phases

### Phase 1: Foundation (Day 1-2)
- [ ] Vue 3 + Vite project setup
- [ ] PrimeVue installation and configuration
- [ ] Lara Dark theme setup
- [ ] Vue Router setup
- [ ] Basic layout with PrimeVue components
- [ ] API client setup with axios

### Phase 2: Core Dashboard (Week 2)
- [ ] Dashboard home page
- [ ] Real-time WebSocket integration
- [ ] Basic charts with vue-chartjs
- [ ] Queue list and details pages
- [ ] State management with Pinia

### Phase 3: Message Management (Week 3)
- [ ] Message browser
- [ ] Message push/pop interface
- [ ] Message details viewer
- [ ] Batch operations
- [ ] Testing tools

### Phase 4: Analytics & Monitoring (Week 4)
- [ ] Analytics dashboard
- [ ] Advanced charts and metrics
- [ ] Worker monitoring
- [ ] Performance analysis
- [ ] Export functionality

### Phase 5: Configuration & Settings (Week 5)
- [ ] Queue configuration UI
- [ ] Settings management
- [ ] Notification system
- [ ] User preferences
- [ ] Theme customization

### Phase 6: Polish & Optimization (Week 6)
- [ ] Performance optimizations
- [ ] Responsive design refinements
- [ ] Error handling
- [ ] Loading states
- [ ] Documentation

## Security Considerations

- CORS configuration for API access
- WebSocket authentication (if added)
- Input validation on all forms
- XSS prevention in JSON viewers
- Rate limiting awareness
- Secure storage of settings

## Testing Strategy

### Unit Tests
- Component testing with Vitest
- Store testing
- Utility function testing

### E2E Tests
- Critical user flows with Playwright
- Real-time update verification
- Chart rendering tests

### Performance Tests
- Lighthouse scores
- Bundle size monitoring
- Runtime performance profiling

## Deployment

### Package.json Dependencies
```json
{
  "name": "queen-dashboard",
  "version": "1.0.0",
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "vite build",
    "preview": "vite preview"
  },
  "dependencies": {
    "vue": "^3.4.0",
    "vue-router": "^4.2.0",
    "pinia": "^2.1.0",
    "primevue": "^3.45.0",
    "primeicons": "^6.0.0",
    "primeflex": "^3.3.0",
    "axios": "^1.6.0",
    "vue-chartjs": "^5.3.0",
    "chart.js": "^4.4.0",
    "date-fns": "^3.0.0",
    "@vueuse/core": "^10.7.0"
  },
  "devDependencies": {
    "@vitejs/plugin-vue": "^5.0.0",
    "vite": "^5.0.0",
    "sass": "^1.69.0"
  }
}
```

### Environment Variables (.env)
```env
VITE_API_URL=http://localhost:6632
VITE_WS_URL=ws://localhost:6632/ws/dashboard
VITE_REFRESH_INTERVAL=5000
```

### Docker Support
- Multi-stage Dockerfile
- Nginx for static hosting
- Environment variable injection

## Success Metrics

- Page load time < 2 seconds
- Time to first meaningful paint < 1 second
- Real-time update latency < 100ms
- 60fps animations and transitions
- Lighthouse score > 90
- Zero runtime errors in production

## Future Enhancements

- User authentication and authorization
- Multi-tenant support
- Alert and notification rules
- Custom dashboards
- API key management
- Audit logging
- Mobile app (React Native/Flutter)
- Slack/Discord integrations
- Prometheus/Grafana integration
