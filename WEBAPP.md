# Queen Message Queue - Web Application Design

**Technology Stack:**
- Vue 3 (Composition API)
- JavaScript (ES6+)
- Tailwind CSS
- Chart.js with vue-chartjs
- Vue Router
- Axios for API calls

**Design Philosophy:**
- Clean, professional interface inspired by Kafka UI and modern analytics dashboards
- Real-time data from API endpoints (no mocked data)
- Responsive design with sidebar navigation
- Light and dark mode support
- Clear data visualization with meaningful charts
- Intuitive user interactions

---

## Table of Contents

1. [Application Structure](#application-structure)
2. [Design System](#design-system)
3. [Pages & Components](#pages--components)
4. [API Integration Map](#api-integration-map)
5. [File Structure](#file-structure)
6. [Implementation Plan](#implementation-plan)

---

## Application Structure

### Core Layout

```
┌────────────────────────────────────────────────────┐
│  Header (Logo, Health Status, Theme Toggle)       │
├──────────┬─────────────────────────────────────────┤
│          │                                         │
│ Sidebar  │         Main Content Area              │
│          │                                         │
│ - Dashboard                                        │
│ - Queues │         [Dynamic Page Content]         │
│ - Consumer                                         │
│   Groups │                                         │
│ - Messages                                         │
│ - Analytics                                        │
│          │                                         │
│          │                                         │
└──────────┴─────────────────────────────────────────┘
```

### Pages Overview

1. **Dashboard** - System overview with key metrics
2. **Queues** - Queue list and management
3. **Queue Detail** - Individual queue deep-dive
4. **Consumer Groups** - Consumer group monitoring
5. **Messages** - Message browser and operations
6. **Analytics** - Time-series analysis and insights

---

## Design System

### Color Palette

**Light Mode:**
```css
--bg-primary: #ffffff
--bg-secondary: #f9fafb
--bg-tertiary: #f3f4f6
--text-primary: #111827
--text-secondary: #6b7280
--text-tertiary: #9ca3af
--border-color: #e5e7eb
--accent-blue: #3b82f6
--accent-indigo: #6366f1
--accent-purple: #8b5cf6
--success: #10b981
--warning: #f59e0b
--danger: #ef4444
```

**Dark Mode:**
```css
--bg-primary: #0f172a
--bg-secondary: #1e293b
--bg-tertiary: #334155
--text-primary: #f1f5f9
--text-secondary: #cbd5e1
--text-tertiary: #94a3b8
--border-color: #334155
--accent-blue: #60a5fa
--accent-indigo: #818cf8
--accent-purple: #a78bfa
--success: #34d399
--warning: #fbbf24
--danger: #f87171
```

### Typography

```css
--font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif
--font-mono: 'JetBrains Mono', 'Courier New', monospace

--text-xs: 0.75rem (12px)
--text-sm: 0.875rem (14px)
--text-base: 1rem (16px)
--text-lg: 1.125rem (18px)
--text-xl: 1.25rem (20px)
--text-2xl: 1.5rem (24px)
--text-3xl: 1.875rem (30px)
```

### Spacing System

Following Tailwind's 4px base unit:
- `xs`: 4px
- `sm`: 8px
- `md`: 16px
- `lg`: 24px
- `xl`: 32px
- `2xl`: 48px

### Component Patterns

**Cards:**
- White/dark background with subtle shadow
- Rounded corners (8px)
- Padding: 24px
- Border: 1px solid border-color

**Buttons:**
- Primary: Blue background, white text
- Secondary: Gray background, dark text
- Danger: Red background, white text
- Height: 40px, padding: 12px 24px
- Rounded: 6px

**Tables:**
- Alternating row colors
- Hover state on rows
- Sortable columns with indicators
- Sticky header

**Status Badges:**
- Rounded pill shape
- Color-coded: green (healthy/stable), yellow (warning), red (error)
- Small text with icon

---

## Pages & Components

### 1. Dashboard (`/`)

**Purpose:** System overview showing health, throughput, and key metrics at a glance.

**API Routes Used:**
- `GET /health` - System health status
- `GET /metrics` - Performance metrics
- `GET /api/v1/resources/overview` - System-wide statistics
- `GET /api/v1/status` - Dashboard status with throughput

**Layout:**

```
┌─────────────────────────────────────────────────────────────────┐
│  Queen Message Queue                    ● Healthy    🌙          │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌─────────┐│
│  │   QUEUES     │ │  PARTITIONS  │ │   MESSAGES   │ │ UPTIME  ││
│  │      53      │ │     512      │ │   200,000    │ │  2h 15m ││
│  └──────────────┘ └──────────────┘ └──────────────┘ └─────────┘│
│                                                                   │
│  ┌──────────────────────────────────────────────────────────────┐│
│  │ Message Throughput (Last Hour)                               ││
│  │                                                               ││
│  │     Chart: Line chart showing ingested/processed rates       ││
│  │     X-axis: Time, Y-axis: Messages/second                    ││
│  │     Two lines: Ingested (blue), Processed (green)            ││
│  │                                                               ││
│  └──────────────────────────────────────────────────────────────┘│
│                                                                   │
│  ┌────────────────────────────┐ ┌───────────────────────────────┐│
│  │ Message Status             │ │ System Performance            ││
│  │                            │ │                               ││
│  │ Pending:      2            │ │ Requests/sec:    0.00        ││
│  │ Processing:   0            │ │ Messages/sec:    0.00        ││
│  │ Completed:    16,493       │ │ DB Connections:  3/3         ││
│  │ Failed:       0            │ │ Memory (Heap):   7.7 MB      ││
│  │ Dead Letter:  0            │ │ CPU Time:        227ms       ││
│  │                            │ │                               ││
│  └────────────────────────────┘ └───────────────────────────────┘│
│                                                                   │
│  ┌──────────────────────────────────────────────────────────────┐│
│  │ Top Queues by Activity                                       ││
│  │                                                               ││
│  │  Queue Name             Pending    Processing    Completed   ││
│  │  ───────────────────────────────────────────────────────────││
│  │  benchmark-queue-50       0           0           2,000      ││
│  │  benchmark-queue-49       0           0           2,000      ││
│  │  test-queue               2           0           3          ││
│  │                                                               ││
│  └──────────────────────────────────────────────────────────────┘│
│                                                                   │
└─────────────────────────────────────────────────────────────────┘
```

**Components:**

1. **HealthIndicator.vue**
   - Shows server health status from `/health`
   - Color-coded dot: green (healthy), red (unhealthy)
   - Displays uptime

2. **MetricCard.vue**
   - Reusable card for displaying single metrics
   - Props: title, value, icon, trend (optional)
   - Used for queues, partitions, messages, uptime

3. **ThroughputChart.vue**
   - Line chart using Chart.js
   - Data from `/api/v1/status` throughput array
   - Two datasets: ingested and processed messages

4. **MessageStatusCard.vue**
   - Displays message counts by status
   - Data from `/api/v1/resources/overview`

5. **PerformanceCard.vue**
   - Shows system performance metrics
   - Data from `/metrics` endpoint

6. **TopQueuesTable.vue**
   - Simple table showing top 5 queues
   - Data from `/api/v1/resources/queues`
   - Sortable by activity

---

### 2. Queues Page (`/queues`)

**Purpose:** List all queues with filtering, sorting, and management capabilities.

**API Routes Used:**
- `GET /api/v1/resources/queues` - Get all queues
- `GET /api/v1/resources/namespaces` - Get namespaces for filtering
- `POST /api/v1/configure` - Create new queue (modal)
- `DELETE /api/v1/resources/queues/:queue` - Delete queue

**Layout:**

```
┌─────────────────────────────────────────────────────────────────┐
│  Queues                                        [+ New Queue]     │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  🔍 Search queues...    [Namespace ▼]  [Clear Filters]          │
│                                                                   │
│  ┌──────────────────────────────────────────────────────────────┐│
│  │ Queue Name ▲  Namespace  Partitions  Pending  Processing  Total││
│  ├──────────────────────────────────────────────────────────────┤│
│  │ test-queue      -         1          2         0         0   ││
│  │ benchmark-q-50  benchmark 10         0         0      2,000  ││
│  │ benchmark-q-49  benchmark 10         0         0      2,000  ││
│  │ benchmark-q-48  benchmark 10         0         0      2,000  ││
│  │ ...                                                           ││
│  └──────────────────────────────────────────────────────────────┘│
│                                                                   │
│  Showing 1-20 of 53                              [< 1 2 3 4 >]   │
│                                                                   │
└─────────────────────────────────────────────────────────────────┘
```

**Components:**

1. **QueueList.vue**
   - Main component for queues page
   - Handles filtering, sorting, pagination
   - Data from `/api/v1/resources/queues`

2. **QueueRow.vue**
   - Single row in the queue table
   - Click to navigate to queue detail
   - Shows queue statistics
   - Action menu (delete, clear)

3. **CreateQueueModal.vue**
   - Modal form for creating new queue
   - Calls `POST /api/v1/configure`
   - Fields: name, namespace, task, ttl, priority, partition

4. **FilterBar.vue**
   - Search input
   - Namespace dropdown (from `/api/v1/resources/namespaces`)
   - Clear filters button

---

### 3. Queue Detail Page (`/queues/:queueName`)

**Purpose:** Deep dive into a single queue with partition details, messages, and performance.

**API Routes Used:**
- `GET /api/v1/resources/queues/:queue` - Queue details
- `GET /api/v1/status/queues/:queue` - Queue status with time range
- `GET /api/v1/status/queues/:queue/messages` - Queue messages
- `DELETE /api/v1/queues/:queue/clear` - Clear queue
- `POST /api/v1/push` - Push message to queue

**Layout:**

```
┌─────────────────────────────────────────────────────────────────┐
│  ← Queues / test-queue                          [Clear Queue]   │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  test-queue                              Created: Oct 15, 2025   │
│  Namespace: -  Task: -  Priority: 0                              │
│                                                                   │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌─────────┐│
│  │  PENDING     │ │ PROCESSING   │ │  COMPLETED   │ │ FAILED  ││
│  │      0       │ │      0       │ │      3       │ │    0    ││
│  └──────────────┘ └──────────────┘ └──────────────┘ └─────────┘│
│                                                                   │
│  ┌──────────────────────────────────────────────────────────────┐│
│  │ Queue Configuration                                          ││
│  │                                                               ││
│  │  Lease Time:      300s         Max Queue Size:  10,000       ││
│  │  TTL:            3,600s        Retry Limit:     3            ││
│  │  Retry Delay:    1,000ms       DLQ Enabled:     No           ││
│  │                                                               ││
│  └──────────────────────────────────────────────────────────────┘│
│                                                                   │
│  ┌──────────────────────────────────────────────────────────────┐│
│  │ Partitions                                [Push Message]     ││
│  ├──────────────────────────────────────────────────────────────┤│
│  │ Default                                                       ││
│  │   Pending: 0  Processing: 0  Completed: 3  Failed: 0        ││
│  │   Last Activity: Oct 15, 06:22:21                            ││
│  │   Consumed: 3 messages in 2 batches                          ││
│  └──────────────────────────────────────────────────────────────┘│
│                                                                   │
│  ┌──────────────────────────────────────────────────────────────┐│
│  │ Recent Messages                                              ││
│  ├──────────────────────────────────────────────────────────────┤│
│  │ Transaction ID                           Created      Status ││
│  │ ────────────────────────────────────────────────────────────││
│  │ 0199e688-1857...                     06:21:42     Completed ││
│  │ 0199e688-4d29...                     06:22:01     Completed ││
│  │                                                               ││
│  └──────────────────────────────────────────────────────────────┘│
│                                                                   │
└─────────────────────────────────────────────────────────────────┘
```

**Components:**

1. **QueueDetailHeader.vue**
   - Queue name and metadata
   - Back button
   - Action buttons (clear, delete)

2. **QueueMetrics.vue**
   - Four metric cards for message status
   - Data from `/api/v1/status/queues/:queue`

3. **QueueConfig.vue**
   - Display queue configuration
   - Grid layout for config values

4. **PartitionList.vue**
   - List of partitions with stats
   - Data from queue detail response

5. **RecentMessages.vue**
   - Table of recent messages
   - Data from `/api/v1/status/queues/:queue/messages`
   - Link to full message view

6. **PushMessageModal.vue**
   - Form to push new message
   - JSON editor for payload
   - Calls `POST /api/v1/push`

---

### 4. Consumer Groups Page (`/consumer-groups`)

**Purpose:** Monitor consumer groups, lag, and consumption patterns.

**API Routes Used:**
- `GET /api/v1/resources/queues` - Get queues with consumer info
- `GET /api/v1/status/queues` - Get detailed consumer stats

**Layout:**

```
┌─────────────────────────────────────────────────────────────────┐
│  Consumer Groups                                                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  🔍 Search groups...    [Status ▼]  [Clear Filters]             │
│                                                                   │
│  ┌──────────────────────────────────────────────────────────────┐│
│  │ Group Name ▲       Topics         Offset Lag   Time Lag  State││
│  ├──────────────────────────────────────────────────────────────┤│
│  │ worker-group-1     test-queue        0          0ms     ● Stable││
│  │ consumer-default   benchmark-q-01    1,234      5m 23s  ● Stable││
│  │ batch-processor    batch-queue       45         12s     ● Stable││
│  │ ...                                                           ││
│  └──────────────────────────────────────────────────────────────┘│
│                                                                   │
└─────────────────────────────────────────────────────────────────┘
```

**Components:**

1. **ConsumerGroupList.vue**
   - Table of consumer groups
   - Derived from queue data showing active consumers
   - Filtering and sorting

2. **ConsumerGroupRow.vue**
   - Single consumer group entry
   - Status badge
   - Lag indicators

---

### 5. Messages Page (`/messages`)

**Purpose:** Browse, search, and manage individual messages across all queues.

**API Routes Used:**
- `GET /api/v1/messages` - List messages with filters
- `GET /api/v1/messages/:transactionId` - Get message detail
- `DELETE /api/v1/messages/:transactionId` - Delete message
- `POST /api/v1/messages/:transactionId/retry` - Retry message
- `POST /api/v1/messages/:transactionId/dlq` - Move to DLQ

**Layout:**

```
┌─────────────────────────────────────────────────────────────────┐
│  Messages                                                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  🔍 Search by ID...  [Queue ▼]  [Status ▼]  [Clear Filters]    │
│                                                                   │
│  ┌──────────────────────────────────────────────────────────────┐│
│  │ Transaction ID         Queue        Created        Status    ││
│  ├──────────────────────────────────────────────────────────────┤│
│  │ 0199e688-1857...       test-queue   Oct 15 06:21  Completed ││
│  │ 0199e688-4d29...       test-queue   Oct 15 06:22  Completed ││
│  │ ...                                                           ││
│  └──────────────────────────────────────────────────────────────┘│
│                                                                   │
│  [Message Detail Panel appears on row click]                     │
│                                                                   │
└─────────────────────────────────────────────────────────────────┘
```

**Components:**

1. **MessageList.vue**
   - Table of messages
   - Click to show detail panel
   - Pagination

2. **MessageDetailPanel.vue**
   - Slide-out panel from right
   - Shows full message data
   - JSON payload viewer
   - Action buttons (retry, delete, move to DLQ)

3. **MessageFilters.vue**
   - Filter bar with multiple options
   - Queue selector
   - Status selector

---

### 6. Analytics Page (`/analytics`)

**Purpose:** Time-series analysis, trends, and system insights.

**API Routes Used:**
- `GET /api/v1/status/analytics` - Analytics data with time intervals
- `GET /api/v1/status` - Status data for time range

**Layout:**

```
┌─────────────────────────────────────────────────────────────────┐
│  Analytics                                                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  [Last Hour ▼]  [All Queues ▼]  [Refresh]                      │
│                                                                   │
│  ┌──────────────────────────────────────────────────────────────┐│
│  │ Message Flow (Time Series)                                   ││
│  │                                                               ││
│  │     Stacked Area Chart:                                       ││
│  │     - Ingested (blue area)                                    ││
│  │     - Processed (green area)                                  ││
│  │     - Failed (red line)                                       ││
│  │                                                               ││
│  └──────────────────────────────────────────────────────────────┘│
│                                                                   │
│  ┌────────────────────────────┐ ┌───────────────────────────────┐│
│  │ Top Queues by Volume       │ │ Message Status Distribution   ││
│  │                            │ │                               ││
│  │ Bar Chart:                 │ │ Doughnut Chart:               ││
│  │ Queue names on X-axis      │ │ - Pending (yellow)            ││
│  │ Message count on Y-axis    │ │ - Processing (blue)           ││
│  │                            │ │ - Completed (green)           ││
│  │                            │ │ - Failed (red)                ││
│  │                            │ │ - DLQ (gray)                  ││
│  │                            │ │                               ││
│  └────────────────────────────┘ └───────────────────────────────┘│
│                                                                   │
│  ┌──────────────────────────────────────────────────────────────┐│
│  │ Performance Metrics                                          ││
│  │                                                               ││
│  │ ┌────────────────┐ ┌────────────────┐ ┌────────────────┐   ││
│  │ │ Avg Latency    │ │ Throughput     │ │ Error Rate     │   ││
│  │ │    125ms       │ │  1,234 msg/s   │ │    0.01%       │   ││
│  │ └────────────────┘ └────────────────┘ └────────────────┘   ││
│  │                                                               ││
│  └──────────────────────────────────────────────────────────────┘│
│                                                                   │
└─────────────────────────────────────────────────────────────────┘
```

**Components:**

1. **TimeRangeSelector.vue**
   - Dropdown for time range selection
   - Options: Last hour, 6 hours, 24 hours, 7 days

2. **MessageFlowChart.vue**
   - Stacked area or line chart
   - Data from `/api/v1/status/analytics`

3. **TopQueuesChart.vue**
   - Horizontal bar chart
   - Shows top queues by message volume

4. **MessageDistributionChart.vue**
   - Doughnut chart showing status distribution
   - Color-coded segments

5. **PerformanceMetrics.vue**
   - Three metric cards
   - Calculated from analytics data

---

## API Integration Map

### Component to API Endpoint Mapping

| Component | API Endpoint | Method | Purpose |
|-----------|-------------|--------|---------|
| HealthIndicator | `/health` | GET | Server health check |
| MetricCard (Dashboard) | `/api/v1/resources/overview` | GET | System metrics |
| ThroughputChart | `/api/v1/status` | GET | Throughput data |
| PerformanceCard | `/metrics` | GET | System performance |
| QueueList | `/api/v1/resources/queues` | GET | List all queues |
| QueueList | `/api/v1/resources/namespaces` | GET | Filter namespaces |
| CreateQueueModal | `/api/v1/configure` | POST | Create queue |
| QueueRow | `/api/v1/resources/queues/:queue` | DELETE | Delete queue |
| QueueDetailHeader | `/api/v1/resources/queues/:queue` | GET | Queue details |
| QueueMetrics | `/api/v1/status/queues/:queue` | GET | Queue status |
| RecentMessages | `/api/v1/status/queues/:queue/messages` | GET | Queue messages |
| PushMessageModal | `/api/v1/push` | POST | Push message |
| ClearQueueAction | `/api/v1/queues/:queue/clear` | DELETE | Clear queue |
| MessageList | `/api/v1/messages` | GET | List messages |
| MessageDetailPanel | `/api/v1/messages/:transactionId` | GET | Message detail |
| MessageDetailPanel | `/api/v1/messages/:transactionId` | DELETE | Delete message |
| MessageDetailPanel | `/api/v1/messages/:transactionId/retry` | POST | Retry message |
| MessageDetailPanel | `/api/v1/messages/:transactionId/dlq` | POST | Move to DLQ |
| ConsumerGroupList | `/api/v1/resources/queues` | GET | Consumer info |
| MessageFlowChart | `/api/v1/status/analytics` | GET | Analytics data |
| TopQueuesChart | `/api/v1/status/analytics` | GET | Queue metrics |

---

## File Structure

```
webapp/
├── public/
│   ├── index.html
│   └── assets/
│       ├── queen-logo.svg
│       ├── queen-logo-blue.svg
│       ├── queen-logo-cyan.svg
│       ├── queen-logo-indigo.svg
│       ├── queen-logo-orange.svg
│       ├── queen-logo-pink.svg
│       ├── queen-logo-purple.svg
│       ├── queen-logo-rose.svg
│       └── favicon.svg
│
├── src/
│   ├── main.js                    # App entry point
│   ├── App.vue                    # Root component with layout
│   │
│   ├── router/
│   │   └── index.js               # Vue Router configuration
│   │
│   ├── api/
│   │   ├── client.js              # Axios instance with base config
│   │   ├── health.js              # Health & metrics endpoints
│   │   ├── queues.js              # Queue management endpoints
│   │   ├── messages.js            # Message operations endpoints
│   │   ├── resources.js           # Resource query endpoints
│   │   └── analytics.js           # Analytics endpoints
│   │
│   ├── composables/
│   │   ├── useTheme.js            # Dark mode toggle
│   │   ├── useApi.js              # API call wrapper with error handling
│   │   └── usePolling.js          # Manual refresh helper
│   │
│   ├── components/
│   │   ├── layout/
│   │   │   ├── AppLayout.vue      # Main layout with sidebar
│   │   │   ├── AppHeader.vue      # Header with logo and controls
│   │   │   ├── AppSidebar.vue     # Sidebar navigation
│   │   │   └── ThemeToggle.vue    # Light/dark theme switcher
│   │   │
│   │   ├── common/
│   │   │   ├── MetricCard.vue     # Reusable metric display card
│   │   │   ├── StatusBadge.vue    # Status indicator badge
│   │   │   ├── DataTable.vue      # Generic data table
│   │   │   ├── LoadingSpinner.vue # Loading state
│   │   │   ├── EmptyState.vue     # Empty state placeholder
│   │   │   ├── ConfirmDialog.vue  # Confirmation modal
│   │   │   └── RefreshButton.vue  # Manual refresh button
│   │   │
│   │   ├── dashboard/
│   │   │   ├── HealthIndicator.vue
│   │   │   ├── ThroughputChart.vue
│   │   │   ├── MessageStatusCard.vue
│   │   │   ├── PerformanceCard.vue
│   │   │   └── TopQueuesTable.vue
│   │   │
│   │   ├── queues/
│   │   │   ├── QueueList.vue
│   │   │   ├── QueueRow.vue
│   │   │   ├── QueueFilters.vue
│   │   │   └── CreateQueueModal.vue
│   │   │
│   │   ├── queue-detail/
│   │   │   ├── QueueDetailHeader.vue
│   │   │   ├── QueueMetrics.vue
│   │   │   ├── QueueConfig.vue
│   │   │   ├── PartitionList.vue
│   │   │   ├── RecentMessages.vue
│   │   │   └── PushMessageModal.vue
│   │   │
│   │   ├── messages/
│   │   │   ├── MessageList.vue
│   │   │   ├── MessageDetailPanel.vue
│   │   │   ├── MessageFilters.vue
│   │   │   └── JsonViewer.vue
│   │   │
│   │   ├── consumer-groups/
│   │   │   ├── ConsumerGroupList.vue
│   │   │   └── ConsumerGroupRow.vue
│   │   │
│   │   └── analytics/
│   │       ├── TimeRangeSelector.vue
│   │       ├── MessageFlowChart.vue
│   │       ├── TopQueuesChart.vue
│   │       ├── MessageDistributionChart.vue
│   │       └── PerformanceMetrics.vue
│   │
│   ├── views/
│   │   ├── Dashboard.vue          # Dashboard page
│   │   ├── Queues.vue             # Queues list page
│   │   ├── QueueDetail.vue        # Queue detail page
│   │   ├── Messages.vue           # Messages page
│   │   ├── ConsumerGroups.vue     # Consumer groups page
│   │   └── Analytics.vue          # Analytics page
│   │
│   ├── utils/
│   │   ├── formatters.js          # Number, date, size formatters
│   │   ├── constants.js           # App constants
│   │   └── colors.js              # Color scheme definitions
│   │
│   └── assets/
│       └── styles/
│           ├── main.css            # Global styles
│           └── tailwind.css        # Tailwind imports
│
├── package.json
├── tailwind.config.js
├── vite.config.js
├── postcss.config.js
└── README.md
```

---

## Implementation Plan

### Phase 1: Project Setup & Core Infrastructure (Day 1)

**1.1 Initialize Project**
```bash
npm create vite@latest webapp -- --template vue
cd webapp
npm install
```

**1.2 Install Dependencies**
```bash
npm install vue-router axios chart.js vue-chartjs
npm install -D tailwindcss postcss autoprefixer
npx tailwindcss init -p
```

**1.3 Configure Tailwind CSS**

`tailwind.config.js`:
```javascript
export default {
  content: [
    "./index.html",
    "./src/**/*.{vue,js}",
  ],
  darkMode: 'class',
  theme: {
    extend: {
      colors: {
        'queen-blue': '#3b82f6',
        'queen-indigo': '#6366f1',
        'queen-purple': '#8b5cf6',
      },
      fontFamily: {
        sans: ['Inter', 'sans-serif'],
        mono: ['JetBrains Mono', 'monospace'],
      },
    },
  },
  plugins: [],
}
```

**1.4 Setup Base Files**

`src/assets/styles/main.css`:
```css
@import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap');
@import url('https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;500&display=swap');

@tailwind base;
@tailwind components;
@tailwind utilities;

@layer base {
  * {
    @apply border-gray-200 dark:border-gray-700;
  }
  
  body {
    @apply bg-white dark:bg-slate-900 text-gray-900 dark:text-gray-100;
  }
}

@layer components {
  .card {
    @apply bg-white dark:bg-slate-800 rounded-lg shadow-sm border p-6;
  }
  
  .btn {
    @apply px-6 py-2.5 rounded-md font-medium transition-colors;
  }
  
  .btn-primary {
    @apply bg-blue-600 hover:bg-blue-700 text-white;
  }
  
  .btn-secondary {
    @apply bg-gray-100 hover:bg-gray-200 dark:bg-gray-800 dark:hover:bg-gray-700 text-gray-900 dark:text-gray-100;
  }
  
  .btn-danger {
    @apply bg-red-600 hover:bg-red-700 text-white;
  }
  
  .badge {
    @apply inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-medium;
  }
  
  .badge-success {
    @apply bg-green-100 text-green-800 dark:bg-green-900 dark:text-green-200;
  }
  
  .badge-warning {
    @apply bg-yellow-100 text-yellow-800 dark:bg-yellow-900 dark:text-yellow-200;
  }
  
  .badge-danger {
    @apply bg-red-100 text-red-800 dark:bg-red-900 dark:text-red-200;
  }
  
  .input {
    @apply w-full px-4 py-2 border rounded-md bg-white dark:bg-slate-800 focus:outline-none focus:ring-2 focus:ring-blue-500;
  }
}
```

**1.5 Create API Client**

`src/api/client.js`:
```javascript
import axios from 'axios';

const API_BASE_URL = import.meta.env.VITE_API_BASE_URL || 'http://localhost:6632';

const apiClient = axios.create({
  baseURL: API_BASE_URL,
  timeout: 30000,
  headers: {
    'Content-Type': 'application/json',
  },
});

// Response interceptor for error handling
apiClient.interceptors.response.use(
  (response) => response,
  (error) => {
    if (error.response) {
      // Server responded with error
      console.error('API Error:', error.response.data);
    } else if (error.request) {
      // Request made but no response
      console.error('Network Error:', error.message);
    } else {
      console.error('Error:', error.message);
    }
    return Promise.reject(error);
  }
);

export default apiClient;
```

**1.6 Create API Modules**

`src/api/health.js`:
```javascript
import apiClient from './client';

export const healthApi = {
  getHealth: () => apiClient.get('/health'),
  getMetrics: () => apiClient.get('/metrics'),
};
```

`src/api/queues.js`:
```javascript
import apiClient from './client';

export const queuesApi = {
  getQueues: (params) => apiClient.get('/api/v1/resources/queues', { params }),
  getQueue: (queueName) => apiClient.get(`/api/v1/resources/queues/${queueName}`),
  deleteQueue: (queueName) => apiClient.delete(`/api/v1/resources/queues/${queueName}`),
  clearQueue: (queueName, partition) => {
    const params = partition ? { partition } : {};
    return apiClient.delete(`/api/v1/queues/${queueName}/clear`, { params });
  },
  configureQueue: (data) => apiClient.post('/api/v1/configure', data),
  
  // Partition operations
  getPartitions: (params) => apiClient.get('/api/v1/resources/partitions', { params }),
};
```

`src/api/messages.js`:
```javascript
import apiClient from './client';

export const messagesApi = {
  getMessages: (params) => apiClient.get('/api/v1/messages', { params }),
  getMessage: (transactionId) => apiClient.get(`/api/v1/messages/${transactionId}`),
  deleteMessage: (transactionId) => apiClient.delete(`/api/v1/messages/${transactionId}`),
  retryMessage: (transactionId) => apiClient.post(`/api/v1/messages/${transactionId}/retry`),
  moveToDLQ: (transactionId) => apiClient.post(`/api/v1/messages/${transactionId}/dlq`),
  getRelatedMessages: (transactionId) => apiClient.get(`/api/v1/messages/${transactionId}/related`),
  
  // Push messages
  pushMessages: (data) => apiClient.post('/api/v1/push', data),
  
  // Pop messages
  popMessages: (queue, partition, params) => {
    if (partition) {
      return apiClient.get(`/api/v1/pop/queue/${queue}/partition/${partition}`, { params });
    }
    return apiClient.get(`/api/v1/pop/queue/${queue}`, { params });
  },
  
  // Acknowledgment
  ackMessage: (data) => apiClient.post('/api/v1/ack', data),
  batchAck: (data) => apiClient.post('/api/v1/ack/batch', data),
};
```

`src/api/resources.js`:
```javascript
import apiClient from './client';

export const resourcesApi = {
  getOverview: () => apiClient.get('/api/v1/resources/overview'),
  getNamespaces: () => apiClient.get('/api/v1/resources/namespaces'),
  getTasks: () => apiClient.get('/api/v1/resources/tasks'),
};
```

`src/api/analytics.js`:
```javascript
import apiClient from './client';

export const analyticsApi = {
  getStatus: (params) => apiClient.get('/api/v1/status', { params }),
  getQueues: (params) => apiClient.get('/api/v1/status/queues', { params }),
  getQueueDetail: (queueName, params) => apiClient.get(`/api/v1/status/queues/${queueName}`, { params }),
  getQueueMessages: (queueName, params) => apiClient.get(`/api/v1/status/queues/${queueName}/messages`, { params }),
  getAnalytics: (params) => apiClient.get('/api/v1/status/analytics', { params }),
};
```

---

### Phase 2: Layout & Navigation (Day 1-2)

**2.1 Create Layout Components**

`src/App.vue`:
```vue
<template>
  <div :class="{ 'dark': isDark }" class="min-h-screen">
    <AppLayout>
      <router-view />
    </AppLayout>
  </div>
</template>

<script setup>
import { provide } from 'vue';
import AppLayout from './components/layout/AppLayout.vue';
import { useTheme } from './composables/useTheme';

const { isDark, toggleTheme } = useTheme();

// Provide theme globally
provide('theme', { isDark, toggleTheme });
</script>
```

`src/components/layout/AppLayout.vue`:
```vue
<template>
  <div class="flex h-screen bg-gray-50 dark:bg-slate-900">
    <AppSidebar />
    
    <div class="flex-1 flex flex-col overflow-hidden">
      <AppHeader />
      
      <main class="flex-1 overflow-y-auto p-6">
        <slot />
      </main>
    </div>
  </div>
</template>

<script setup>
import AppHeader from './AppHeader.vue';
import AppSidebar from './AppSidebar.vue';
</script>
```

`src/components/layout/AppHeader.vue`:
```vue
<template>
  <header class="bg-white dark:bg-slate-800 border-b h-16 flex items-center px-6 justify-between">
    <div class="flex items-center gap-4">
      <img src="/assets/queen-logo.svg" alt="Queen" class="h-8 w-8" />
      <h1 class="text-xl font-semibold">Queen Message Queue</h1>
    </div>
    
    <div class="flex items-center gap-4">
      <HealthIndicator />
      <ThemeToggle />
    </div>
  </header>
</template>

<script setup>
import HealthIndicator from '../dashboard/HealthIndicator.vue';
import ThemeToggle from './ThemeToggle.vue';
</script>
```

`src/components/layout/AppSidebar.vue`:
```vue
<template>
  <aside class="w-64 bg-white dark:bg-slate-800 border-r flex flex-col">
    <nav class="flex-1 p-4 space-y-1">
      <router-link
        v-for="item in navigation"
        :key="item.path"
        :to="item.path"
        class="flex items-center gap-3 px-4 py-3 rounded-lg transition-colors"
        :class="[
          isActive(item.path)
            ? 'bg-blue-50 dark:bg-blue-900/20 text-blue-600 dark:text-blue-400'
            : 'text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-slate-700'
        ]"
      >
        <component :is="item.icon" class="w-5 h-5" />
        <span class="font-medium">{{ item.name }}</span>
      </router-link>
    </nav>
  </aside>
</template>

<script setup>
import { useRoute } from 'vue-router';

const route = useRoute();

const navigation = [
  { name: 'Dashboard', path: '/', icon: 'HomeIcon' },
  { name: 'Queues', path: '/queues', icon: 'QueueIcon' },
  { name: 'Consumer Groups', path: '/consumer-groups', icon: 'GroupIcon' },
  { name: 'Messages', path: '/messages', icon: 'MessageIcon' },
  { name: 'Analytics', path: '/analytics', icon: 'ChartIcon' },
];

const isActive = (path) => {
  if (path === '/') return route.path === '/';
  return route.path.startsWith(path);
};
</script>
```

`src/components/layout/ThemeToggle.vue`:
```vue
<template>
  <button
    @click="toggleTheme"
    class="p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-slate-700"
    aria-label="Toggle theme"
  >
    <svg v-if="isDark" class="w-5 h-5" fill="currentColor" viewBox="0 0 20 20">
      <path d="M10 2a1 1 0 011 1v1a1 1 0 11-2 0V3a1 1 0 011-1zm4 8a4 4 0 11-8 0 4 4 0 018 0zm-.464 4.95l.707.707a1 1 0 001.414-1.414l-.707-.707a1 1 0 00-1.414 1.414zm2.12-10.607a1 1 0 010 1.414l-.706.707a1 1 0 11-1.414-1.414l.707-.707a1 1 0 011.414 0zM17 11a1 1 0 100-2h-1a1 1 0 100 2h1zm-7 4a1 1 0 011 1v1a1 1 0 11-2 0v-1a1 1 0 011-1zM5.05 6.464A1 1 0 106.465 5.05l-.708-.707a1 1 0 00-1.414 1.414l.707.707zm1.414 8.486l-.707.707a1 1 0 01-1.414-1.414l.707-.707a1 1 0 011.414 1.414zM4 11a1 1 0 100-2H3a1 1 0 000 2h1z"/>
    </svg>
    <svg v-else class="w-5 h-5" fill="currentColor" viewBox="0 0 20 20">
      <path d="M17.293 13.293A8 8 0 016.707 2.707a8.001 8.001 0 1010.586 10.586z"/>
    </svg>
  </button>
</template>

<script setup>
import { inject } from 'vue';

const { isDark, toggleTheme } = inject('theme');
</script>
```

**2.2 Create Composables**

`src/composables/useTheme.js`:
```javascript
import { ref, watch, onMounted } from 'vue';

export function useTheme() {
  const isDark = ref(false);

  const toggleTheme = () => {
    isDark.value = !isDark.value;
  };

  // Watch for changes and update localStorage and document class
  watch(isDark, (newValue) => {
    if (newValue) {
      document.documentElement.classList.add('dark');
      localStorage.setItem('theme', 'dark');
    } else {
      document.documentElement.classList.remove('dark');
      localStorage.setItem('theme', 'light');
    }
  });

  // Initialize theme from localStorage or system preference
  onMounted(() => {
    const savedTheme = localStorage.getItem('theme');
    if (savedTheme) {
      isDark.value = savedTheme === 'dark';
    } else {
      isDark.value = window.matchMedia('(prefers-color-scheme: dark)').matches;
    }
  });

  return {
    isDark,
    toggleTheme,
  };
}
```

`src/composables/useApi.js`:
```javascript
import { ref } from 'vue';

export function useApi(apiCall) {
  const data = ref(null);
  const error = ref(null);
  const loading = ref(false);

  const execute = async (...params) => {
    loading.value = true;
    error.value = null;
    
    try {
      const response = await apiCall(...params);
      data.value = response.data;
      return response.data;
    } catch (err) {
      error.value = err.response?.data?.error || err.message || 'An error occurred';
      throw err;
    } finally {
      loading.value = false;
    }
  };

  const refresh = () => execute();

  return {
    data,
    error,
    loading,
    execute,
    refresh,
  };
}
```

**2.3 Create Router**

`src/router/index.js`:
```javascript
import { createRouter, createWebHistory } from 'vue-router';

const routes = [
  {
    path: '/',
    name: 'Dashboard',
    component: () => import('../views/Dashboard.vue'),
  },
  {
    path: '/queues',
    name: 'Queues',
    component: () => import('../views/Queues.vue'),
  },
  {
    path: '/queues/:queueName',
    name: 'QueueDetail',
    component: () => import('../views/QueueDetail.vue'),
  },
  {
    path: '/consumer-groups',
    name: 'ConsumerGroups',
    component: () => import('../views/ConsumerGroups.vue'),
  },
  {
    path: '/messages',
    name: 'Messages',
    component: () => import('../views/Messages.vue'),
  },
  {
    path: '/analytics',
    name: 'Analytics',
    component: () => import('../views/Analytics.vue'),
  },
];

const router = createRouter({
  history: createWebHistory(),
  routes,
});

export default router;
```

---

### Phase 3: Common Components (Day 2)

**3.1 Utility Components**

`src/components/common/MetricCard.vue`:
```vue
<template>
  <div class="card">
    <div class="flex items-center justify-between">
      <div>
        <p class="text-sm text-gray-500 dark:text-gray-400">{{ title }}</p>
        <p class="text-3xl font-bold mt-1">{{ formattedValue }}</p>
        <p v-if="subtitle" class="text-sm text-gray-500 dark:text-gray-400 mt-1">
          {{ subtitle }}
        </p>
      </div>
      <div v-if="icon" class="text-blue-500">
        <component :is="icon" class="w-10 h-10" />
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue';
import { formatNumber } from '../../utils/formatters';

const props = defineProps({
  title: String,
  value: [String, Number],
  subtitle: String,
  icon: Object,
});

const formattedValue = computed(() => {
  if (typeof props.value === 'number') {
    return formatNumber(props.value);
  }
  return props.value;
});
</script>
```

`src/components/common/StatusBadge.vue`:
```vue
<template>
  <span :class="badgeClass">
    <span v-if="showDot" class="w-2 h-2 rounded-full mr-1.5" :class="dotClass"></span>
    {{ label }}
  </span>
</template>

<script setup>
import { computed } from 'vue';

const props = defineProps({
  status: {
    type: String,
    required: true,
  },
  showDot: {
    type: Boolean,
    default: true,
  },
});

const statusConfig = {
  healthy: { class: 'badge-success', dot: 'bg-green-500', label: 'Healthy' },
  stable: { class: 'badge-success', dot: 'bg-green-500', label: 'Stable' },
  warning: { class: 'badge-warning', dot: 'bg-yellow-500', label: 'Warning' },
  error: { class: 'badge-danger', dot: 'bg-red-500', label: 'Error' },
  unhealthy: { class: 'badge-danger', dot: 'bg-red-500', label: 'Unhealthy' },
  pending: { class: 'badge-warning', dot: 'bg-yellow-500', label: 'Pending' },
  processing: { class: 'bg-blue-100 text-blue-800 dark:bg-blue-900 dark:text-blue-200', dot: 'bg-blue-500', label: 'Processing' },
  completed: { class: 'badge-success', dot: 'bg-green-500', label: 'Completed' },
  failed: { class: 'badge-danger', dot: 'bg-red-500', label: 'Failed' },
};

const config = computed(() => statusConfig[props.status.toLowerCase()] || statusConfig.pending);
const badgeClass = computed(() => `badge ${config.value.class}`);
const dotClass = computed(() => config.value.dot);
const label = computed(() => config.value.label);
</script>
```

`src/components/common/LoadingSpinner.vue`:
```vue
<template>
  <div class="flex items-center justify-center p-8">
    <div class="animate-spin rounded-full h-12 w-12 border-b-2 border-blue-600"></div>
  </div>
</template>
```

`src/components/common/RefreshButton.vue`:
```vue
<template>
  <button
    @click="$emit('refresh')"
    :disabled="loading"
    class="btn btn-secondary flex items-center gap-2"
  >
    <svg
      class="w-4 h-4"
      :class="{ 'animate-spin': loading }"
      fill="none"
      stroke="currentColor"
      viewBox="0 0 24 24"
    >
      <path
        stroke-linecap="round"
        stroke-linejoin="round"
        stroke-width="2"
        d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15"
      />
    </svg>
    Refresh
  </button>
</template>

<script setup>
defineProps({
  loading: Boolean,
});

defineEmits(['refresh']);
</script>
```

**3.2 Utility Functions**

`src/utils/formatters.js`:
```javascript
export function formatNumber(num) {
  if (num >= 1000000) {
    return (num / 1000000).toFixed(1) + 'M';
  }
  if (num >= 1000) {
    return (num / 1000).toFixed(1) + 'K';
  }
  return num.toString().replace(/\B(?=(\d{3})+(?!\d))/g, ',');
}

export function formatBytes(bytes) {
  if (bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

export function formatDuration(ms) {
  if (ms < 1000) return `${ms}ms`;
  
  const seconds = Math.floor(ms / 1000);
  const minutes = Math.floor(seconds / 60);
  const hours = Math.floor(minutes / 60);
  const days = Math.floor(hours / 24);
  
  if (days > 0) return `${days}d ${hours % 24}h`;
  if (hours > 0) return `${hours}h ${minutes % 60}m`;
  if (minutes > 0) return `${minutes}m ${seconds % 60}s`;
  return `${seconds}s`;
}

export function formatDate(date) {
  return new Date(date).toLocaleString('en-US', {
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
  });
}

export function formatTime(date) {
  return new Date(date).toLocaleTimeString('en-US', {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  });
}
```

---

### Phase 4: Dashboard Page (Day 3)

**4.1 Dashboard View**

`src/views/Dashboard.vue`:
```vue
<template>
  <div class="space-y-6">
    <div class="flex items-center justify-between">
      <h2 class="text-2xl font-bold">Dashboard</h2>
      <RefreshButton :loading="loading" @refresh="loadData" />
    </div>

    <LoadingSpinner v-if="loading && !overview" />

    <div v-else-if="error" class="card bg-red-50 dark:bg-red-900/20 text-red-600">
      <p>Error loading dashboard: {{ error }}</p>
    </div>

    <template v-else>
      <!-- Metric Cards -->
      <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-6">
        <MetricCard
          title="Queues"
          :value="overview?.queues || 0"
        />
        <MetricCard
          title="Partitions"
          :value="overview?.partitions || 0"
        />
        <MetricCard
          title="Total Messages"
          :value="overview?.messages?.total || 0"
        />
        <MetricCard
          title="Uptime"
          :value="health?.uptime || '-'"
        />
      </div>

      <!-- Throughput Chart -->
      <div class="card">
        <h3 class="text-lg font-semibold mb-4">Message Throughput</h3>
        <ThroughputChart :data="status" />
      </div>

      <!-- Stats Row -->
      <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <MessageStatusCard :data="overview?.messages" />
        <PerformanceCard :data="metrics" />
      </div>

      <!-- Top Queues -->
      <div class="card">
        <h3 class="text-lg font-semibold mb-4">Top Queues by Activity</h3>
        <TopQueuesTable :queues="topQueues" />
      </div>
    </template>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue';
import { healthApi } from '../api/health';
import { resourcesApi } from '../api/resources';
import { analyticsApi } from '../api/analytics';
import { queuesApi } from '../api/queues';

import MetricCard from '../components/common/MetricCard.vue';
import LoadingSpinner from '../components/common/LoadingSpinner.vue';
import RefreshButton from '../components/common/RefreshButton.vue';
import ThroughputChart from '../components/dashboard/ThroughputChart.vue';
import MessageStatusCard from '../components/dashboard/MessageStatusCard.vue';
import PerformanceCard from '../components/dashboard/PerformanceCard.vue';
import TopQueuesTable from '../components/dashboard/TopQueuesTable.vue';

const loading = ref(false);
const error = ref(null);
const overview = ref(null);
const health = ref(null);
const metrics = ref(null);
const status = ref(null);
const topQueues = ref([]);

async function loadData() {
  loading.value = true;
  error.value = null;

  try {
    const [overviewRes, healthRes, metricsRes, statusRes, queuesRes] = await Promise.all([
      resourcesApi.getOverview(),
      healthApi.getHealth(),
      healthApi.getMetrics(),
      analyticsApi.getStatus(),
      queuesApi.getQueues(),
    ]);

    overview.value = overviewRes.data;
    health.value = healthRes.data;
    metrics.value = metricsRes.data;
    status.value = statusRes.data;
    
    // Get top 5 queues by total messages
    topQueues.value = queuesRes.data.queues
      .sort((a, b) => (b.messages?.total || 0) - (a.messages?.total || 0))
      .slice(0, 5);
  } catch (err) {
    error.value = err.message;
  } finally {
    loading.value = false;
  }
}

onMounted(() => {
  loadData();
});
</script>
```

**4.2 Dashboard Components**

`src/components/dashboard/HealthIndicator.vue`:
```vue
<template>
  <div class="flex items-center gap-2">
    <span
      class="w-2 h-2 rounded-full"
      :class="isHealthy ? 'bg-green-500' : 'bg-red-500'"
    ></span>
    <span class="text-sm font-medium">{{ statusText }}</span>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue';
import { healthApi } from '../../api/health';

const health = ref(null);

const isHealthy = computed(() => health.value?.status === 'healthy');
const statusText = computed(() => isHealthy.value ? 'Healthy' : 'Unhealthy');

async function checkHealth() {
  try {
    const response = await healthApi.getHealth();
    health.value = response.data;
  } catch (error) {
    health.value = { status: 'unhealthy' };
  }
}

onMounted(() => {
  checkHealth();
  // Check health every 30 seconds
  setInterval(checkHealth, 30000);
});
</script>
```

`src/components/dashboard/ThroughputChart.vue`:
```vue
<template>
  <div class="h-64">
    <Line v-if="chartData" :data="chartData" :options="chartOptions" />
    <div v-else class="flex items-center justify-center h-full text-gray-500">
      No data available
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue';
import { Line } from 'vue-chartjs';
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
  Filler,
} from 'chart.js';

ChartJS.register(
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
  Filler
);

const props = defineProps({
  data: Object,
});

const chartData = computed(() => {
  if (!props.data?.throughput?.length) return null;

  const throughput = props.data.throughput;
  
  return {
    labels: throughput.map(t => {
      const date = new Date(t.timestamp);
      return date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
    }),
    datasets: [
      {
        label: 'Ingested',
        data: throughput.map(t => t.ingestedPerSecond),
        borderColor: 'rgb(59, 130, 246)',
        backgroundColor: 'rgba(59, 130, 246, 0.1)',
        fill: true,
        tension: 0.4,
      },
      {
        label: 'Processed',
        data: throughput.map(t => t.processedPerSecond),
        borderColor: 'rgb(16, 185, 129)',
        backgroundColor: 'rgba(16, 185, 129, 0.1)',
        fill: true,
        tension: 0.4,
      },
    ],
  };
});

const chartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  interaction: {
    mode: 'index',
    intersect: false,
  },
  scales: {
    y: {
      beginAtZero: true,
      ticks: {
        callback: (value) => value + ' msg/s',
      },
    },
  },
  plugins: {
    legend: {
      position: 'top',
    },
    tooltip: {
      callbacks: {
        label: (context) => {
          return `${context.dataset.label}: ${context.parsed.y} msg/s`;
        },
      },
    },
  },
};
</script>
```

`src/components/dashboard/MessageStatusCard.vue`:
```vue
<template>
  <div class="card">
    <h3 class="text-lg font-semibold mb-4">Message Status</h3>
    <div class="space-y-3">
      <div class="flex items-center justify-between">
        <span class="text-gray-600 dark:text-gray-400">Pending</span>
        <span class="font-semibold">{{ formatNumber(data?.pending || 0) }}</span>
      </div>
      <div class="flex items-center justify-between">
        <span class="text-gray-600 dark:text-gray-400">Processing</span>
        <span class="font-semibold">{{ formatNumber(data?.processing || 0) }}</span>
      </div>
      <div class="flex items-center justify-between">
        <span class="text-gray-600 dark:text-gray-400">Completed</span>
        <span class="font-semibold text-green-600">{{ formatNumber(data?.completed || 0) }}</span>
      </div>
      <div class="flex items-center justify-between">
        <span class="text-gray-600 dark:text-gray-400">Failed</span>
        <span class="font-semibold text-red-600">{{ formatNumber(data?.failed || 0) }}</span>
      </div>
      <div class="flex items-center justify-between">
        <span class="text-gray-600 dark:text-gray-400">Dead Letter</span>
        <span class="font-semibold text-gray-600">{{ formatNumber(data?.deadLetter || 0) }}</span>
      </div>
    </div>
  </div>
</template>

<script setup>
import { formatNumber } from '../../utils/formatters';

defineProps({
  data: Object,
});
</script>
```

`src/components/dashboard/PerformanceCard.vue`:
```vue
<template>
  <div class="card">
    <h3 class="text-lg font-semibold mb-4">System Performance</h3>
    <div class="space-y-3">
      <div class="flex items-center justify-between">
        <span class="text-gray-600 dark:text-gray-400">Requests/sec</span>
        <span class="font-semibold">{{ data?.requests?.rate?.toFixed(2) || '0.00' }}</span>
      </div>
      <div class="flex items-center justify-between">
        <span class="text-gray-600 dark:text-gray-400">Messages/sec</span>
        <span class="font-semibold">{{ data?.messages?.rate?.toFixed(2) || '0.00' }}</span>
      </div>
      <div class="flex items-center justify-between">
        <span class="text-gray-600 dark:text-gray-400">DB Connections</span>
        <span class="font-semibold">
          {{ data?.database?.poolSize - data?.database?.idleConnections || 0 }}/{{ data?.database?.poolSize || 0 }}
        </span>
      </div>
      <div class="flex items-center justify-between">
        <span class="text-gray-600 dark:text-gray-400">Memory (Heap)</span>
        <span class="font-semibold">{{ formatBytes(data?.memory?.heapUsed || 0) }}</span>
      </div>
      <div class="flex items-center justify-between">
        <span class="text-gray-600 dark:text-gray-400">CPU Time</span>
        <span class="font-semibold">{{ formatDuration((data?.cpu?.user + data?.cpu?.system) / 1000 || 0) }}</span>
      </div>
    </div>
  </div>
</template>

<script setup>
import { formatBytes, formatDuration } from '../../utils/formatters';

defineProps({
  data: Object,
});
</script>
```

`src/components/dashboard/TopQueuesTable.vue`:
```vue
<template>
  <div class="overflow-x-auto">
    <table class="w-full">
      <thead class="text-left text-sm text-gray-500 border-b">
        <tr>
          <th class="pb-3 font-medium">Queue Name</th>
          <th class="pb-3 font-medium text-right">Pending</th>
          <th class="pb-3 font-medium text-right">Processing</th>
          <th class="pb-3 font-medium text-right">Completed</th>
        </tr>
      </thead>
      <tbody class="text-sm">
        <tr
          v-for="queue in queues"
          :key="queue.id"
          class="border-b last:border-b-0 hover:bg-gray-50 dark:hover:bg-slate-700/50 cursor-pointer"
          @click="navigateToQueue(queue.name)"
        >
          <td class="py-3">
            <div class="font-medium">{{ queue.name }}</div>
            <div v-if="queue.namespace" class="text-xs text-gray-500">{{ queue.namespace }}</div>
          </td>
          <td class="py-3 text-right">{{ formatNumber(queue.messages?.pending || 0) }}</td>
          <td class="py-3 text-right">{{ formatNumber(queue.messages?.processing || 0) }}</td>
          <td class="py-3 text-right">{{ formatNumber(queue.messages?.total || 0) }}</td>
        </tr>
      </tbody>
    </table>
    
    <div v-if="!queues?.length" class="text-center py-8 text-gray-500">
      No queues available
    </div>
  </div>
</template>

<script setup>
import { useRouter } from 'vue-router';
import { formatNumber } from '../../utils/formatters';

defineProps({
  queues: Array,
});

const router = useRouter();

function navigateToQueue(queueName) {
  router.push(`/queues/${queueName}`);
}
</script>
```

---

### Phase 5: Remaining Pages (Day 4-5)

Continue implementing:
- Queues page with list and filters
- Queue Detail page with full information
- Messages page with browsing capabilities
- Consumer Groups page
- Analytics page with charts

Each page follows similar patterns established in the Dashboard.

---

### Phase 6: Testing & Polish (Day 6)

1. Test all API integrations
2. Verify dark mode consistency
3. Test responsive layouts
4. Add error boundaries
5. Optimize performance
6. Add loading states everywhere
7. Test with real queue data

---

## Chart Configuration

### Chart.js Setup

All charts use consistent color schemes:

**Light Mode:**
- Primary: Blue (#3b82f6)
- Success: Green (#10b981)
- Warning: Yellow (#f59e0b)
- Danger: Red (#ef4444)

**Dark Mode:**
- Primary: Light Blue (#60a5fa)
- Success: Light Green (#34d399)
- Warning: Light Yellow (#fbbf24)
- Danger: Light Red (#f87171)

### Chart Types Used:

1. **Line Charts:** Throughput, time-series data
2. **Bar Charts:** Queue comparisons, volume metrics
3. **Doughnut Charts:** Message status distribution
4. **Stacked Bar Charts:** Multi-metric comparisons

---

## Environment Configuration

`.env`:
```
VITE_API_BASE_URL=http://localhost:4000
VITE_APP_TITLE=Queen Message Queue
```

---

## Build & Deploy

`package.json` scripts:
```json
{
  "scripts": {
    "dev": "vite",
    "build": "vite build",
    "preview": "vite preview"
  }
}
```

Build for production:
```bash
npm run build
```

Output in `dist/` directory ready for deployment.

---

## Summary

This design creates a **professional, clean, and highly functional** web application for Queen Message Queue with:

✅ **Real API Integration** - Every component uses actual endpoints, no mocked data  
✅ **Beautiful UI** - Modern design with Tailwind, proper spacing, typography  
✅ **Dark Mode** - Full light/dark theme support  
✅ **Responsive** - Works on desktop and tablet  
✅ **Clear Navigation** - Intuitive sidebar with logical page organization  
✅ **Data Visualization** - Meaningful charts showing system health and activity  
✅ **Manual Refresh** - No auto-refresh, user-controlled data updates  
✅ **Clean Code** - Vue 3 Composition API, modular structure  
✅ **No Animations** - Minimal transitions, focus on content

The application provides complete visibility into the message queue system with intuitive interactions for managing queues, monitoring consumers, browsing messages, and analyzing system performance.

