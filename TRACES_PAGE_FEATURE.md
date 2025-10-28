# Traces Page Feature

## Overview
Added a dedicated **Traces** page in the frontend that allows users to search and explore message traces by `traceName` across the entire system. This provides powerful cross-message correlation and workflow visualization capabilities.

## Implementation Summary

### ✅ New Components

#### 1. **Traces.vue** - Main View
**Location:** `webapp/src/views/Traces.vue`

**Features:**
- **Search Interface**: Clean search box for entering trace names
- **Quick Examples**: Clickable example trace names for easy discovery
- **Results Timeline**: Chronological display of all traces matching the search
- **Summary Statistics**: Shows total traces, unique messages, and unique queues
- **Trace Details**: Each trace shows:
  - Event type with color-coded indicator
  - Timestamp
  - Associated trace names (with highlight for searched name)
  - Queue and partition information
  - Transaction ID
  - User data (text + expandable JSON)
  - Worker ID and consumer group metadata
- **Pagination**: Navigate through large result sets
- **Message Detail Integration**: Click any trace to view full message details
- **Responsive Design**: Works on mobile and desktop
- **Dark Mode Support**: Full support for dark theme

#### 2. **Router Configuration**
**Location:** `webapp/src/router/index.js`

Added route:
```javascript
{
  path: '/traces',
  name: 'Traces',
  component: () => import('../views/Traces.vue'),
}
```

#### 3. **Navigation Menu**
**Location:** `webapp/src/components/layout/AppSidebar.vue`

Added navigation item:
```javascript
{ 
  name: 'Traces', 
  path: '/traces',
  iconPath: 'M9 20l-5.447-2.724A1 1 0 013 16.382V5.618a1 1 0 011.447-.894L9 7m0 13l6-3m-6 3V7m6 10l4.553 2.276A1 1 0 0021 18.382V7.618a1 1 0 00-.553-.894L15 4m0 13V4m0 0L9 7'
}
```

## Usage

### Accessing the Traces Page
1. Navigate to **Traces** in the sidebar menu
2. Or visit `http://localhost:5173/traces` directly

### Searching for Traces
1. Enter a trace name in the search box (e.g., `tenant-acme`, `order-flow-123`)
2. Press Enter or click the "Search" button
3. View all traces across all messages that have that trace name

### Understanding Results
- **Timeline View**: Traces are displayed chronologically
- **Color Indicators**: Each trace type has a colored dot:
  - 🔵 Blue = Info
  - 🟢 Green = Processing
  - 🟣 Purple = Step
  - 🔴 Red = Error
  - 🟡 Yellow = Warning
- **Trace Names**: Multiple trace names shown as badges (searched name highlighted in rose)
- **Click to View**: Click any trace to see full message details in the side panel

### Example Workflows

#### 1. Track Multi-Tenant Processing
```javascript
// Messages from different services tagged with tenant
await msg.trace({
  traceName: ['tenant-acme', 'service-orders'],
  data: { text: 'Order created' }
});

await msg.trace({
  traceName: ['tenant-acme', 'service-inventory'],
  data: { text: 'Inventory checked' }
});

await msg.trace({
  traceName: ['tenant-acme', 'service-payment'],
  data: { text: 'Payment processed' }
});

// Search for "tenant-acme" to see entire workflow across all services
```

#### 2. Debug Distributed Workflows
```javascript
const workflowId = `order-${orderId}`;

// Service 1
await msg.trace({
  traceName: [workflowId, 'orders'],
  data: { text: 'Order validated', service: 'order-service' }
});

// Service 2
await msg.trace({
  traceName: [workflowId, 'inventory'],
  data: { text: 'Stock reserved', service: 'inventory-service' }
});

// Service 3
await msg.trace({
  traceName: [workflowId, 'shipping'],
  data: { text: 'Shipping label created', service: 'shipping-service' }
});

// Search for "order-12345" to see complete order flow
```

#### 3. Monitor User Journeys
```javascript
// Track user actions across different queues
await msg.trace({
  traceName: [`user-${userId}`, 'signup-flow'],
  data: { text: 'Account created' }
});

await msg.trace({
  traceName: [`user-${userId}`, 'verification-flow'],
  data: { text: 'Email verified' }
});

await msg.trace({
  traceName: [`user-${userId}`, 'onboarding-flow'],
  data: { text: 'Profile completed' }
});

// Search for "user-456" to see complete user journey
```

## Features

### ✅ **Cross-Message Correlation**
- View traces from multiple messages with the same trace name
- Perfect for distributed workflows and multi-service architectures

### ✅ **Timeline Visualization**
- Chronological display of events
- Easy to follow processing flow
- Identify bottlenecks and errors

### ✅ **Rich Metadata**
- Event types with color coding
- Worker and consumer group information
- Queue and partition details
- Full message payload access

### ✅ **Search & Filter**
- Fast search by trace name
- Pagination for large result sets
- Summary statistics

### ✅ **Integrated Details**
- Click any trace to view full message
- Access to message detail panel
- Retry/delete/DLQ actions available

### ✅ **Responsive & Accessible**
- Works on mobile and desktop
- Dark mode support
- Keyboard navigation

## API Integration

The Traces page uses the existing API endpoint:
```
GET /api/v1/traces/by-name/:traceName?limit=50&offset=0
```

Returns traces with:
- Event data
- Trace names (all associated names)
- Message information (queue, partition, transaction ID)
- Metadata (worker, consumer group, timestamp)

## Navigation Flow

```
Dashboard → Traces
              ↓
         Search by name
              ↓
       View trace timeline
              ↓
    Click trace → Message Detail Panel
                        ↓
                  View full message
                        ↓
                Retry/Delete/DLQ actions
```

## Design Decisions

### **Why a Dedicated Page?**
- Traces are fundamentally different from messages (cross-message correlation)
- Search-first interface optimized for trace discovery
- Better suited for debugging and workflow monitoring
- Keeps Messages page focused on individual message management

### **Why Search Instead of List?**
- Trace names are dynamic and user-defined
- Infinite possible trace names (can't list all)
- Search is more intuitive for finding specific workflows
- Example suggestions guide users

### **Why Timeline View?**
- Chronological order shows workflow progression
- Natural way to understand distributed processing
- Easy to spot delays or errors in sequence
- Matches mental model of "what happened when"

## Future Enhancements

Potential improvements:
- **Trace Name Autocomplete**: Suggest trace names as you type
- **Advanced Filtering**: Filter by event type, date range, queue
- **Visual Flow Diagram**: Graph view of message relationships
- **Export**: Download traces as JSON or CSV
- **Live Updates**: Real-time trace streaming
- **Trace Analytics**: Aggregated stats per trace name
- **Search History**: Recent searches saved locally

## Screenshots Placeholders

### Empty State
- Shows search box with examples
- Guides user to enter trace name

### Search Results
- Timeline of traces
- Summary statistics banner
- Color-coded event types
- Clickable traces

### Trace Details
- Expanded JSON data
- Multiple trace names displayed
- Metadata footer

## Testing

To test the Traces page:

1. **Generate traces** in your consumer:
```javascript
await msg.trace({
  traceName: ['test-workflow', 'tenant-demo'],
  data: { text: 'Test trace event', step: 1 }
});
```

2. **Navigate to Traces** page in the UI

3. **Search** for `test-workflow`

4. **Verify** results show up correctly

5. **Click a trace** to view message details

## Related Documentation

- [TRACING_IMPLEMENTATION.md](TRACING_IMPLEMENTATION.md) - Core tracing feature implementation
- [API.md](API.md) - API endpoints documentation
- [client-v2/README.md](client-js/client-v2/README.md) - Client library usage

## Summary

The Traces page provides a powerful interface for:
- 🔍 Searching traces by name
- 📊 Visualizing distributed workflows
- 🐛 Debugging cross-service issues
- 📈 Monitoring multi-tenant operations
- 🔗 Correlating related messages

It's an essential tool for understanding message processing flows in complex, distributed systems built with Queen.

