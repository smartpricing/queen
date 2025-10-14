# Queen Message Queue Dashboard v4 - Implementation Plan

## Overview

A beautiful, modern web dashboard for monitoring the Queen Message Queue system, built with **Vue 3** and **Nuxt UI** in plain JavaScript. The dashboard provides real-time insights into queue performance, message flow, and system health.

**Reference Template**: [Nuxt UI Vue Dashboard](https://dashboard-vue-template.nuxt.dev/) ([GitHub](https://github.com/nuxt-ui-templates/dashboard-vue))

## Design Reference

**Primary Reference**: [Nuxt UI Vue Dashboard Template](https://dashboard-vue-template.nuxt.dev/)

### Key Design Elements to Follow:
1. ✅ **Collapsible sidebar** (left side)
   - Clean, minimal design with hover states
   - Icon + label navigation items
   - Collapsible groups for nested navigation
   - Dark/light theme support

2. ✅ **Top bar layout**:
   - Search command palette (Cmd+K)
   - Theme toggle (sun/moon icon)
   - User menu with avatar
   - Breadcrumbs for navigation context
   - Clean, consistent header

3. ✅ **Content area**:
   - Card-based layouts with subtle borders
   - Tab navigation for views
   - Tables with sortable columns
   - Clean spacing and padding
   - Responsive grid layouts

4. ✅ **Color scheme**:
   - Primary: Green (#10B981) as shown in template
   - Customizable via Tailwind colors
   - Clean backgrounds with subtle borders
   - Status badges with semantic colors
   - Dark mode with proper contrast

5. ✅ **Typography**:
   - Clear hierarchy (text-xl, text-lg, text-sm)
   - Inter font family
   - Adequate spacing and line-height
   - Consistent font weights

6. ✅ **Tables**:
   - Clean rows with hover states
   - Badge components for status
   - Sortable columns
   - Actions dropdown menu
   - Pagination controls

## Technology Stack

- **Framework**: Vue 3 (Composition API)
- **UI Library**: Nuxt UI ([@nuxt/ui](https://ui.nuxt.com/))
- **Build Tool**: Vite
- **Language**: Plain JavaScript (no TypeScript)
- **Charts**: Chart.js with vue-chartjs
- **Icons**: Heroicons (via Nuxt UI)
- **Styling**: Tailwind CSS v3 (via Nuxt UI)
- **HTTP Client**: Native Fetch API
- **Routing**: Vue Router 4
- **Command Palette**: Built-in Nuxt UI CommandPalette

## Design Philosophy

1. **Beautiful & Modern**: Inspired by Nuxt UI dashboard template design
2. **Responsive**: Mobile-first design that works on all screen sizes
3. **Dark/Light Theme**: Full support for theme switching via Tailwind CSS
4. **Real-time**: Auto-refresh data with configurable intervals
5. **Performance**: Efficient rendering with virtual scrolling for large lists
6. **User-friendly**: Intuitive navigation with command palette (Cmd+K)
7. **Accessible**: Built on Headless UI for full accessibility

## Sidebar Navigation

**Based on Nuxt UI Dashboard Template**

### Layout
- **Width**: Collapsible (240px expanded, 64px collapsed on desktop)
- **Position**: Fixed left side (desktop/tablet), drawer (mobile)
- **Background**: Themed background (Tailwind classes)
- **Icons**: Heroicons via Nuxt UI
- **Components**: Use `UVerticalNavigation` component

### Navigation Structure

```javascript
const links = [
  {
    label: 'Dashboard',
    icon: 'i-heroicons-home',
    to: '/'
  },
  {
    label: 'Queues',
    icon: 'i-heroicons-queue-list',
    to: '/queues',
    badge: '12' // Optional: show queue count
  },
  {
    label: 'Messages',
    icon: 'i-heroicons-envelope',
    to: '/messages'
  },
  {
    label: 'Analytics',
    icon: 'i-heroicons-chart-bar',
    to: '/analytics'
  },
  {
    label: 'Settings',
    icon: 'i-heroicons-cog-6-tooth',
    to: '/settings'
  }
];
```

### Features
- **Collapsible**: Toggle between expanded/collapsed states
- **Active States**: Automatic active route highlighting
- **Badges**: Show notification counts on navigation items
- **Grouped Items**: Support for nested navigation groups
- **Search Integration**: Links available in command palette

### Mobile Adaptation
- On mobile (< 768px): Sidebar becomes a **slide-in drawer**
- Trigger via hamburger menu button in top bar
- Full-height overlay when open
- Swipe to close gesture support

## Top Bar Layout

**Based on Nuxt UI Dashboard Template**

### Desktop/Tablet
- **Height**: 64px
- **Background**: Themed background with border-b
- **Border**: `border-gray-200 dark:border-gray-800`

**Layout (2 sections)**:

**Left Section**:
- Hamburger menu button (mobile only)
- Page title or breadcrumbs
- Uses Nuxt UI `UBreadcrumb` component

**Right Section**:
- Command palette trigger (`Cmd+K`) using `UButton` with kbd
- Theme toggle using `UColorModeButton`
- Notifications dropdown using `UDropdown`
- User menu with avatar using `UAvatar` + `UDropdown`

### Command Palette
- Trigger: `Cmd+K` keyboard shortcut or button click
- Component: `UCommandPalette`
- Features:
  - Search all routes
  - Quick actions (refresh, export, etc.)
  - Recent activity
  - Keyboard navigation

### Mobile
- **Left**: Hamburger menu + page title
- **Right**: Command palette + theme toggle (collapsed user menu)
- Height: 56px
- Full command palette on mobile

## Application Architecture

```
dashboard/
├── index.html                 # Main HTML entry point
├── package.json              # Dependencies
├── vite.config.js            # Vite configuration
├── tailwind.config.js        # Tailwind CSS configuration
├── public/
│   └── favicon.ico           # App favicon
└── src/
    ├── main.js               # App entry point with Nuxt UI setup
    ├── App.vue               # Root component with layout
    ├── router.js             # Vue Router configuration
    ├── app.config.ts         # Nuxt UI app config (colors, shortcuts)
    ├── api/
    │   ├── client.js         # API client wrapper
    │   └── endpoints.js      # API endpoint definitions
    ├── composables/
    │   ├── useColorMode.js   # Dark/light mode composable (Nuxt UI)
    │   ├── useApi.js         # API data fetching composable
    │   ├── usePolling.js     # Auto-refresh composable
    │   └── useFilters.js     # Filter state management
    ├── components/
    │   ├── layout/
    │   │   ├── AppLayout.vue       # Main layout with sidebar
    │   │   ├── AppHeader.vue       # Top bar with command palette
    │   │   └── AppSidebar.vue      # Collapsible sidebar navigation
    │   ├── dashboard/
    │   │   ├── MetricCard.vue      # Metric display card (UCard)
    │   │   ├── ThroughputChart.vue # Throughput line chart
    │   │   ├── QueuesList.vue      # Quick queues overview (UTable)
    │   │   └── ActivityFeed.vue    # Recent activity/events
    │   ├── queues/
    │   │   ├── QueueCard.vue       # Queue summary card (UCard)
    │   │   ├── QueueTable.vue      # Queues data table (UTable)
    │   │   └── QueueFilters.vue    # Filter controls (UInput, USelect)
    │   ├── queue-detail/
    │   │   ├── QueueHeader.vue     # Queue info header
    │   │   ├── QueueStats.vue      # Queue statistics
    │   │   ├── PartitionsTable.vue # Partitions breakdown (UTable)
    │   │   └── QueueChart.vue      # Queue-specific charts
    │   ├── messages/
    │   │   ├── MessagesTable.vue   # Messages data table (UTable)
    │   │   ├── MessageDetail.vue   # Message details (UModal)
    │   │   └── MessageFilters.vue  # Message filter controls
    │   ├── analytics/
    │   │   ├── ThroughputPanel.vue    # Throughput analytics
    │   │   ├── LatencyPanel.vue       # Latency percentiles
    │   │   ├── ErrorRatePanel.vue     # Error rate trends
    │   │   ├── TopQueuesPanel.vue     # Top queues by volume
    │   │   └── DLQPanel.vue           # Dead letter queue stats
    │   └── common/
    │       ├── StatusBadge.vue     # Status badge (UBadge)
    │       ├── RefreshButton.vue   # Manual refresh (UButton)
    │       ├── TimeRangePicker.vue # Time range selector
    │       └── LoadingState.vue    # Loading state (USkeleton)
    └── views/
        ├── Dashboard.vue       # Main dashboard overview
        ├── Queues.vue          # Queues list view
        ├── QueueDetail.vue     # Single queue detail view
        ├── Messages.vue        # Messages browser view
        ├── Analytics.vue       # Advanced analytics view
        └── Settings.vue        # Settings and preferences
```

## Pages & Features

### 1. Dashboard Overview (`/`)

**Purpose**: High-level system overview with key metrics and real-time throughput.

**Components**:
- **Header**: Title, command palette, theme toggle, refresh button
- **Metrics Row**: 4 metric cards (Total Messages, Pending, Processing, Failed)
- **Throughput Chart**: Line chart showing ingested/processed messages per minute
- **Two-Column Layout**:
  - **Left**: Active queues list with quick stats (UCard)
  - **Right**: Recent activity feed showing DLQ errors and lease activity
- **Bottom Stats**: Active leases, dead letter messages, current depth

**Nuxt UI Components**:
- `UCard` - for metric cards and panels
- `UBadge` - for status indicators and counts
- `UButton` - for actions (refresh, view details)
- `UButtonGroup` - for time range selection
- `UTable` - for queues list
- Chart.js (via vue-chartjs) - for throughput visualization

**API Endpoint**: `GET /api/v1/status`

**Key Features**:
- Auto-refresh every 5 seconds
- Responsive grid layout (grid-cols-1 md:grid-cols-2 lg:grid-cols-4)
- Animated counters for metrics
- Click queue row to navigate to detail

---

### 2. Queues List (`/queues`)

**Purpose**: Comprehensive list of all queues with filtering and sorting.

**Components**:
- **Filter Bar**: Namespace filter, Task filter, Search input
- **Queues Table**: UTable with columns:
  - Name (with namespace/task badges)
  - Partitions count
  - Total Messages
  - Pending (with lag indicator)
  - Processing
  - Completed
  - Failed (with error rate %)
  - Avg Processing Time
  - Actions (dropdown menu)
- **Pagination**: Built into UTable

**Nuxt UI Components**:
- `UTable` - main table with sorting, filtering, pagination
- `UInput` - search field with icon
- `USelect` or `USelectMenu` - namespace/task filters
- `UBadge` - for namespace/task display and counts
- `UProgress` - for completion percentage
- `UButton` - action buttons
- `UDropdown` - actions menu per row

**API Endpoint**: `GET /api/v1/status/queues`

**Key Features**:
- Client-side sorting (click column headers)
- Client-side filtering with debounced search
- Pagination controls (rows per page)
- Export to CSV functionality via UButton
- Highlight rows with high lag (conditional class binding)
- Row click to navigate to queue detail

---

### 3. Queue Detail (`/queues/:queueName`)

**Purpose**: Deep dive into a specific queue with partition-level breakdown.

**Components**:
- **Queue Header**: 
  - Queue name, namespace, task
  - Priority badge
  - Configuration details (lease time, retry limit, TTL)
- **Summary Cards Row**:
  - Total Messages
  - Pending (with lag)
  - Processing (with active leases)
  - Completed
  - Failed (with retry info)
- **Partitions Section**:
  - UAccordion or UTabs for each partition
  - Each partition shows:
    - Message counts by status
    - Cursor position (consumed count, last consumed time)
    - Active lease info (if any)
    - Lag metrics
- **Charts Section**:
  - Message flow over time (stacked bar chart)
  - Status distribution (doughnut chart)

**Nuxt UI Components**:
- `UCard` - for queue info and metric cards
- `UAccordion` or `UTabs` - partitions display
- `UTable` - partitions summary table
- Chart.js (via vue-chartjs) - various charts
- `UProgress` - progress indicators
- `UTooltip` - detailed info on hover
- `UButton` - actions (view messages, refresh)
- `UBadge` - priority, status badges

**API Endpoint**: `GET /api/v1/status/queues/:queueName`

**Key Features**:
- Real-time partition status updates
- Visual lag indicators with color coding
- Expandable partition details
- Navigate to messages filtered by partition

---

### 4. Messages Browser (`/queues/:queueName/messages`)

**Purpose**: Browse and inspect individual messages in a queue.

**Components**:
- **Filter Bar**:
  - Status filter (All, Pending, Processing, Completed, Failed)
  - Partition filter dropdown
  - Time range picker
  - Search by transaction ID
- **Messages Table**: UTable with columns:
  - Transaction ID (copyable with UButton icon)
  - Trace ID (copyable)
  - Partition
  - Status badge
  - Created At (relative time)
  - Age/Processing Time
  - Worker ID (if applicable)
  - Retry Count
  - Actions (View Details)
- **Message Detail Dialog**:
  - Full message info
  - JSON payload viewer (syntax highlighted with Shiki)
  - Error message (for failed)
  - Timeline of status changes

**Nuxt UI Components**:
- `UTable` - messages table with pagination
- `USelect` or `USelectMenu` - filters
- `UInput` - search with icon
- `UModal` - message detail modal (slideover option)
- `UBadge` - status badges
- `UButton` - actions (copy, view details)
- `UKbd` - keyboard shortcuts display
- `UCode` - code block for JSON payload

**API Endpoint**: `GET /api/v1/status/queues/:queueName/messages`

**Key Features**:
- Pagination for large datasets
- Real-time updates (optional polling)
- Copy transaction/trace IDs to clipboard (useClipboard composable)
- Syntax-highlighted JSON payload
- Filter by status, partition, time range
- Export filtered messages to JSON
- Keyboard shortcuts (arrow keys, Enter to view)

---

### 5. Analytics (`/analytics`)

**Purpose**: Advanced analytics with time-series data and trends.

**Components**:
- **Filter Panel** (top):
  - Namespace filter
  - Queue filter
  - Time range picker (last hour, 6h, 24h, 7d, custom)
  - Interval selector (minute, hour, day)
- **Throughput Section**:
  - Line chart: Ingested vs Processed over time
  - Total counts and averages
- **Latency Section**:
  - Line chart: p50, p95, p99 latencies over time
  - Current latency stats
- **Error Rate Section**:
  - Area chart: Failed messages over time
  - Error rate percentage trend
- **Top Queues Section**:
  - UTable showing top 10 queues by volume
  - Performance metrics per queue
- **Dead Letter Queue Section**:
  - Line chart: DLQ messages over time
  - Top errors breakdown (bar chart)
  - Total DLQ count

**Nuxt UI Components**:
- `UCard` - section containers
- Chart.js (via vue-chartjs) - multiple chart types (line, area, bar, doughnut)
- `UTable` - top queues table
- `USelect` or `USelectMenu` - filters
- `UButtonGroup` - time range quick selection
- `URadioGroup` - interval selector
- `UTabs` - organize different analytics sections
- `UDivider` - section separators

**API Endpoint**: `GET /api/v1/status/analytics`

**Key Features**:
- Interactive charts with zoom/pan
- Compare multiple queues
- Export charts as images (via canvas)
- Download data as CSV
- Configurable refresh interval
- Responsive chart layouts (grid-based)

---

### 6. Settings (`/settings`)

**Purpose**: User preferences and configuration.

**Components**:
- **Appearance Section**:
  - Theme selector (Light/Dark/Auto)
  - Primary color picker (Tailwind color presets)
  - Compact mode toggle
- **Refresh Settings**:
  - Auto-refresh toggle
  - Refresh interval slider (5s - 60s)
- **API Configuration**:
  - Base URL input
  - Connection status indicator
  - Test connection button
- **Display Preferences**:
  - Time format (relative/absolute)
  - Decimal precision
  - Rows per page defaults

**Nuxt UI Components**:
- `UCard` - section containers
- `UToggle` - on/off settings
- `URadioGroup` - theme selection
- `URange` - interval slider
- `UInput` - text inputs
- `UButton` - action buttons
- `UDivider` - section separators
- `UAlert` - info/success messages
- `UFormGroup` - form field grouping with labels

**Key Features**:
- Persist settings to localStorage
- Real-time theme preview (useColorMode composable)
- Connection health check
- Reset to defaults button

---

## Nuxt UI Components Used

### Core Components
1. **Layout**: `UCard`, `UContainer`, `UDivider`, `UPage`, `UDashboardPanel`
2. **Data Display**: `UTable`, `UBadge`, `UAvatar`, `UKbd`
3. **Charts**: Chart.js via vue-chartjs wrapper
4. **Overlays**: `UModal`, `USlideover`, `UTooltip`, `UPopover`, `UDropdown`
5. **Form**: `UInput`, `USelect`, `USelectMenu`, `URange`, `UToggle`, `URadioGroup`, `UButtonGroup`, `UFormGroup`
6. **Buttons**: `UButton`, `UButtonGroup`
7. **Navigation**: `UVerticalNavigation`, `UHorizontalNavigation`, `UBreadcrumb`, `UCommandPalette`
8. **Feedback**: `UAlert`, `UProgress`, `USkeleton`, `UNotification`
9. **Misc**: `UCode`, `UColorModeButton`, `UIcon`

### Advanced Features
- **Command Palette**: Global search and actions (Cmd+K)
- **Keyboard Shortcuts**: Built-in keyboard navigation
- **Responsive Design**: Tailwind breakpoints (sm, md, lg, xl)
- **Dark Mode**: Automatic dark mode support via useColorMode
- **Accessibility**: Built on Headless UI for full a11y support
- **Color Customization**: Tailwind color system integration

---

## Theme Implementation

**IMPORTANT**: Use Nuxt UI's Tailwind CSS-based theming system with app.config.ts

### Nuxt UI Theme Architecture

Nuxt UI is built on Tailwind CSS and uses an `app.config.ts` file for customization:

#### 1. **Tailwind Color System**
- Use Tailwind's default color palette or customize
- Colors: `slate`, `gray`, `zinc`, `neutral`, `stone`, `red`, `orange`, `amber`, `yellow`, `lime`, `green`, `emerald`, `teal`, `cyan`, `sky`, `blue`, `indigo`, `violet`, `purple`, `fuchsia`, `pink`, `rose`
- Each color has shades: 50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 950

#### 2. **App Config (app.config.ts)**
- Configure primary color and gray shade
- Define UI component defaults
- Set keyboard shortcuts
- Example:
```typescript
export default defineAppConfig({
  ui: {
    primary: 'green',  // Primary color for the app
    gray: 'slate',     // Gray shade for neutral elements
    // Component-specific overrides
    card: {
      base: 'overflow-hidden',
      background: 'bg-white dark:bg-gray-900',
      shadow: 'shadow-sm'
    }
  }
})
```

#### 3. **Dark Mode**
- Automatic dark mode via `useColorMode()` composable
- Uses Tailwind's `dark:` variant
- Class-based: adds `.dark` class to `<html>` element
- Persisted to localStorage

### Best Practices

**✅ DO:**
1. Use **Tailwind utility classes** for styling
2. Use **Nuxt UI components** with their props for variants
3. Customize via **app.config.ts** for global changes
4. Use **dark:** variant for dark mode styling
5. Leverage Nuxt UI's **color** and **variant** props

**❌ DON'T:**
1. Write custom CSS unless absolutely necessary
2. Override Nuxt UI component styles with !important
3. Hardcode colors (use Tailwind classes)
4. Ignore dark mode variants

### Setup in main.js

```javascript
// src/main.js
import { createApp } from 'vue';
import { createPinia } from 'pinia';
import router from './router';
import App from './App.vue';

// Import Nuxt UI (standalone Vue version)
import ui from '@nuxt/ui';

// Import Tailwind CSS
import './assets/main.css';

const app = createApp(App);
const pinia = createPinia();

app.use(pinia);
app.use(router);
app.use(ui);

app.mount('#app');
```

```css
/* src/assets/main.css */
@tailwind base;
@tailwind components;
@tailwind utilities;
```

### App Configuration

**ALL theme customization in ONE file**: `src/app.config.ts`

```typescript
// src/app.config.ts - SINGLE SOURCE OF TRUTH FOR STYLING
export default defineAppConfig({
  ui: {
    // Primary brand color (matches Nuxt UI dashboard template)
    primary: 'green',
    
    // Gray shade for neutral elements
    gray: 'slate',
    
    // Keyboard shortcuts for command palette
    shortcuts: {
      commandPalette: {
        open: ['meta', 'k'],
        close: ['escape']
      }
    },
    
    // Global component customization
    card: {
      base: 'overflow-hidden',
      background: 'bg-white dark:bg-gray-900',
      divide: 'divide-y divide-gray-200 dark:divide-gray-800',
      ring: 'ring-1 ring-gray-200 dark:ring-gray-800',
      rounded: 'rounded-lg',
      shadow: 'shadow'
    },
    
    button: {
      default: {
        size: 'sm',
        color: 'gray',
        variant: 'ghost'
      }
    },
    
    table: {
      default: {
        sortButton: {
          color: 'gray'
        }
      }
    }
  }
});
```

### Tailwind Configuration

```javascript
// tailwind.config.js
/** @type {import('tailwindcss').Config} */
export default {
  content: [
    './index.html',
    './src/**/*.{vue,js,ts,jsx,tsx}',
  ],
  theme: {
    extend: {
      colors: {
        // Add custom colors if needed
        // 'brand': { ... }
      }
    },
  },
  plugins: [],
  darkMode: 'class'  // Enable dark mode via class
}
```

### Dark Mode Composable

```javascript
// src/composables/useColorMode.js
// Note: Nuxt UI provides this composable, but for standalone Vue:
import { ref, watch, onMounted } from 'vue';

export function useColorMode() {
  const colorMode = ref('light');
  
  onMounted(() => {
    // Initialize from localStorage or system preference
    const saved = localStorage.getItem('color-mode');
    if (saved) {
      colorMode.value = saved;
    } else if (window.matchMedia('(prefers-color-scheme: dark)').matches) {
      colorMode.value = 'dark';
    }
    updateDOM();
  });
  
  const updateDOM = () => {
    if (colorMode.value === 'dark') {
      document.documentElement.classList.add('dark');
    } else {
      document.documentElement.classList.remove('dark');
    }
  };
  
  watch(colorMode, (newMode) => {
    localStorage.setItem('color-mode', newMode);
    updateDOM();
  });
  
  const toggleColorMode = () => {
    colorMode.value = colorMode.value === 'light' ? 'dark' : 'light';
  };
  
  return { colorMode, toggleColorMode };
}
```

### Component Usage

**ALWAYS use Nuxt UI components with Tailwind classes. NEVER hardcode colors.**

```vue
<!-- ✅ CORRECT: Use Nuxt UI color props -->
<UBadge :label="count" color="green" />
<UBadge :label="status" color="yellow" />
<UButton label="Submit" color="primary" />
<UAlert color="red" title="Error" description="Error message" />

<!-- ✅ CORRECT: Use Nuxt UI components with Tailwind -->
<UCard>
  <template #header>
    <h3 class="text-lg font-semibold">Queue Status</h3>
  </template>
  <div class="space-y-2">
    <!-- Nuxt UI + Tailwind handles all styling -->
  </div>
</UCard>

<!-- ❌ WRONG: Don't use inline styles with hex colors -->
<div style="background-color: #3B82F6">Bad</div>

<!-- ✅ CORRECT: Use Tailwind utility classes -->
<div class="bg-blue-500">Good</div>
<div class="bg-white dark:bg-gray-900">Good with dark mode</div>
```

### Tailwind Utility Classes

Use Tailwind's utility classes for all styling:

```html
<!-- Backgrounds -->
<div class="bg-gray-50 dark:bg-gray-900">Background</div>
<div class="bg-white dark:bg-gray-800">Card background</div>

<!-- Text colors -->
<span class="text-gray-900 dark:text-white">Primary text</span>
<span class="text-gray-600 dark:text-gray-400">Secondary text</span>

<!-- Spacing -->
<div class="p-4">Padding 1rem</div>
<div class="m-2">Margin 0.5rem</div>
<div class="space-y-4">Vertical spacing between children</div>

<!-- Borders -->
<div class="border border-gray-200 dark:border-gray-800 rounded-lg">Bordered</div>
```

### Status Colors (Use Color Props)

```vue
<!-- Message status badges - use color prop -->
<UBadge v-if="status === 'pending'" label="Pending" color="yellow" />
<UBadge v-if="status === 'processing'" label="Processing" color="blue" />
<UBadge v-if="status === 'completed'" label="Completed" color="green" />
<UBadge v-if="status === 'failed'" label="Failed" color="red" />

<!-- No custom CSS needed! Nuxt UI + Tailwind handles everything -->
```

### Per-Component Customization

Use the `:ui` prop or Tailwind classes for **per-component** customization:

```vue
<template>
  <!-- Global theme applied -->
  <UToggle v-model="checked1" />
  
  <!-- Custom styling for this instance only -->
  <UToggle 
    v-model="checked2" 
    :ui="{ 
      active: 'bg-amber-500 dark:bg-amber-400',
      inactive: 'bg-gray-200 dark:bg-gray-700'
    }" 
  />
  
  <!-- Or use Tailwind classes directly -->
  <UButton 
    color="amber" 
    class="px-6 py-3"
  >
    Custom Button
  </UButton>
</template>

<script setup>
import { ref } from 'vue';

const checked1 = ref(true);
const checked2 = ref(true);
</script>
```

### Dynamic Theme Utilities

```javascript
// src/composables/useAppConfig.js
import { computed } from 'vue';
import { useAppConfig } from '#app';  // For Nuxt
// For standalone Vue, use a custom composable

export function useThemeConfig() {
  const appConfig = useAppConfig();
  
  // Get current primary color
  const primaryColor = computed(() => appConfig.ui.primary);
  
  // Update primary color dynamically
  const setPrimaryColor = (color) => {
    appConfig.ui.primary = color;
    // Note: This requires app restart in standalone Vue
    // Consider using CSS variables for dynamic changes
  };
  
  return {
    primaryColor,
    setPrimaryColor
  };
}

// Using CSS variables for dynamic color changes
export function useDynamicColors() {
  const setCustomColor = (colorVar, value) => {
    document.documentElement.style.setProperty(colorVar, value);
  };
  
  const changePrimaryColor = (color) => {
    // Update Tailwind's primary color at runtime
    setCustomColor('--color-primary-500', color);
  };
  
  return {
    setCustomColor,
    changePrimaryColor
  };
}
```

---

## API Client Implementation

### API Client

```javascript
// src/api/client.js
const BASE_URL = import.meta.env.VITE_API_BASE_URL || 'http://localhost:3000';

class ApiClient {
  constructor(baseUrl = BASE_URL) {
    this.baseUrl = baseUrl;
  }
  
  async request(endpoint, options = {}) {
    const url = `${this.baseUrl}${endpoint}`;
    
    try {
      const response = await fetch(url, {
        headers: {
          'Content-Type': 'application/json',
          ...options.headers
        },
        ...options
      });
      
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }
      
      return await response.json();
    } catch (error) {
      console.error('API request failed:', error);
      throw error;
    }
  }
  
  // Dashboard endpoints
  getStatus(params = {}) {
    const query = new URLSearchParams(params).toString();
    return this.request(`/api/v1/status?${query}`);
  }
  
  getQueues(params = {}) {
    const query = new URLSearchParams(params).toString();
    return this.request(`/api/v1/status/queues?${query}`);
  }
  
  getQueueDetail(queueName, params = {}) {
    const query = new URLSearchParams(params).toString();
    return this.request(`/api/v1/status/queues/${queueName}?${query}`);
  }
  
  getQueueMessages(queueName, params = {}) {
    const query = new URLSearchParams(params).toString();
    return this.request(`/api/v1/status/queues/${queueName}/messages?${query}`);
  }
  
  getAnalytics(params = {}) {
    const query = new URLSearchParams(params).toString();
    return this.request(`/api/v1/status/analytics?${query}`);
  }
}

export default new ApiClient();
```

### API Composable with Polling

```javascript
// src/composables/useApi.js
import { ref, onMounted, onUnmounted } from 'vue';
import apiClient from '../api/client';

export function useApi(endpoint, params = {}, options = {}) {
  const data = ref(null);
  const loading = ref(false);
  const error = ref(null);
  const { autoRefresh = false, interval = 5000 } = options;
  
  let intervalId = null;
  
  const fetch = async () => {
    loading.value = true;
    error.value = null;
    
    try {
      data.value = await apiClient[endpoint](params);
    } catch (err) {
      error.value = err.message;
      console.error(`Error fetching ${endpoint}:`, err);
    } finally {
      loading.value = false;
    }
  };
  
  const startPolling = () => {
    if (autoRefresh && !intervalId) {
      intervalId = setInterval(fetch, interval);
    }
  };
  
  const stopPolling = () => {
    if (intervalId) {
      clearInterval(intervalId);
      intervalId = null;
    }
  };
  
  onMounted(() => {
    fetch();
    startPolling();
  });
  
  onUnmounted(() => {
    stopPolling();
  });
  
  return { data, loading, error, fetch, startPolling, stopPolling };
}
```

---

## Routing Configuration

```javascript
// src/router.js
import { createRouter, createWebHistory } from 'vue-router';
import Dashboard from './views/Dashboard.vue';
import Queues from './views/Queues.vue';
import QueueDetail from './views/QueueDetail.vue';
import Messages from './views/Messages.vue';
import Analytics from './views/Analytics.vue';
import Settings from './views/Settings.vue';

const routes = [
  {
    path: '/',
    name: 'dashboard',
    component: Dashboard,
    meta: { title: 'Dashboard' }
  },
  {
    path: '/queues',
    name: 'queues',
    component: Queues,
    meta: { title: 'Queues' }
  },
  {
    path: '/queues/:queueName',
    name: 'queue-detail',
    component: QueueDetail,
    meta: { title: 'Queue Detail' }
  },
  {
    path: '/queues/:queueName/messages',
    name: 'messages',
    component: Messages,
    meta: { title: 'Messages' }
  },
  {
    path: '/analytics',
    name: 'analytics',
    component: Analytics,
    meta: { title: 'Analytics' }
  },
  {
    path: '/settings',
    name: 'settings',
    component: Settings,
    meta: { title: 'Settings' }
  }
];

const router = createRouter({
  history: createWebHistory(),
  routes
});

export default router;
```

---

## Data Flow & State Management

### No Vuex/Pinia Needed
- Use Vue 3 Composition API with `ref` and `reactive`
- Composables for shared state (`useTheme`, `useApi`, `useSettings`)
- Props and emits for component communication
- Provide/Inject for deeply nested data

### Auto-refresh Strategy
1. Dashboard: 5 second refresh
2. Queues list: 10 second refresh
3. Queue detail: 5 second refresh
4. Messages: 10 second refresh (optional, can be paused)
5. Analytics: Manual refresh only (heavy queries)

### Error Handling
- Toast notifications for errors (PrimeVue Toast)
- Retry mechanism for failed requests
- Graceful degradation if API is down
- Loading skeletons during data fetch

---

## Responsive Design

### Breakpoints
- **Mobile**: < 768px (stack cards, bottom navigation bar with icons)
- **Tablet**: 768px - 1024px (2-column grid, icon-only sidebar)
- **Desktop**: > 1024px (icon-only sidebar, multi-column layouts)

### Sidebar Design
- **Always collapsed** showing **icons only** (no text labels)
- Tooltips on hover to show full label
- Fixed width (60-70px)
- Vertical icon navigation
- Active route highlighted
- No expand/collapse toggle (stays minimized)

### Mobile Optimizations
- Touch-friendly buttons (min 44px)
- Bottom navigation bar with icons (mobile only)
- Simplified charts on small screens
- Card-based layouts instead of tables
- Same icons as desktop sidebar for consistency

---

## Chart Configurations

### Throughput Chart (Line)
```javascript
{
  type: 'line',
  data: {
    labels: timestamps,
    datasets: [
      { label: 'Ingested', data: ingested, borderColor: '#10B981' },
      { label: 'Processed', data: processed, borderColor: '#3B82F6' }
    ]
  },
  options: {
    responsive: true,
    maintainAspectRatio: false,
    interaction: { mode: 'index', intersect: false },
    plugins: {
      legend: { position: 'top' },
      tooltip: { mode: 'index' }
    }
  }
}
```

### Status Distribution (Doughnut)
```javascript
{
  type: 'doughnut',
  data: {
    labels: ['Pending', 'Processing', 'Completed', 'Failed'],
    datasets: [{
      data: [pending, processing, completed, failed],
      backgroundColor: ['#F59E0B', '#3B82F6', '#10B981', '#EF4444']
    }]
  },
  options: {
    responsive: true,
    plugins: {
      legend: { position: 'bottom' }
    }
  }
}
```

---

## Performance Optimizations

1. **Lazy Loading**: Code-split routes and heavy components
2. **Virtual Scrolling**: For tables with 1000+ rows
3. **Debounced Search**: Wait for user to stop typing
4. **Memoization**: Cache expensive computations
5. **Conditional Polling**: Pause when tab is inactive
6. **Image Optimization**: Use SVG icons, optimize images
7. **Bundle Size**: Tree-shake unused PrimeVue components

---

## Accessibility (a11y)

1. **Keyboard Navigation**: All actions accessible via keyboard
2. **ARIA Labels**: Proper labels for screen readers
3. **Color Contrast**: WCAG AA compliant (4.5:1 minimum)
4. **Focus Indicators**: Clear focus states on interactive elements
5. **Alt Text**: All images have descriptive alt text
6. **Semantic HTML**: Use proper heading hierarchy
7. **Announcements**: Screen reader announcements for dynamic updates

---

## Testing Strategy (Future)

1. **Unit Tests**: Vitest for composables and utilities
2. **Component Tests**: Vue Test Utils for components
3. **E2E Tests**: Playwright for user flows
4. **Visual Regression**: Chromatic or Percy
5. **Accessibility Tests**: axe-core integration

---

## Deployment

### Build Configuration

```javascript
// vite.config.js
import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';

export default defineConfig({
  plugins: [vue()],
  server: {
    port: 8080,
    proxy: {
      '/api': {
        target: 'http://localhost:3000',
        changeOrigin: true
      }
    }
  },
  build: {
    outDir: 'dist',
    sourcemap: true
  }
});
```

### Environment Variables

```bash
# .env.development
VITE_API_BASE_URL=http://localhost:3000

# .env.production
VITE_API_BASE_URL=https://api.yourcompany.com
```

### Docker Support (Optional)

```dockerfile
FROM node:22-alpine
WORKDIR /app
COPY package*.json ./
RUN npm ci
COPY . .
RUN npm run build
EXPOSE 8080
CMD ["npm", "run", "preview"]
```

---

## Dependencies

### package.json

```json
{
  "name": "queen-dashboard",
  "version": "4.0.0",
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "vite build",
    "preview": "vite preview"
  },
  "dependencies": {
    "vue": "^3.5.0",
    "vue-router": "^4.4.0",
    "@nuxt/ui": "^2.18.0",
    "@headlessui/vue": "^1.7.0",
    "@heroicons/vue": "^2.1.0",
    "chart.js": "^4.4.0",
    "vue-chartjs": "^5.3.0",
    "pinia": "^2.2.0"
  },
  "devDependencies": {
    "@vitejs/plugin-vue": "^5.0.0",
    "vite": "^5.4.0",
    "tailwindcss": "^3.4.0",
    "autoprefixer": "^10.4.0",
    "postcss": "^8.4.0"
  }
}
```

---

## Implementation Phases

### Phase 1: Project Setup & Layout (Day 1)
- ✅ Initialize Vite + Vue 3 project
- ✅ Install Nuxt UI and dependencies (Tailwind CSS, Heroicons)
- ✅ Configure Tailwind CSS and Nuxt UI
- ✅ Create app.config.ts for theme configuration
- ✅ Create layout components (AppLayout, AppHeader, AppSidebar)
- ✅ Set up Vue Router
- ✅ Implement dark/light mode switching with useColorMode
- ✅ Create API client

### Phase 2: Dashboard & Queues (Day 2)
- ✅ Dashboard overview page with UCard metric cards
- ✅ Throughput chart component with Chart.js
- ✅ Queues list page with UTable
- ✅ Queue filters with USelect and UInput
- ✅ Auto-refresh implementation with composables

### Phase 3: Queue Detail & Messages (Day 3)
- ✅ Queue detail page with partition breakdown
- ✅ Messages browser page with UTable
- ✅ Message detail UModal/USlideover
- ✅ Status UBadge components
- ✅ Copy to clipboard with useClipboard composable

### Phase 4: Analytics & Charts (Day 4)
- ✅ Analytics page layout with UTabs
- ✅ Multiple chart components (throughput, latency, errors)
- ✅ Top queues UTable
- ✅ DLQ panel with error breakdown
- ✅ Time range UButtonGroup and interval selectors

### Phase 5: Polish & Features (Day 5)
- ✅ Settings page with UFormGroup and UToggle
- ✅ Command palette (UCommandPalette) for quick navigation
- ✅ Responsive design with Tailwind breakpoints
- ✅ Loading states with USkeleton
- ✅ UNotification for toast messages
- ✅ Export functionality (CSV)
- ✅ Performance optimizations

---

## Success Criteria

1. ✅ All 5 API endpoints integrated and working
2. ✅ Dark/Light theme fully functional
3. ✅ Responsive on mobile, tablet, and desktop
4. ✅ Auto-refresh working without memory leaks
5. ✅ Bar charts displaying real-time data (no lines, no splines)
6. ✅ No console errors or warnings
7. ✅ Fast initial load (< 3 seconds)
8. ✅ Beautiful, professional UI matching Nuxt UI dashboard template
9. ✅ Accessible (keyboard navigation, screen reader friendly)
10. ✅ Production-ready build

---

## Styling Rules

### ✅ DO's:
1. **Use Nuxt UI components** with their built-in props (`color`, `variant`, `size`, etc.)
2. **Use Tailwind utility classes** for all styling (`bg-white`, `text-gray-900`, `p-4`, `space-y-4`)
3. **Use `dark:` variant** for dark mode styling
4. **Define theme ONCE** in `src/app.config.ts`
5. **Use semantic color names** (`primary`, `green`, `red`, `yellow`, `blue`)

### ❌ DON'T's:
1. **No inline styles** with hardcoded hex colors
2. **No custom CSS** files unless absolutely necessary
3. **No hardcoded hex values** in components
4. **No !important** overrides
5. **No mocking data** - all components must be functional

### Example: StatusBadge Component

```vue
<!-- ✅ CORRECT: Use Nuxt UI Badge component -->
<template>
  <UBadge :label="status" :color="getColor(status)" />
</template>

<script setup>
const props = defineProps({
  status: String
});

const getColor = (status) => {
  const map = {
    'completed': 'green',
    'failed': 'red',
    'pending': 'yellow',
    'processing': 'blue'
  };
  return map[status] || 'gray';
};
</script>
```

### File Structure for Styling

```
dashboard/
└── src/
    ├── main.js            # Import Nuxt UI
    ├── app.config.ts      # ✅ ONLY file with theme config
    ├── assets/main.css    # Tailwind imports only
    └── components/
        └── *.vue          # Use Tailwind classes, minimal <style>
```

**Single source of truth**: `src/app.config.ts`

```typescript
// ✅ All theme config here
export default defineAppConfig({
  ui: {
    primary: 'green',
    gray: 'slate'
  }
});
```

---

## Configuration

### Server Configuration
- **API Server**: `http://localhost:6632`
- **Vite Dev Server**: Port `4000`

### API Endpoints
- `GET http://localhost:6632/api/v1/status` - System overview
- `GET http://localhost:6632/api/v1/status/queues` - All queues
- `GET http://localhost:6632/api/v1/status/queues/:queueName` - Queue detail
- `GET http://localhost:6632/api/v1/status/queues/:queueName/messages` - Messages
- `GET http://localhost:6632/api/v1/status/analytics` - Analytics data

---

## Conclusion

This implementation plan provides a comprehensive blueprint for building a beautiful, functional, and performant dashboard for the Queen Message Queue system. The use of Nuxt UI ensures a modern, accessible, and consistent UI based on the proven Nuxt UI dashboard template, while Vue 3 Composition API provides a clean and maintainable codebase.

The dashboard will serve as a powerful monitoring tool, giving users real-time insights into their message queue performance, helping them identify bottlenecks, track message flow, and troubleshoot issues efficiently.

**Ready to implement!** 🚀

