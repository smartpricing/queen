# Queen Dashboard - Frontend Development Plan

## Overview
A modern, responsive web dashboard for the Queen V2 message queue system, built with Vue 3, PrimeVue, and Vite. The dashboard provides real-time monitoring, resource management, and analytics capabilities.

## Technology Stack
- **Framework**: Vue 3 (Composition API)
- **UI Library**: PrimeVue 4.x
- **Build Tool**: Vite
- **Module System**: ESM
- **Styling**: PrimeVue themes + custom CSS
- **Charts**: Chart.js with PrimeVue Chart component
- **HTTP Client**: Native fetch API
- **WebSocket**: Native WebSocket API
- **State Management**: Vue 3 reactive/ref (no Vuex needed for this scale)
- **Routing**: Vue Router 4
- **Icons**: PrimeIcons
- **Date Handling**: Native JavaScript Date + Intl API

## Project Structure
```
dashboard/
├── index.html
├── package.json
├── vite.config.js
├── public/
│   └── favicon.ico
└── src/
    ├── main.js                 # App entry point
    ├── App.vue                 # Root component
    ├── router.js               # Route definitions
    ├── assets/
    │   └── styles/
    │       ├── main.css        # Global styles
    │       └── variables.css   # CSS variables
    ├── components/
    │   ├── layout/
    │   │   ├── AppHeader.vue   # Top navigation bar
    │   │   ├── AppSidebar.vue  # Side navigation
    │   │   └── AppLayout.vue   # Main layout wrapper
    │   ├── charts/
    │   │   ├── ThroughputChart.vue      # Real-time throughput
    │   │   ├── QueueDepthChart.vue      # Queue depths over time
    │   │   ├── MessageStatusChart.vue   # Message status distribution
    │   │   └── LatencyChart.vue         # Processing latency
    │   ├── cards/
    │   │   ├── MetricCard.vue          # Single metric display
    │   │   ├── QueueCard.vue           # Queue summary card
    │   │   └── SystemHealthCard.vue    # System health indicator
    │   └── common/
    │       ├── DataTable.vue           # Reusable data table
    │       ├── LoadingSpinner.vue      # Loading indicator
    │       └── ErrorMessage.vue        # Error display
    ├── views/
    │   ├── Dashboard.vue               # Main dashboard
    │   ├── Queues.vue                  # Queue management
    │   ├── QueueDetail.vue             # Single queue details
    │   ├── Partitions.vue              # Partition management
    │   ├── Analytics.vue               # Analytics & metrics
    │   ├── Messages.vue                # Message browser
    │   └── System.vue                  # System overview
    ├── services/
    │   ├── api.js                      # API client
    │   ├── websocket.js                # WebSocket manager
    │   └── formatter.js                # Data formatters
    └── utils/
        ├── constants.js                # App constants
        ├── colors.js                   # Chart colors
        └── helpers.js                  # Utility functions
```

## Core Features

### 1. Dashboard View
**Purpose**: Real-time system overview and key metrics

**Components**:
- **System Health Card**: Connection status, uptime, active connections
- **Message Metrics Cards**: 
  - Total messages (with trend)
  - Pending messages
  - Processing messages
  - Completed today
  - Failed today
  - Messages/second rate
- **Throughput Chart**: Line chart showing incoming/completed/failed rates over last hour
- **Queue Depth Chart**: Stacked bar chart of top 10 queues by depth
- **Recent Activity Feed**: Live feed of recent message events via WebSocket
- **Quick Actions**: Buttons for common tasks

**Data Sources**:
- `/health` - System health
- `/api/v1/analytics/throughput` - Throughput metrics
- `/api/v1/analytics/queue-depths` - Queue depths
- `/api/v1/resources/overview` - System overview
- WebSocket events for real-time updates

### 2. Resource Management

#### 2.1 Queues View
**Purpose**: Manage and monitor all queues

**Features**:
- **Queue List Table**:
  - Queue name
  - Namespace/Task tags
  - Partition count
  - Total messages
  - Pending/Processing counts
  - Created date
  - Actions (View, Configure, Clear)
- **Filters**: By namespace, task, or search
- **Sorting**: By name, depth, created date
- **Bulk Actions**: Clear multiple queues

**Data Sources**:
- `/api/v1/resources/queues` - Queue list
- `/api/v1/analytics/queues` - Queue statistics

#### 2.2 Queue Detail View
**Purpose**: Detailed view of a single queue

**Features**:
- **Queue Info Card**: Name, namespace, task, created date
- **Partition List**: 
  - Partition name
  - Priority
  - Message counts by status
  - Configuration (lease time, retry limit)
  - Actions (Configure, Clear)
- **Message Status Pie Chart**: Distribution of message states
- **Recent Messages Table**: Last 20 messages with status
- **Configuration Panel**: Edit queue/partition settings

**Data Sources**:
- `/api/v1/resources/queues/:queue` - Queue details
- `/api/v1/messages?queue=:queue` - Recent messages
- `/api/v1/configure` - Update configuration

#### 2.3 Partitions View
**Purpose**: Global partition management

**Features**:
- **Partition Table**:
  - Queue/Partition path
  - Priority
  - Depth
  - Processing count
  - Configuration summary
- **Filter by Queue**: Dropdown to filter partitions
- **Sort by Priority/Depth**: Quick sorting options

**Data Sources**:
- `/api/v1/resources/partitions` - All partitions

### 3. Analytics View
**Purpose**: Detailed performance metrics and trends

**Features**:
- **Time Range Selector**: Last hour, 6 hours, 24 hours, 7 days
- **Multi-Metric Chart**: 
  - Incoming rate
  - Completion rate
  - Failure rate
  - Processing lag
- **Queue Performance Table**:
  - Queue name
  - Throughput (msg/min)
  - Average processing time
  - Success rate
  - Error rate
- **System Performance Cards**:
  - Database pool usage
  - Memory usage
  - CPU usage
  - Request rate
- **Top Queues by Volume**: Bar chart
- **Top Queues by Errors**: Bar chart

**Data Sources**:
- `/api/v1/analytics/throughput` - Throughput data
- `/api/v1/analytics/queue-stats` - Queue statistics
- `/metrics` - System metrics

### 4. Messages View
**Purpose**: Browse and manage individual messages

**Features**:
- **Message Search**:
  - By queue
  - By partition
  - By status
  - By date range
- **Message Table**:
  - Transaction ID
  - Queue/Partition
  - Status with color coding
  - Created/Completed times
  - Retry count
  - Actions (View, Retry, Delete, Move to DLQ)
- **Message Detail Modal**:
  - Full payload JSON viewer
  - Complete timeline
  - Error details if failed
  - Related messages

**Data Sources**:
- `/api/v1/messages` - Message list
- `/api/v1/messages/:id` - Message details
- `/api/v1/messages/:id/related` - Related messages

## UI/UX Design Principles

### Visual Design
1. **Color Scheme**: 
   - Primary: PrimeVue Aura theme (blue/indigo)
   - Success: Green (#10b981)
   - Warning: Amber (#f59e0b)
   - Error: Red (#ef4444)
   - Neutral: Gray scale

2. **Typography**:
   - Headers: Inter or system font
   - Body: System font stack
   - Monospace: For IDs and code

3. **Layout**:
   - Fixed header with navigation
   - Collapsible sidebar for mobile
   - Content area with consistent padding
   - Card-based component layout

4. **Animations**:
   - Smooth transitions for route changes
   - Subtle hover effects
   - Loading skeletons for data fetching
   - Real-time data updates with fade effects

### Responsive Design
- **Desktop** (>1024px): Full sidebar, multi-column layouts
- **Tablet** (768-1024px): Collapsible sidebar, adjusted grids
- **Mobile** (<768px): Bottom navigation, single column, simplified charts

### Accessibility
- Proper ARIA labels
- Keyboard navigation support
- High contrast mode support
- Screen reader friendly

## Real-time Features

### WebSocket Integration
```javascript
// Connection management
- Auto-reconnect on disconnect
- Heartbeat/ping-pong
- Event subscription system

// Event handling
- Queue depth updates (every 5s)
- Message state changes
- System statistics (every 10s)
- Visual indicators for real-time data
```

### Data Refresh Strategy
- Dashboard: Real-time via WebSocket
- Resource views: Refresh on focus + manual refresh
- Analytics: Auto-refresh every 30s
- Messages: Manual refresh only

## Performance Optimizations

1. **Code Splitting**: 
   - Lazy load routes
   - Dynamic import for charts
   
2. **Data Management**:
   - Pagination for large datasets
   - Virtual scrolling for long lists
   - Debounced search inputs
   
3. **Caching**:
   - Cache static resources
   - Short-term cache for analytics data
   
4. **Bundle Size**:
   - Tree-shake PrimeVue components
   - Minimize external dependencies

## Implementation Phases

### Phase 1: Foundation (Current Focus)
1. ✅ Project setup with Vite
2. ✅ PrimeVue integration
3. ✅ Basic routing structure
4. ✅ Layout components
5. ✅ API service layer

### Phase 2: Core Views
1. ✅ Dashboard with basic metrics
2. ✅ Queue list and management
3. ✅ Basic analytics view
4. ✅ WebSocket connection

### Phase 3: Enhanced Features
1. ⏳ Advanced charts and visualizations
2. ⏳ Message browser and management
3. ⏳ Partition configuration UI
4. ⏳ System health monitoring

### Phase 4: Polish
1. ⏳ Responsive design refinements
2. ⏳ Dark mode support
3. ⏳ Export functionality
4. ⏳ User preferences

## API Integration Patterns

### Error Handling
```javascript
// Consistent error handling
try {
  const data = await api.get('/endpoint')
} catch (error) {
  toast.add({
    severity: 'error',
    summary: 'Error',
    detail: error.message
  })
}
```

### Loading States
- Show skeleton loaders during fetch
- Disable buttons during operations
- Progress bars for long operations

### Data Formatting
- Human-readable timestamps
- Number formatting (1.2K, 3.4M)
- Status badges with colors
- Percentage calculations

## Development Guidelines

### Component Structure
- Use Composition API with `<script setup>`
- Props validation with type definitions
- Emit events for parent communication
- Scoped styles with CSS modules where needed

### State Management
- Local state with ref/reactive
- Provide/inject for cross-component state
- No global store needed initially

### Code Style
- ESLint for code quality
- Prettier for formatting
- Meaningful variable names
- Comments for complex logic

## Testing Approach
- Manual testing for UI/UX
- API mocking for development
- Browser compatibility testing
- Performance profiling with Lighthouse

## Deployment
- Build with Vite: `npm run build`
- Serve static files from `/dashboard` path
- Configure server to proxy API calls
- Enable gzip compression

## Future Enhancements
- User authentication and authorization
- Custom alerting and notifications
- Data export (CSV, JSON)
- Saved filters and views
- Dashboard customization
- Mobile app consideration
