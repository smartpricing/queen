# Queen Dashboard V3 - PrimeVue Dark Theme Redesign Plan

## Vision
Transform the Queen Dashboard into a modern, dark-themed application inspired by PrimeVue's showcase design, featuring the Aura theme with rose/pink primary colors and sophisticated data visualization.

## Design Reference
Based on [PrimeVue's homepage dashboard](https://primevue.org/) showcasing:
- Dark mode with Aura theme
- Rose/pink gradient primary colors
- Modern card-based layouts
- Sophisticated charts and data tables
- Glass-morphism and subtle depth effects

## Technology Stack (No Changes)
- Vue 3 (Composition API)
- PrimeVue 4.x with Aura Theme
- Chart.js with PrimeVue Chart
- Vite
- JavaScript (ESM modules)

## Theme Configuration

### Color Palette
```css
/* Dark Mode Colors - Aura Theme with Rose Primary */
--surface-0: #0f172a;        /* Darkest background */
--surface-50: #1e293b;       /* Card backgrounds */
--surface-100: #334155;      /* Elevated surfaces */
--surface-200: #475569;      /* Borders, dividers */
--surface-300: #64748b;      /* Disabled text */
--surface-400: #94a3b8;      /* Muted text */
--surface-500: #cbd5e1;      /* Regular text */
--surface-600: #e2e8f0;      /* Emphasized text */
--surface-700: #f1f5f9;      /* Bright text */

/* Rose/Pink Primary Colors */
--primary-50: #fdf2f8;
--primary-100: #fce7f3;
--primary-200: #fbcfe8;
--primary-300: #f9a8d4;
--primary-400: #f472b6;
--primary-500: #ec4899;      /* Main rose */
--primary-600: #db2777;
--primary-700: #be185d;
--primary-800: #9f1239;
--primary-900: #881337;

/* Gradient Definitions */
--gradient-primary: linear-gradient(135deg, #ec4899 0%, #db2777 100%);
--gradient-chart: linear-gradient(180deg, rgba(236, 72, 153, 0.4) 0%, rgba(236, 72, 153, 0.1) 100%);
```

### Typography
- Font Family: Inter, system-ui
- Base Size: 14px
- Scale: 0.75rem to 1.875rem
- Weight: 300-700

## Component Redesign

### 1. Layout Structure
```
┌─────────────────────────────────────────────────────┐
│                    Top Bar (Dark)                   │
├──────┬──────────────────────────────────────────────┤
│      │                                              │
│  S   │            Main Content Area                 │
│  i   │         (Dark background #0f172a)            │
│  d   │                                              │
│  e   │    ┌──────────────────────────────┐          │
│  b   │    │   Cards with #1e293b bg      │          │
│  a   │    └──────────────────────────────┘          │
│  r   │                                              │
│      │                                              │
└──────┴──────────────────────────────────────────────┘
```

### 2. Sidebar Redesign
- **Width**: 80px collapsed, 250px expanded
- **Background**: #1e293b with subtle border
- **Icons**: Outline style, 20px size
- **Active Item**: Rose gradient background with glow effect
- **User Profile**: Avatar at bottom with status indicator
- **Navigation Items**:
  ```
  🏠 Dashboard
  📊 Queues
  📈 Analytics  
  ✉️ Messages
  ⚙️ System
  👤 Profile (bottom)
  ```

### 3. Top Bar
- **Height**: 64px
- **Background**: Transparent with backdrop blur
- **Content**:
  - Left: Menu toggle + Logo
  - Center: Page title
  - Right: Search bar, notifications, theme toggle, settings

### 4. Dashboard Cards

#### 4.1 Metric Cards (Redesigned)
```html
<div class="metric-card-v3">
  <div class="metric-icon-wrapper">
    <i class="pi pi-envelope"></i>
  </div>
  <div class="metric-content">
    <p class="metric-label">Total Messages</p>
    <h3 class="metric-value">1.9K</h3>
    <p class="metric-change">
      <i class="pi pi-arrow-up"></i>
      <span>12.5%</span>
    </p>
  </div>
  <div class="metric-sparkline">
    <!-- Mini chart here -->
  </div>
</div>
```

**Styling**:
- Background: #1e293b
- Border: 1px solid rgba(236, 72, 153, 0.2)
- Hover: Subtle glow effect
- Icon: Rose gradient background

#### 4.2 Charts (PrimeVue Dark Style for Message Queue Data)

**Message Throughput Chart** (Styled like Crypto Analytics):
```javascript
{
  type: 'bar',
  data: {
    labels: ['00:00', '01:00', '02:00', ...], // Time periods
    datasets: [
      {
        label: 'Incoming Messages',
        backgroundColor: '#ec4899',
        data: [...] // Actual message counts
      },
      {
        label: 'Completed Messages',
        backgroundColor: 'rgba(236, 72, 153, 0.5)',
        data: [...]
      },
      {
        label: 'Failed Messages',
        backgroundColor: 'rgba(236, 72, 153, 0.2)',
        data: [...]
      }
    ]
  },
  options: {
    // Stacked bar chart for message flow visualization
    scales: {
      x: { stacked: true },
      y: { stacked: true }
    }
  }
}
```

#### 4.3 Queue Table (Transaction Table Style)
```html
<DataTable class="dark-table-v3">
  <Column field="name" header="Queue Name">
    <template #body="{data}">
      <div class="queue-name-cell">
        <div class="queue-icon">Q</div>
        <span>{{data.name}}</span>
      </div>
    </template>
  </Column>
  <Column field="namespace" header="Namespace" />
  <Column field="pending" header="Pending">
    <template #body="{data}">
      <Badge value="{{data.pending}}" severity="warning" />
    </template>
  </Column>
  <Column field="processing" header="Processing">
    <template #body="{data}">
      <Badge value="{{data.processing}}" severity="info" />
    </template>
  </Column>
  <Column field="rate" header="Rate">
    <template #body="{data}">
      <span class="rate-value">{{data.rate}} msg/min</span>
    </template>
  </Column>
</DataTable>
```

**Features**:
- Dark header (#0f172a)
- Row hover with rose glow
- Action buttons with gradient
- Status badges for message counts

#### 4.4 Queue Distribution (My Wallet Style)
```html
<div class="distribution-card">
  <div class="distribution-header">
    <h3>Queue Distribution</h3>
    <Button icon="pi pi-ellipsis-h" />
  </div>
  <div class="distribution-bar">
    <!-- Colorful segments showing message distribution across queues -->
    <div class="segment" style="width: 25%; background: #ec4899" 
         title="email-queue: 25%"></div>
    <div class="segment" style="width: 15%; background: #10b981"
         title="notification-queue: 15%"></div>
    <div class="segment" style="width: 30%; background: #f59e0b"
         title="analytics-queue: 30%"></div>
    <div class="segment" style="width: 30%; background: #3b82f6"
         title="payment-queue: 30%"></div>
  </div>
  <div class="distribution-legend">
    <div class="legend-item">
      <span class="dot" style="background: #ec4899"></span>
      <span>email-queue (250 msgs)</span>
    </div>
    <!-- More queue legend items -->
  </div>
</div>
```

### 5. Component Specific Styles

#### Cards
```css
.card-v3 {
  background: #1e293b;
  border: 1px solid rgba(255, 255, 255, 0.05);
  border-radius: 12px;
  padding: 1.5rem;
  position: relative;
  overflow: hidden;
}

.card-v3::before {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 1px;
  background: linear-gradient(90deg, 
    transparent,
    rgba(236, 72, 153, 0.5),
    transparent
  );
}
```

#### Buttons
```css
.btn-primary {
  background: linear-gradient(135deg, #ec4899, #db2777);
  border: none;
  color: white;
  padding: 0.625rem 1.25rem;
  border-radius: 8px;
  font-weight: 500;
  box-shadow: 0 4px 14px rgba(236, 72, 153, 0.3);
}

.btn-secondary {
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid rgba(255, 255, 255, 0.1);
  color: #cbd5e1;
}
```

#### Input Fields
```css
.input-v3 {
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 8px;
  color: #e2e8f0;
  padding: 0.625rem 1rem;
}

.input-v3:focus {
  border-color: #ec4899;
  box-shadow: 0 0 0 3px rgba(236, 72, 153, 0.1);
}
```

### 6. Animations & Interactions

#### Hover Effects
- Cards: Subtle lift with glow
- Buttons: Brightness increase
- Table rows: Rose highlight
- Links: Underline animation

#### Transitions
```css
/* Smooth transitions for all interactive elements */
* {
  transition: background-color 0.2s, 
              border-color 0.2s,
              box-shadow 0.2s,
              transform 0.2s;
}
```

#### Loading States
- Skeleton screens with animated gradients
- Pulse animations for pending items
- Shimmer effects on data loading

### 7. Responsive Breakpoints
- Desktop: 1440px+ (full layout)
- Laptop: 1024px-1439px (adjusted spacing)
- Tablet: 768px-1023px (collapsed sidebar)
- Mobile: <768px (bottom navigation)

### 8. View-Specific Redesigns (Queen Message Queue Data)

#### Dashboard View
- **Metric Cards**: Total Messages, Pending, Processing, Completed Today, Failed Today, Messages/sec
- **Throughput Chart**: Message flow (Incoming/Completed/Failed) using stacked bar chart style from PrimeVue
- **Top Queues Table**: Styled like the Transactions table but showing Queue Name, Namespace, Pending, Processing, Rate
- **Queue Distribution**: Colorful segmented bar (like My Wallet) showing distribution across queues
- **Live Activity Feed**: Real-time message events in dark cards

#### Queues View
- **Queue Cards**: Each queue as a dark card with metrics
- **Queue Table**: Dark themed table showing all queues with their statistics
- **Actions**: Configure, Clear, View buttons with rose gradient

#### Analytics View
- **Throughput Chart**: Full-width dark chart with rose gradients
- **Queue Performance**: Dark table with queue metrics
- **System Metrics**: Cards showing Database Connections, Memory, Request Rate, Latency

#### Messages View
- **Message Browser**: Dark table showing Transaction ID, Queue, Status, Created date
- **Message Detail**: Dark modal with JSON payload viewer
- **Filters**: Queue and Status dropdowns with dark theme

#### System View
- **Health Status**: Dark cards with system health metrics
- **Memory Usage**: Progress bars with rose accent
- **Database Pool**: Connection statistics in dark cards
- **Configuration**: System settings in dark themed cards

### 9. PrimeVue Configuration

```javascript
// main.js
import PrimeVue from 'primevue/config';
import Aura from '@primevue/themes/aura';

app.use(PrimeVue, {
  theme: {
    preset: Aura,
    options: {
      primary: 'rose',
      darkModeSelector: '.dark',
      cssLayer: {
        name: 'primevue',
        order: 'tailwind-base, primevue, tailwind-utilities'
      }
    }
  },
  ripple: true
});
```

### 10. Implementation Phases

#### Phase 1: Core Theme Setup (Day 1)
- [ ] Configure Aura theme with rose primary
- [ ] Set up dark mode variables
- [ ] Update base layout components
- [ ] Implement new color system

#### Phase 2: Component Library (Day 2)
- [ ] Create new metric card component
- [ ] Update chart configurations
- [ ] Redesign data tables
- [ ] Build distribution bar component

#### Phase 3: Layout Redesign (Day 3)
- [ ] Implement new sidebar with icons
- [ ] Create glass-morphism top bar
- [ ] Update navigation system
- [ ] Add user profile section

#### Phase 4: View Updates (Day 4-5)
- [ ] Redesign Dashboard view
- [ ] Update Queues view with cards
- [ ] Enhance Analytics with new charts
- [ ] Modernize Messages view
- [ ] Revamp System view

#### Phase 5: Polish & Optimization (Day 6)
- [ ] Add animations and transitions
- [ ] Implement loading states
- [ ] Optimize for performance
- [ ] Test responsive design
- [ ] Fix edge cases

### 11. Key Features to Implement

#### Visual Effects
- Glass morphism on elevated elements
- Gradient borders and accents
- Subtle glow effects on hover
- Smooth color transitions
- Animated number counters

#### Interactive Elements
- Ripple effect on clicks
- Tooltip on hover
- Expandable cards
- Draggable widgets
- Keyboard shortcuts

#### Data Visualization
- Animated chart transitions
- Interactive legends
- Zoom and pan capabilities
- Export functionality
- Real-time updates with animations

### 12. Performance Considerations
- Use CSS variables for theme switching
- Lazy load heavy components
- Optimize chart rendering
- Implement virtual scrolling for long lists
- Cache static assets

### 13. Accessibility
- ARIA labels for all interactive elements
- Keyboard navigation support
- Focus indicators with rose color
- Screen reader announcements
- High contrast mode option

### 14. Testing Strategy
- Component testing with Vitest
- E2E testing with Playwright
- Visual regression testing
- Performance monitoring
- Accessibility audits

## Success Metrics
- Load time < 2 seconds
- Lighthouse score > 90
- Smooth 60fps animations
- Mobile-first responsive design
- WCAG 2.1 AA compliance

## Deliverables
1. Fully themed dark mode dashboard
2. Reusable component library
3. Responsive design system
4. Documentation and style guide
5. Performance optimization report

## Timeline
- Week 1: Theme setup and component development
- Week 2: View implementation and testing
- Week 3: Polish, optimization, and deployment

## Inspiration Sources
- [PrimeVue Showcase](https://primevue.org/)
- Modern crypto dashboards
- Financial trading platforms
- Dark mode design patterns
- Glass morphism trends
