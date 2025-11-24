# Custom UI Components

This directory contains reusable, custom-designed UI components for the Queen MQ webapp.

## Components

### CustomSelect

A feature-rich dropdown select component with search, keyboard navigation, and modern styling.

**Features:**
- ✨ Smooth animations and transitions
- 🔍 Built-in search/filter functionality
- ⌨️ Full keyboard navigation (Arrow keys, Enter, Escape)
- 🎨 Dark mode support
- ❌ Optional clear button
- 🎯 Focused state management
- 📱 Responsive design

**Usage:**

```vue
<template>
  <CustomSelect
    v-model="selectedValue"
    :options="options"
    placeholder="Select an option"
    :searchable="true"
    :clearable="true"
  />
</template>

<script setup>
import { ref } from 'vue';
import CustomSelect from '@/components/common/CustomSelect.vue';

const selectedValue = ref('');

// Simple array of strings
const simpleOptions = ['Option 1', 'Option 2', 'Option 3'];

// Or array of objects with value and label
const options = [
  { value: '', label: 'All Options' },
  { value: '1', label: 'Option 1' },
  { value: '2', label: 'Option 2' },
  { value: '3', label: 'Option 3' }
];
</script>
```

**Props:**

| Prop | Type | Default | Description |
|------|------|---------|-------------|
| `modelValue` | String/Number/Object | - | The selected value (v-model) |
| `options` | Array | `[]` | Array of options (strings or objects) |
| `placeholder` | String | `'Select...'` | Placeholder text |
| `searchable` | Boolean | `true` | Enable search functionality |
| `clearable` | Boolean | `true` | Show clear button when value selected |
| `disabled` | Boolean | `false` | Disable the select |
| `maxHeight` | String | `'320px'` | Max height of dropdown |
| `valueKey` | String | `'value'` | Key for option value (when using objects) |
| `labelKey` | String | `'label'` | Key for option label (when using objects) |

**Events:**
- `@update:modelValue` - Emitted when value changes
- `@change` - Emitted when value changes (same as update:modelValue)

---

### DateTimePicker

A modern date and time picker with calendar view, quick presets, and intuitive UX.

**Features:**
- 📅 Interactive calendar grid
- ⚡ Quick preset buttons (Now, 1 hour ago, etc.)
- 🕐 Separate hour/minute inputs
- 🎨 Dark mode support
- 📱 Responsive design
- ✨ Smooth animations
- ❌ Optional clear button

**Usage:**

```vue
<template>
  <DateTimePicker
    v-model="selectedDateTime"
    placeholder="Select date & time"
    :show-presets="true"
    :clearable="true"
  />
</template>

<script setup>
import { ref } from 'vue';
import DateTimePicker from '@/components/common/DateTimePicker.vue';

const selectedDateTime = ref('');
</script>
```

**Props:**

| Prop | Type | Default | Description |
|------|------|---------|-------------|
| `modelValue` | String | - | ISO date string (v-model) |
| `placeholder` | String | `'Select date & time'` | Placeholder text |
| `clearable` | Boolean | `true` | Show clear button when value selected |
| `disabled` | Boolean | `false` | Disable the picker |
| `showPresets` | Boolean | `true` | Show quick preset buttons |
| `format` | String | `'MMM DD, YYYY HH:mm'` | Display format (for future use) |

**Events:**
- `@update:modelValue` - Emitted when value changes (ISO string)
- `@change` - Emitted when value changes (same as update:modelValue)

**Presets:**
- Now
- 1 hour ago
- 6 hours ago
- 24 hours ago
- Yesterday (start of day)
- 7 days ago

---

## Design System

Both components follow the Queen MQ design system:

- **Primary Color**: Emerald Green (#059669, #FF8A3D for dark mode)
- **Border Radius**: 0.5rem (8px) for inputs, 0.75rem (12px) for dropdowns
- **Transitions**: Smooth cubic-bezier(0.4, 0, 0.2, 1) animations
- **Shadows**: Subtle elevation with dark mode support
- **Typography**: System UI fonts with proper sizing
- **Dark Mode**: Full support with adjusted colors and contrast

## Browser Support

- Chrome/Edge (latest)
- Firefox (latest)
- Safari (latest)
- Mobile browsers (iOS Safari, Chrome Android)

## Accessibility

- Keyboard navigation support
- Focus management
- Click-outside detection
- Proper ARIA attributes (to be added in future updates)

