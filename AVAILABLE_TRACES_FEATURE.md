# Available Trace Names List Feature

## Overview
Enhanced the Traces page to display a **paginated list of all available trace names** in the system, making trace discovery easy and intuitive.

## Implementation Summary

### ✅ Backend (C++)

#### 1. **Queue Manager Method**
**Location:** `server/src/managers/queue_manager.cpp` (lines 2948-2999)

```cpp
nlohmann::json QueueManager::get_available_trace_names(int limit, int offset)
```

**SQL Query:**
```sql
SELECT 
    trace_name,
    COUNT(DISTINCT trace_id) as trace_count,
    COUNT(DISTINCT mt.transaction_id) as message_count,
    MAX(mt.created_at) as last_seen
FROM queen.message_trace_names mtn
JOIN queen.message_traces mt ON mtn.trace_id = mt.id
GROUP BY trace_name
ORDER BY last_seen DESC
LIMIT $1 OFFSET $2
```

**Returns:**
- `trace_name` - The trace name
- `trace_count` - Number of trace events with this name
- `message_count` - Number of unique messages with this trace name
- `last_seen` - Timestamp of most recent trace

#### 2. **API Endpoint**
**Location:** `server/src/acceptor_server.cpp` (lines 1823-1835)

```
GET /api/v1/traces/names?limit=20&offset=0
```

**Response:**
```json
{
  "trace_names": [
    {
      "trace_name": "tenant-acme",
      "trace_count": 15,
      "message_count": 5,
      "last_seen": "2025-10-28T14:30:00Z"
    },
    {
      "trace_name": "order-flow-123",
      "trace_count": 8,
      "message_count": 3,
      "last_seen": "2025-10-28T14:25:00Z"
    }
  ],
  "count": 2,
  "total": 42,
  "limit": 20,
  "offset": 0
}
```

### ✅ Frontend (Vue.js)

#### 1. **API Client**
**Location:** `webapp/src/api/messages.js` (line 29)

```javascript
getAvailableTraceNames: (params) => apiClient.get('/api/v1/traces/names', { params })
```

#### 2. **Traces View Enhancement**
**Location:** `webapp/src/views/Traces.vue`

**New Features:**
- **List Display**: Shows all available trace names when no search is active
- **Rich Metadata**: Each trace name shows:
  - Trace name (monospace font)
  - Number of trace events
  - Number of unique messages
  - Last seen timestamp
- **Clickable Items**: Click any trace name to search for it
- **Pagination**: Navigate through large lists (20 items per page)
- **Loading State**: Spinner while loading
- **Empty State**: Helpful message when no traces exist yet
- **Error Handling**: Clear error messages if loading fails

## User Interface

### When No Search is Active:

```
┌─────────────────────────────────────────────┐
│  Traces                                     │
│  [Search box]                               │
├─────────────────────────────────────────────┤
│  Available Trace Names                      │
├─────────────────────────────────────────────┤
│  tenant-acme                          [→]   │
│  15 traces • 5 messages • Last: 2:30 PM     │
├─────────────────────────────────────────────┤
│  order-flow-123                       [→]   │
│  8 traces • 3 messages • Last: 2:25 PM      │
├─────────────────────────────────────────────┤
│  user-workflow-456                    [→]   │
│  12 traces • 4 messages • Last: 2:20 PM     │
├─────────────────────────────────────────────┤
│  [Previous] [Next]                          │
└─────────────────────────────────────────────┘
```

### After Clicking a Trace Name:

Automatically searches for that trace name and displays the timeline of all traces.

## User Flow

```
1. User opens /traces
   ↓
2. Page loads list of available trace names
   (sorted by most recent first)
   ↓
3. User sees each trace name with:
   - Count of trace events
   - Count of messages
   - Last activity time
   ↓
4. User clicks on a trace name
   ↓
5. Search automatically executes
   ↓
6. Timeline view shows all traces for that name
```

## Key Features

### ✅ **Discovery**
- Browse all available trace names without knowing them beforehand
- See what traces exist in your system
- Discover patterns and naming conventions

### ✅ **Metadata**
- **Trace Count**: How many trace events have this name
- **Message Count**: How many unique messages involved
- **Last Seen**: When this trace was last recorded

### ✅ **Sorted by Recency**
- Most recently used traces appear first
- Helps identify active workflows
- Easy to find current activity

### ✅ **Pagination**
- Handle hundreds of trace names
- 20 items per page (configurable)
- Fast navigation with Previous/Next buttons

### ✅ **One-Click Search**
- Click any trace name to search
- No typing required
- Instant results

### ✅ **Empty States**
- Clear message when no traces exist yet
- Helpful guidance for new users
- Error messages when loading fails

## API Examples

### Get First Page of Trace Names:
```bash
curl "http://localhost:6632/api/v1/traces/names?limit=20&offset=0"
```

### Get Second Page:
```bash
curl "http://localhost:6632/api/v1/traces/names?limit=20&offset=20"
```

### Response Example:
```json
{
  "trace_names": [
    {
      "trace_name": "tenant-acme",
      "trace_count": 15,
      "message_count": 5,
      "last_seen": "2025-10-28T14:30:00.000Z"
    },
    {
      "trace_name": "order-flow-123",
      "trace_count": 8,
      "message_count": 3,
      "last_seen": "2025-10-28T14:25:00.000Z"
    }
  ],
  "count": 2,
  "total": 42,
  "limit": 20,
  "offset": 0
}
```

## Benefits

### 🔍 **Trace Discovery**
Before: Users needed to know trace names to search
After: Browse and discover all available traces

### 📊 **Usage Insights**
- See which traces are most active
- Identify frequently used workflows
- Understand system usage patterns

### ⚡ **Faster Navigation**
- Click to search (no typing)
- Recent traces appear first
- Pagination for large lists

### 🎯 **Better UX**
- No empty search box staring at you
- Actionable list instead of blank page
- Clear indication of what's available

## Technical Details

### Performance
- **Efficient Query**: Uses `GROUP BY` with aggregates
- **Indexed**: `trace_name` is indexed for fast lookups
- **Pagination**: Limits result set for fast response

### Sorting
- **By Last Seen**: Most recent traces first
- Helps identify active workflows
- More useful than alphabetical order

### Database Impact
- Read-only query
- No writes or modifications
- Leverages existing indexes

## Future Enhancements

Potential improvements:
- **Search/Filter**: Filter trace names by text
- **Sort Options**: Sort by name, count, or date
- **Usage Stats**: Show trace trends over time
- **Favorites**: Pin frequently used trace names
- **Grouping**: Group related traces together
- **Export**: Download trace names list

## Testing

To test the feature:

1. **Create some traces** in your consumers:
```javascript
await msg.trace({
  traceName: ['tenant-alpha', 'workflow-A'],
  data: { text: 'Step 1' }
});

await msg.trace({
  traceName: ['tenant-beta', 'workflow-B'],
  data: { text: 'Step 2' }
});
```

2. **Open Traces page** in the UI

3. **Verify the list** shows both trace names

4. **Click a trace name** and verify it searches

5. **Test pagination** if you have more than 20 trace names

## Summary

The Available Trace Names list provides:
- ✅ **Full list** of all trace names in the system
- ✅ **Rich metadata** (counts, timestamps)
- ✅ **Pagination** for large lists
- ✅ **One-click search** for any trace
- ✅ **Sorted by recency** (most recent first)
- ✅ **Clean UI** with loading and error states

This makes trace discovery **effortless** and helps users understand what workflows exist in their system! 🎉

