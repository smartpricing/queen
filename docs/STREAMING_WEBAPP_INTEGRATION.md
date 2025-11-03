# Streaming Integration - Web Application

## Overview

Added comprehensive streaming support to the Queen web application, including dashboard metrics and a dedicated Streams management page.

---

## Frontend Components

### 1. **Streams API Module** (`webapp/src/api/streams.js`)

New API client for streaming endpoints:

```javascript
export const streamsApi = {
  getStreams: () => apiClient.get('/api/v1/resources/streams'),
  getStream: (streamName) => apiClient.get(`/api/v1/resources/streams/${streamName}`),
  defineStream: (data) => apiClient.post('/api/v1/stream/define', data),
  deleteStream: (streamName) => apiClient.delete(`/api/v1/resources/streams/${streamName}`),
  getStreamConsumers: (streamName) => apiClient.get(`/api/v1/resources/streams/${streamName}/consumers`),
  seekStream: (data) => apiClient.post('/api/v1/stream/seek', data),
  getStreamStats: () => apiClient.get('/api/v1/resources/streams/stats'),
};
```

### 2. **Dashboard Enhancements** (`webapp/src/views/Dashboard.vue`)

Added streaming metrics card to the dashboard:

**New Card:**
- **STREAMS** - Shows total streams count (purple-colored)
  - Subtext: Number of active windows being processed
  - Clickable: Navigates to `/streams` page

**Integration:**
- Fetches streaming stats on dashboard load
- Gracefully handles if streaming is not supported
- Auto-refreshes with other dashboard metrics

### 3. **Streams Page** (`webapp/src/views/Streams.vue`)

Full-featured streams management interface:

**Features:**
- **Search & Filter**: Search by stream name, filter by namespace
- **Sortable Table** with columns:
  - Stream Name (with source queue count)
  - Namespace
  - Type (Partitioned/Global badge)
  - Window Duration (human-readable)
  - Active Leases count
  - Consumer Groups count
  - Delete action

- **Pagination**: 20 streams per page
- **Responsive Design**: Mobile and desktop optimized
- **Dark Mode Support**: Full theme integration
- **Empty State**: Helpful message when no streams exist

### 4. **Create Stream Modal** (`webapp/src/components/streams/CreateStreamModal.vue`)

Beautiful modal for stream creation:

**Form Fields:**
- Stream name (required)
- Namespace (required)
- Partitioned toggle switch
- Source queues (add/remove multiple)
- Window configuration:
  - Window type selector (tumbling)
  - Window duration (ms) with live preview
  - Grace period (ms) with live preview
  - Lease timeout (ms) with live preview

**Features:**
- Real-time duration formatting (converts ms → days/hours/minutes/seconds)
- Input validation
- Error handling with user-friendly messages
- Smooth animations

### 5. **Navigation Updates**

**Sidebar** (`webapp/src/components/layout/AppSidebar.vue`):
- Added **"Streams"** navigation item (between Consumer Groups and Messages)
- Cloud upload icon
- Refresh support for `/streams` route

**Router** (`webapp/src/router/index.js`):
- New route: `/streams` → `Streams.vue`

---

## Backend API Routes

Added 5 new REST endpoints in `server/src/acceptor_server.cpp`:

### 1. **GET `/api/v1/resources/streams`**

Returns list of all streams with metadata.

**Response:**
```json
{
  "streams": [
    {
      "id": "uuid",
      "name": "my-stream",
      "namespace": "analytics",
      "partitioned": false,
      "windowType": "tumbling",
      "windowDurationMs": 60000,
      "windowGracePeriodMs": 30000,
      "windowLeaseTimeoutMs": 60000,
      "sourceQueues": ["queue1", "queue2"],
      "activeLeases": 5,
      "consumerGroups": 2,
      "createdAt": "2025-11-03T08:51:14.012+00",
      "updatedAt": "2025-11-03T08:51:14.012+00"
    }
  ]
}
```

### 2. **GET `/api/v1/resources/streams/stats`**

Returns streaming statistics for dashboard.

**Response:**
```json
{
  "totalStreams": 10,
  "activeLeases": 25
}
```

### 3. **GET `/api/v1/resources/streams/:streamName`**

Returns detailed information about a specific stream.

**Response:**
```json
{
  "id": "uuid",
  "name": "my-stream",
  "namespace": "analytics",
  "partitioned": false,
  "windowType": "tumbling",
  "windowDurationMs": 60000,
  "windowGracePeriodMs": 30000,
  "windowLeaseTimeoutMs": 60000,
  "createdAt": "2025-11-03T08:51:14.012+00",
  "updatedAt": "2025-11-03T08:51:14.012+00"
}
```

### 4. **GET `/api/v1/resources/streams/:streamName/consumers`**

Returns all consumer groups consuming from a stream.

**Response:**
```json
{
  "consumers": [
    {
      "consumerGroup": "processor-1",
      "streamKey": "__GLOBAL__",
      "lastAckedWindowEnd": "2025-11-03T10:00:00.000000+00",
      "totalWindowsConsumed": 150,
      "lastConsumedAt": "2025-11-03T10:05:00.123456+00"
    }
  ]
}
```

### 5. **DELETE `/api/v1/resources/streams/:streamName`**

Deletes a stream by name.

**Response:**
```json
{
  "success": true,
  "message": "Stream deleted successfully"
}
```

---

## Database Tables Used

The resource endpoints query these streaming tables:

1. **`queen.streams`** - Stream definitions
2. **`queen.stream_sources`** - Stream-to-queue mappings
3. **`queen.stream_leases`** - Active window leases
4. **`queen.stream_consumer_offsets`** - Consumer progress tracking

---

## UI/UX Features

### Color Scheme
- **Purple theme** for streaming components (matching Queen branding)
- Consistent with existing dashboard colors

### User Experience
- **Clickable cards** on dashboard with arrow indicators
- **Smooth navigation** between pages
- **Real-time updates** via refresh button
- **Error handling** with friendly messages
- **Loading states** during API calls
- **Confirmation dialogs** before destructive actions

### Responsive Design
- Mobile-optimized layouts
- Collapsible sidebar
- Adaptive table columns
- Touch-friendly buttons

### Dark Mode
- Full dark mode support throughout
- Consistent color palette
- Proper contrast ratios

---

## Usage Example

### Creating a Stream via UI

1. Navigate to **Streams** page from sidebar
2. Click **"New Stream"** button
3. Fill in stream details:
   - Name: `user-analytics-stream`
   - Namespace: `analytics`
   - Toggle: Partitioned = OFF (global stream)
   - Source Queues: Add `user-events`, `page-views`
   - Window Duration: 300000 (5 minutes)
   - Grace Period: 60000 (1 minute)
   - Lease Timeout: 120000 (2 minutes)
4. Click **"Create Stream"**
5. Stream appears in table immediately

### Viewing Stream Details

- Click on any stream row to view details (coming soon)
- Dashboard shows live count of active windows
- Monitor consumer group progress

### Deleting a Stream

1. Click trash icon on stream row
2. Confirm deletion in dialog
3. Stream removed from table

---

## Implementation Notes

### Global Variables Used
The backend routes use `global_db_pool` to access the database connection pool, as it's shared across all worker threads.

### Error Handling
- **Frontend**: Shows user-friendly error messages in modal/alert
- **Backend**: Returns appropriate HTTP status codes (404, 400, 500)
- **Database**: Gracefully handles missing data with COALESCE and empty checks

### Performance
- Efficient SQL queries with proper joins
- Indexed lookups on stream names
- Pagination for large stream lists
- Minimal API calls on page load

---

## Future Enhancements

Potential improvements:
1. Stream detail page with real-time window visualization
2. Consumer group management (seek interface)
3. Stream metrics charts (throughput, lag)
4. Window history viewer
5. Real-time WebSocket updates for active windows
6. Batch stream operations
7. Stream templates/presets
8. Export/import stream configurations

---

## Testing

### Manual Testing Checklist

- [ ] Dashboard loads and shows stream count
- [ ] Click on Streams card navigates to Streams page
- [ ] Streams page loads and displays all streams
- [ ] Search and filtering work correctly
- [ ] Sorting by different columns works
- [ ] Create Stream modal opens and validates input
- [ ] Create Stream successfully creates a stream
- [ ] Delete Stream confirmation works
- [ ] Delete Stream successfully removes stream
- [ ] Refresh button updates stream list
- [ ] Dark mode works correctly on all pages
- [ ] Mobile responsive design works
- [ ] Navigation between pages is smooth

---

## Database Schema Reference

The streaming feature uses these tables (already created in `initialize_schema()`):

```sql
-- Stream definitions
CREATE TABLE queen.streams (
    id UUID PRIMARY KEY,
    name VARCHAR(255) UNIQUE NOT NULL,
    namespace VARCHAR(255) NOT NULL,
    partitioned BOOLEAN NOT NULL DEFAULT FALSE,
    window_type VARCHAR(50) NOT NULL,
    window_duration_ms BIGINT,
    window_grace_period_ms BIGINT NOT NULL DEFAULT 30000,
    window_lease_timeout_ms BIGINT NOT NULL DEFAULT 30000,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

-- Stream source mappings
CREATE TABLE queen.stream_sources (
    stream_id UUID REFERENCES queen.streams(id) ON DELETE CASCADE,
    queue_id UUID REFERENCES queen.queues(id) ON DELETE CASCADE,
    PRIMARY KEY (stream_id, queue_id)
);

-- Consumer offsets
CREATE TABLE queen.stream_consumer_offsets (
    stream_id UUID REFERENCES queen.streams(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255) NOT NULL,
    stream_key TEXT NOT NULL,
    last_acked_window_end TIMESTAMPTZ,
    total_windows_consumed BIGINT DEFAULT 0,
    last_consumed_at TIMESTAMPTZ,
    PRIMARY KEY (stream_id, consumer_group, stream_key)
);

-- Active leases
CREATE TABLE queen.stream_leases (
    id UUID PRIMARY KEY,
    stream_id UUID REFERENCES queen.streams(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255) NOT NULL,
    stream_key TEXT NOT NULL,
    window_start TIMESTAMPTZ NOT NULL,
    window_end TIMESTAMPTZ NOT NULL,
    lease_id UUID NOT NULL UNIQUE,
    lease_expires_at TIMESTAMPTZ NOT NULL,
    UNIQUE(stream_id, consumer_group, stream_key, window_start, window_end)
);
```

---

## Summary

The streaming feature is now fully integrated into the Queen web UI! Users can:

✅ View streaming metrics on the dashboard  
✅ Browse and search all streams  
✅ Create new streams with full configuration  
✅ Delete streams with confirmation  
✅ Monitor active windows and consumer groups  
✅ Use responsive, mobile-friendly interface  
✅ Enjoy dark mode support  

The implementation follows Queen's design patterns and integrates seamlessly with the existing queues, consumer groups, and analytics features.

