<template>
  <div class="datetime-picker" ref="pickerRef">
    <div 
      class="picker-trigger"
      :class="{ 'picker-trigger-open': isOpen, 'picker-trigger-disabled': disabled }"
      @click="togglePicker"
    >
      <svg class="calendar-icon" fill="none" stroke="currentColor" viewBox="0 0 24 24">
        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 7V3m8 4V3m-9 8h10M5 21h14a2 2 0 002-2V7a2 2 0 00-2-2H5a2 2 0 00-2 2v12a2 2 0 002 2z" />
      </svg>
      <span v-if="displayValue" class="picker-value">{{ displayValue }}</span>
      <span v-else class="picker-placeholder">{{ placeholder }}</span>
      <button
        v-if="modelValue && clearable"
        @click.stop="clearValue"
        class="clear-btn"
        type="button"
      >
        <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
        </svg>
      </button>
    </div>

    <transition name="picker-dropdown">
      <div v-if="isOpen" class="picker-dropdown">
        <!-- Quick Presets -->
        <div v-if="showPresets" class="presets-section">
          <div class="presets-title">Quick Select</div>
          <div class="presets-grid">
            <button
              v-for="preset in presets"
              :key="preset.label"
              @click="selectPreset(preset)"
              class="preset-btn"
            >
              {{ preset.label }}
            </button>
          </div>
        </div>

        <!-- Calendar & Time Section -->
        <div class="picker-main">
          <!-- Calendar Header -->
          <div class="calendar-header">
            <button @click="previousMonth" class="nav-btn">
              <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 19l-7-7 7-7" />
              </svg>
            </button>
            <div class="current-month">
              {{ currentMonthYear }}
            </div>
            <button @click="nextMonth" class="nav-btn">
              <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 5l7 7-7 7" />
              </svg>
            </button>
          </div>

          <!-- Calendar Grid -->
          <div class="calendar-grid">
            <div v-for="day in ['Su', 'Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa']" :key="day" class="day-label">
              {{ day }}
            </div>
            <button
              v-for="day in calendarDays"
              :key="day.date"
              @click="selectDate(day)"
              class="day-cell"
              :class="{
                'day-other-month': day.isOtherMonth,
                'day-today': day.isToday,
                'day-selected': day.isSelected,
                'day-disabled': day.isDisabled
              }"
              :disabled="day.isDisabled"
            >
              {{ day.day }}
            </button>
          </div>

          <!-- Time Selection -->
          <div class="time-section">
            <div class="time-label">Time</div>
            <div class="time-inputs">
              <div class="time-input-group">
                <input
                  v-model.number="selectedHour"
                  type="number"
                  min="0"
                  max="23"
                  class="time-input"
                  @change="updateDateTime"
                  placeholder="HH"
                />
                <span class="time-label-small">Hours</span>
              </div>
              <span class="time-separator">:</span>
              <div class="time-input-group">
                <input
                  v-model.number="selectedMinute"
                  type="number"
                  min="0"
                  max="59"
                  class="time-input"
                  @change="updateDateTime"
                  placeholder="MM"
                />
                <span class="time-label-small">Minutes</span>
              </div>
            </div>
          </div>
        </div>

        <!-- Action Buttons -->
        <div class="picker-actions">
          <button @click="cancel" class="action-btn btn-cancel">Cancel</button>
          <button @click="apply" class="action-btn btn-apply">Apply</button>
        </div>
      </div>
    </transition>
  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted } from 'vue';

const props = defineProps({
  modelValue: String,
  placeholder: {
    type: String,
    default: 'Select date & time'
  },
  clearable: {
    type: Boolean,
    default: true
  },
  disabled: {
    type: Boolean,
    default: false
  },
  showPresets: {
    type: Boolean,
    default: true
  },
  format: {
    type: String,
    default: 'MMM DD, YYYY HH:mm'
  }
});

const emit = defineEmits(['update:modelValue', 'change']);

const pickerRef = ref(null);
const isOpen = ref(false);
const currentDate = ref(new Date());
const selectedDate = ref(null);
const selectedHour = ref(0);
const selectedMinute = ref(0);

const presets = [
  { label: 'Now', value: () => new Date() },
  { label: '1 hour ago', value: () => new Date(Date.now() - 60 * 60 * 1000) },
  { label: '6 hours ago', value: () => new Date(Date.now() - 6 * 60 * 60 * 1000) },
  { label: '24 hours ago', value: () => new Date(Date.now() - 24 * 60 * 60 * 1000) },
  { label: 'Yesterday', value: () => {
    const date = new Date();
    date.setDate(date.getDate() - 1);
    date.setHours(0, 0, 0, 0);
    return date;
  }},
  { label: '7 days ago', value: () => new Date(Date.now() - 7 * 24 * 60 * 60 * 1000) }
];

const displayValue = computed(() => {
  if (!props.modelValue) return '';
  const date = new Date(props.modelValue);
  return formatDate(date);
});

const currentMonthYear = computed(() => {
  const date = currentDate.value;
  return date.toLocaleDateString('en-US', { month: 'long', year: 'numeric' });
});

const calendarDays = computed(() => {
  const year = currentDate.value.getFullYear();
  const month = currentDate.value.getMonth();
  
  const firstDay = new Date(year, month, 1);
  const lastDay = new Date(year, month + 1, 0);
  const prevLastDay = new Date(year, month, 0);
  
  const firstDayOfWeek = firstDay.getDay();
  const lastDate = lastDay.getDate();
  const prevLastDate = prevLastDay.getDate();
  
  const days = [];
  
  // Previous month days
  for (let i = firstDayOfWeek - 1; i >= 0; i--) {
    const day = prevLastDate - i;
    const date = new Date(year, month - 1, day);
    days.push({
      day,
      date: date.toISOString(),
      isOtherMonth: true,
      isToday: false,
      isSelected: false,
      isDisabled: false
    });
  }
  
  // Current month days
  for (let day = 1; day <= lastDate; day++) {
    const date = new Date(year, month, day);
    const today = new Date();
    const isToday = date.toDateString() === today.toDateString();
    const isSelected = selectedDate.value && 
      date.toDateString() === new Date(selectedDate.value).toDateString();
    
    days.push({
      day,
      date: date.toISOString(),
      isOtherMonth: false,
      isToday,
      isSelected,
      isDisabled: false
    });
  }
  
  // Next month days
  const remainingDays = 42 - days.length; // 6 rows * 7 days
  for (let day = 1; day <= remainingDays; day++) {
    const date = new Date(year, month + 1, day);
    days.push({
      day,
      date: date.toISOString(),
      isOtherMonth: true,
      isToday: false,
      isSelected: false,
      isDisabled: false
    });
  }
  
  return days;
});

function formatDate(date) {
  const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
  const month = months[date.getMonth()];
  const day = String(date.getDate()).padStart(2, '0');
  const year = date.getFullYear();
  const hours = String(date.getHours()).padStart(2, '0');
  const minutes = String(date.getMinutes()).padStart(2, '0');
  
  return `${month} ${day}, ${year} ${hours}:${minutes}`;
}

function togglePicker() {
  if (props.disabled) return;
  isOpen.value = !isOpen.value;
  
  if (isOpen.value) {
    initializePicker();
  }
}

function initializePicker() {
  if (props.modelValue) {
    const date = new Date(props.modelValue);
    selectedDate.value = date.toISOString();
    currentDate.value = new Date(date);
    selectedHour.value = date.getHours();
    selectedMinute.value = date.getMinutes();
  } else {
    const now = new Date();
    currentDate.value = now;
    selectedHour.value = now.getHours();
    selectedMinute.value = now.getMinutes();
  }
}

function previousMonth() {
  const date = new Date(currentDate.value);
  date.setMonth(date.getMonth() - 1);
  currentDate.value = date;
}

function nextMonth() {
  const date = new Date(currentDate.value);
  date.setMonth(date.getMonth() + 1);
  currentDate.value = date;
}

function selectDate(day) {
  if (day.isDisabled) return;
  selectedDate.value = day.date;
}

function selectPreset(preset) {
  const date = preset.value();
  selectedDate.value = date.toISOString();
  selectedHour.value = date.getHours();
  selectedMinute.value = date.getMinutes();
  currentDate.value = new Date(date);
  apply();
}

function updateDateTime() {
  // Validate hours and minutes
  if (selectedHour.value < 0) selectedHour.value = 0;
  if (selectedHour.value > 23) selectedHour.value = 23;
  if (selectedMinute.value < 0) selectedMinute.value = 0;
  if (selectedMinute.value > 59) selectedMinute.value = 59;
}

function apply() {
  if (!selectedDate.value) {
    selectedDate.value = new Date().toISOString();
  }
  
  const date = new Date(selectedDate.value);
  date.setHours(selectedHour.value);
  date.setMinutes(selectedMinute.value);
  date.setSeconds(0);
  date.setMilliseconds(0);
  
  const isoString = date.toISOString();
  emit('update:modelValue', isoString);
  emit('change', isoString);
  isOpen.value = false;
}

function cancel() {
  isOpen.value = false;
}

function clearValue() {
  emit('update:modelValue', '');
  emit('change', '');
}

function handleClickOutside(event) {
  if (pickerRef.value && !pickerRef.value.contains(event.target)) {
    isOpen.value = false;
  }
}

onMounted(() => {
  document.addEventListener('click', handleClickOutside);
});

onUnmounted(() => {
  document.removeEventListener('click', handleClickOutside);
});
</script>

<style scoped>
.datetime-picker {
  position: relative;
  width: 100%;
}

.picker-trigger {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.5rem 0.75rem;
  background: white;
  border: 1px solid rgba(0, 0, 0, 0.1);
  border-radius: 0.5rem;
  cursor: pointer;
  transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
  line-height: 1.5;
}

.dark .picker-trigger {
  background: rgba(31, 41, 55, 0.5);
  border-color: rgba(255, 255, 255, 0.1);
}

.picker-trigger:hover {
  border-color: rgba(5, 150, 105, 0.3);
  box-shadow: 0 0 0 3px rgba(5, 150, 105, 0.05);
}

.dark .picker-trigger:hover {
  border-color: rgba(5, 150, 105, 0.4);
}

.picker-trigger-open {
  border-color: #059669;
  box-shadow: 0 0 0 3px rgba(5, 150, 105, 0.1);
}

.dark .picker-trigger-open {
  border-color: #34d399;
  box-shadow: 0 0 0 3px rgba(5, 150, 105, 0.15);
}

.picker-trigger-disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.calendar-icon {
  width: 1rem;
  height: 1rem;
  color: #6b7280;
  flex-shrink: 0;
}

.dark .calendar-icon {
  color: #9ca3af;
}

.picker-value {
  flex: 1;
  font-size: 0.875rem;
  color: #111827;
  font-family: ui-monospace, monospace;
  line-height: 1.5;
}

.dark .picker-value {
  color: #f9fafb;
}

.picker-placeholder {
  flex: 1;
  font-size: 0.875rem;
  color: #9ca3af;
  line-height: 1.5;
}

.dark .picker-placeholder {
  color: #6b7280;
}

.clear-btn {
  padding: 0.25rem;
  color: #6b7280;
  border-radius: 0.25rem;
  transition: all 0.15s;
  display: flex;
  align-items: center;
}

.clear-btn:hover {
  color: #ef4444;
  background: rgba(239, 68, 68, 0.1);
}

.picker-dropdown {
  position: absolute;
  top: calc(100% + 0.5rem);
  left: 0;
  background: white;
  border: 1px solid rgba(0, 0, 0, 0.1);
  border-radius: 0.75rem;
  box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.1), 0 8px 10px -6px rgba(0, 0, 0, 0.08);
  z-index: 50;
  overflow: hidden;
  min-width: 320px;
}

.dark .picker-dropdown {
  background: #1f2937;
  border-color: rgba(255, 255, 255, 0.1);
  box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.5), 0 8px 10px -6px rgba(0, 0, 0, 0.4);
}

.presets-section {
  padding: 1rem;
  border-bottom: 1px solid rgba(0, 0, 0, 0.06);
}

.dark .presets-section {
  border-color: rgba(255, 255, 255, 0.06);
}

.presets-title {
  font-size: 0.75rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  color: #6b7280;
  margin-bottom: 0.5rem;
}

.dark .presets-title {
  color: #9ca3af;
}

.presets-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 0.5rem;
}

.preset-btn {
  padding: 0.5rem 0.75rem;
  background: rgba(5, 150, 105, 0.05);
  border: 1px solid rgba(5, 150, 105, 0.15);
  border-radius: 0.375rem;
  font-size: 0.8125rem;
  color: #059669;
  font-weight: 500;
  transition: all 0.15s;
  cursor: pointer;
}

.dark .preset-btn {
  background: rgba(5, 150, 105, 0.1);
  border-color: rgba(5, 150, 105, 0.2);
  color: #34d399;
}

.preset-btn:hover {
  background: rgba(5, 150, 105, 0.12);
  border-color: rgba(5, 150, 105, 0.3);
  transform: translateY(-1px);
}

.picker-main {
  padding: 1rem;
}

.calendar-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 1rem;
}

.nav-btn {
  padding: 0.375rem;
  color: #6b7280;
  border-radius: 0.375rem;
  transition: all 0.15s;
  display: flex;
  align-items: center;
}

.dark .nav-btn {
  color: #9ca3af;
}

.nav-btn:hover {
  background: rgba(5, 150, 105, 0.1);
  color: #059669;
}

.dark .nav-btn:hover {
  color: #34d399;
}

.current-month {
  font-size: 0.9375rem;
  font-weight: 600;
  color: #111827;
}

.dark .current-month {
  color: #f9fafb;
}

.calendar-grid {
  display: grid;
  grid-template-columns: repeat(7, 1fr);
  gap: 0.25rem;
}

.day-label {
  padding: 0.5rem;
  font-size: 0.75rem;
  font-weight: 600;
  color: #6b7280;
  text-align: center;
}

.dark .day-label {
  color: #9ca3af;
}

.day-cell {
  aspect-ratio: 1;
  padding: 0.5rem;
  font-size: 0.875rem;
  color: #374151;
  border-radius: 0.375rem;
  transition: all 0.15s;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
}

.dark .day-cell {
  color: #d1d5db;
}

.day-cell:hover {
  background: rgba(5, 150, 105, 0.1);
  color: #059669;
}

.dark .day-cell:hover {
  color: #34d399;
}

.day-other-month {
  color: #d1d5db;
}

.dark .day-other-month {
  color: #6b7280;
}

.day-today {
  font-weight: 600;
  color: #059669;
  background: rgba(5, 150, 105, 0.08);
}

.dark .day-today {
  color: #34d399;
}

.day-selected {
  background: #059669;
  color: white;
  font-weight: 600;
}

.dark .day-selected {
  background: #34d399;
  color: #111827;
}

.day-disabled {
  opacity: 0.3;
  cursor: not-allowed;
}

.time-section {
  margin-top: 1rem;
  padding-top: 1rem;
  border-top: 1px solid rgba(0, 0, 0, 0.06);
}

.dark .time-section {
  border-color: rgba(255, 255, 255, 0.06);
}

.time-label {
  font-size: 0.75rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  color: #6b7280;
  margin-bottom: 0.5rem;
}

.dark .time-label {
  color: #9ca3af;
}

.time-inputs {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.time-input-group {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}

.time-input {
  width: 100%;
  padding: 0.5rem 0.75rem;
  background: rgba(0, 0, 0, 0.02);
  border: 1px solid rgba(0, 0, 0, 0.1);
  border-radius: 0.375rem;
  font-size: 0.875rem;
  font-family: ui-monospace, monospace;
  text-align: center;
  color: #111827;
  transition: all 0.15s;
}

.dark .time-input {
  background: rgba(255, 255, 255, 0.05);
  border-color: rgba(255, 255, 255, 0.1);
  color: #f9fafb;
}

.time-input:focus {
  outline: none;
  border-color: #059669;
  background: white;
}

.dark .time-input:focus {
  background: rgba(255, 255, 255, 0.08);
}

.time-label-small {
  font-size: 0.6875rem;
  color: #9ca3af;
  text-align: center;
}

.time-separator {
  font-size: 1.25rem;
  font-weight: 600;
  color: #6b7280;
  margin-top: -1rem;
}

.dark .time-separator {
  color: #9ca3af;
}

.picker-actions {
  display: flex;
  gap: 0.5rem;
  padding: 1rem;
  border-top: 1px solid rgba(0, 0, 0, 0.06);
}

.dark .picker-actions {
  border-color: rgba(255, 255, 255, 0.06);
}

.action-btn {
  flex: 1;
  padding: 0.625rem 1rem;
  border-radius: 0.5rem;
  font-size: 0.875rem;
  font-weight: 500;
  transition: all 0.15s;
  cursor: pointer;
}

.btn-cancel {
  background: transparent;
  border: 1px solid rgba(0, 0, 0, 0.1);
  color: #6b7280;
}

.dark .btn-cancel {
  border-color: rgba(255, 255, 255, 0.1);
  color: #9ca3af;
}

.btn-cancel:hover {
  background: rgba(0, 0, 0, 0.03);
}

.dark .btn-cancel:hover {
  background: rgba(255, 255, 255, 0.05);
}

.btn-apply {
  background: #059669;
  border: 1px solid #059669;
  color: white;
}

.dark .btn-apply {
  background: #34d399;
  border-color: #34d399;
  color: #111827;
}

.btn-apply:hover {
  background: #047857;
  border-color: #047857;
}

.dark .btn-apply:hover {
  background: #10b981;
  border-color: #10b981;
}

/* Picker dropdown animation */
.picker-dropdown-enter-active,
.picker-dropdown-leave-active {
  transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
}

.picker-dropdown-enter-from {
  opacity: 0;
  transform: translateY(-0.5rem) scale(0.95);
}

.picker-dropdown-enter-to {
  opacity: 1;
  transform: translateY(0) scale(1);
}

.picker-dropdown-leave-from {
  opacity: 1;
  transform: translateY(0) scale(1);
}

.picker-dropdown-leave-to {
  opacity: 0;
  transform: translateY(-0.5rem) scale(0.95);
}
</style>

