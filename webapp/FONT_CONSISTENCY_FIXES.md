# Font Consistency Fixes

## Overview

Removed monospace (`font-mono`) font from transaction IDs, partition IDs, and trace names to use the standard sans-serif font consistently across the application.

## Issue

The application was inconsistently using monospace font for certain text elements, making them stand out awkwardly from the rest of the interface. While monospace fonts are useful for code/data, they're not appropriate for all IDs and names in a modern UI.

## Changes Made

### 1. Messages Page (`webapp/src/views/Messages.vue`)

**Transaction IDs and Partition IDs in table:**

```diff
- <div class="font-mono text-[10px] select-all break-all leading-tight">
+ <div class="text-xs select-all break-all">
    {{ message.transactionId }}
  </div>
```

**Changes:**
- ✅ Removed `font-mono` from transaction ID display
- ✅ Removed `font-mono` from partition ID display  
- ✅ Removed `font-mono` from partition name (mobile view)
- ✅ Updated text size from `text-[10px]` to `text-xs` for consistency
- ✅ Removed unnecessary `leading-tight`

**Affected elements:**
- Transaction ID column
- Partition ID column (desktop)
- Partition display (mobile)

### 2. Message Search Filter (`webapp/src/components/messages/MessageFilters.vue`)

**Search input:**

```diff
- class="input font-mono text-xs"
+ class="input"
```

**Changes:**
- ✅ Removed `font-mono` from transaction ID search input
- ✅ Removed `text-xs` override (now uses standard input sizing)

### 3. Traces Page (`webapp/src/views/Traces.vue`)

**Trace table rows:**

```diff
- <span class="text-xs font-mono text-gray-600 dark:text-gray-400">
+ <span class="text-xs text-gray-600 dark:text-gray-400">
    {{ trace.transaction_id }}
  </span>
```

**Changes:**
- ✅ Removed `font-mono` from transaction ID column
- ✅ Removed `font-mono` from partition name column
- ✅ Removed `font-mono` from trace name display (info box)
- ✅ Removed `font-mono` from trace name display (no results message)
- ✅ Removed `font-mono` from trace names list

**Affected elements:**
- Transaction ID column
- Partition name column
- Trace name in success message
- Trace name in "no results" message
- Trace names in available traces list

## Typography Now Used

All text elements now use the standard application font:

```css
/* Standard Font Stack (from body) */
font-family: -apple-system, BlinkMacSystemFont, "Inter", "Segoe UI", Roboto, 
             "Helvetica Neue", Arial, sans-serif;
```

### Text Sizes (Standardized):

| Element | Size | Usage |
|---------|------|-------|
| Transaction IDs | `text-xs` (0.75rem / 12px) | Consistent with other small data |
| Partition Names | `text-xs` (0.75rem / 12px) | Consistent with metadata |
| Search Input | Standard `.input` | Matches other inputs |
| Trace Names | `text-sm` (0.875rem / 14px) | Readable primary text |

## Before vs After

### Messages Table - Before:
```
Queue Name          Partition      Transaction ID
─────────────────────────────────────────────────
my-queue           part-01        abc-123-def (monospace ❌)
```

### Messages Table - After:
```
Queue Name          Partition      Transaction ID
─────────────────────────────────────────────────
my-queue           part-01        abc-123-def (sans-serif ✅)
```

### Traces Table - Before:
```
Event    Queue       Partition     Transaction ID
─────────────────────────────────────────────────
PUSH     orders      order-01      tx-001 (monospace ❌)
```

### Traces Table - After:
```
Event    Queue       Partition     Transaction ID
─────────────────────────────────────────────────
PUSH     orders      order-01      tx-001 (sans-serif ✅)
```

## Design Rationale

### Why Remove Monospace?

1. **Visual Consistency** - The entire UI uses sans-serif fonts for better readability
2. **Modern Design** - Monospace fonts make the UI look more technical/developer-focused
3. **Better Readability** - Sans-serif fonts are more readable at small sizes
4. **Professional Appearance** - Consistent typography looks more polished
5. **User-Friendly** - Less "code-like", more approachable for all users

### When Monospace IS Appropriate

Monospace fonts are still used in appropriate contexts:
- ✅ JSON payload displays (code blocks)
- ✅ Technical configuration (when explicitly showing code/config)
- ✅ Console-like outputs

### When Monospace Is NOT Appropriate

- ❌ IDs and identifiers in tables
- ❌ User-facing text inputs
- ❌ Names and labels
- ❌ General UI text

## Typography Best Practices

For consistency across the application:

```jsx
// ✅ GOOD - Standard text for IDs and names
<span class="text-xs text-gray-600 dark:text-gray-400">
  {{ transactionId }}
</span>

// ❌ BAD - Monospace for UI elements
<span class="text-xs font-mono text-gray-600 dark:text-gray-400">
  {{ transactionId }}
</span>

// ✅ GOOD - Monospace for code/JSON
<pre class="font-mono">
  {{ jsonData }}
</pre>
```

## Files Modified

1. `webapp/src/views/Messages.vue`
2. `webapp/src/components/messages/MessageFilters.vue`
3. `webapp/src/views/Traces.vue`

## Testing

Verified across:
- ✅ Messages page - All transaction/partition IDs display correctly
- ✅ Messages search - Input uses standard font
- ✅ Traces page - All IDs and names use standard font
- ✅ Desktop view - Proper typography
- ✅ Mobile view - Readable text at smaller sizes
- ✅ Dark mode - Good contrast maintained
- ✅ Copy/paste - Still works with `select-all` class

## Benefits

✅ **Visual Harmony** - Consistent typography throughout the app  
✅ **Better Readability** - Sans-serif is more readable at UI scales  
✅ **Professional Look** - Modern, polished appearance  
✅ **User-Friendly** - Less technical, more accessible  
✅ **Maintainable** - Simpler, fewer font variations to manage  

---

**Result:** Clean, consistent typography that enhances the professional appearance of the application! 🎨

