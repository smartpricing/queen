<template>
  <div class="custom-select" ref="selectRef">
    <div 
      class="select-trigger"
      :class="{ 'select-trigger-open': isOpen, 'select-trigger-disabled': disabled }"
      @click="toggleDropdown"
    >
      <div class="select-value">
        <span v-if="displayValue" class="selected-text">{{ displayValue }}</span>
        <span v-else class="placeholder-text">{{ placeholder }}</span>
      </div>
      <div class="select-icons">
        <button
          v-if="modelValue && clearable"
          @click.stop="clearSelection"
          class="clear-btn"
          type="button"
        >
          <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
          </svg>
        </button>
        <svg 
          class="dropdown-icon" 
          :class="{ 'rotate-180': isOpen }"
          fill="none" 
          stroke="currentColor" 
          viewBox="0 0 24 24"
        >
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
        </svg>
      </div>
    </div>

    <transition name="dropdown">
      <div v-if="isOpen" class="select-dropdown">
        <div v-if="searchable" class="search-container">
          <div class="search-wrapper">
            <svg class="search-icon" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" />
            </svg>
            <input
              ref="searchInput"
              v-model="searchQuery"
              type="text"
              placeholder="Search..."
              class="search-input"
              @click.stop
            />
          </div>
        </div>
        
        <div class="options-container" :style="{ maxHeight: maxHeight }">
          <div
            v-for="(option, index) in filteredOptions"
            :key="getOptionValue(option)"
            class="select-option"
            :class="{ 
              'select-option-selected': isSelected(option),
              'select-option-focused': index === focusedIndex
            }"
            @click="selectOption(option)"
            @mouseenter="focusedIndex = index"
          >
            <span class="option-text">{{ getOptionLabel(option) }}</span>
            <svg 
              v-if="isSelected(option)" 
              class="check-icon" 
              fill="none" 
              stroke="currentColor" 
              viewBox="0 0 24 24"
            >
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7" />
            </svg>
          </div>
          
          <div v-if="filteredOptions.length === 0" class="no-options">
            No options found
          </div>
        </div>
      </div>
    </transition>
  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted, nextTick } from 'vue';

const props = defineProps({
  modelValue: [String, Number, Object],
  options: {
    type: Array,
    default: () => []
  },
  placeholder: {
    type: String,
    default: 'Select...'
  },
  searchable: {
    type: Boolean,
    default: true
  },
  clearable: {
    type: Boolean,
    default: true
  },
  disabled: {
    type: Boolean,
    default: false
  },
  maxHeight: {
    type: String,
    default: '320px'
  },
  valueKey: {
    type: String,
    default: 'value'
  },
  labelKey: {
    type: String,
    default: 'label'
  }
});

const emit = defineEmits(['update:modelValue', 'change']);

const selectRef = ref(null);
const searchInput = ref(null);
const isOpen = ref(false);
const searchQuery = ref('');
const focusedIndex = ref(-1);

const displayValue = computed(() => {
  if (!props.modelValue) return '';
  
  const option = props.options.find(opt => getOptionValue(opt) === props.modelValue);
  return option ? getOptionLabel(option) : '';
});

const filteredOptions = computed(() => {
  if (!props.searchable || !searchQuery.value) {
    return props.options;
  }
  
  const query = searchQuery.value.toLowerCase();
  return props.options.filter(option => {
    const label = getOptionLabel(option).toLowerCase();
    return label.includes(query);
  });
});

function getOptionValue(option) {
  if (typeof option === 'object' && option !== null) {
    return option[props.valueKey];
  }
  return option;
}

function getOptionLabel(option) {
  if (typeof option === 'object' && option !== null) {
    return option[props.labelKey];
  }
  return String(option);
}

function isSelected(option) {
  return getOptionValue(option) === props.modelValue;
}

function toggleDropdown() {
  if (props.disabled) return;
  
  isOpen.value = !isOpen.value;
  
  if (isOpen.value) {
    searchQuery.value = '';
    focusedIndex.value = -1;
    
    if (props.searchable) {
      nextTick(() => {
        searchInput.value?.focus();
      });
    }
  }
}

function selectOption(option) {
  const value = getOptionValue(option);
  emit('update:modelValue', value);
  emit('change', value);
  isOpen.value = false;
  searchQuery.value = '';
}

function clearSelection() {
  emit('update:modelValue', '');
  emit('change', '');
}

function handleClickOutside(event) {
  if (selectRef.value && !selectRef.value.contains(event.target)) {
    isOpen.value = false;
  }
}

function handleKeydown(event) {
  if (!isOpen.value) return;
  
  switch (event.key) {
    case 'ArrowDown':
      event.preventDefault();
      focusedIndex.value = Math.min(focusedIndex.value + 1, filteredOptions.value.length - 1);
      break;
    case 'ArrowUp':
      event.preventDefault();
      focusedIndex.value = Math.max(focusedIndex.value - 1, 0);
      break;
    case 'Enter':
      event.preventDefault();
      if (focusedIndex.value >= 0 && focusedIndex.value < filteredOptions.value.length) {
        selectOption(filteredOptions.value[focusedIndex.value]);
      }
      break;
    case 'Escape':
      isOpen.value = false;
      break;
  }
}

onMounted(() => {
  document.addEventListener('click', handleClickOutside);
  document.addEventListener('keydown', handleKeydown);
});

onUnmounted(() => {
  document.removeEventListener('click', handleClickOutside);
  document.removeEventListener('keydown', handleKeydown);
});

watch(isOpen, (newValue) => {
  if (!newValue) {
    searchQuery.value = '';
    focusedIndex.value = -1;
  }
});
</script>

<style scoped>
.custom-select {
  position: relative;
  width: 100%;
}

.select-trigger {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0.5rem 0.75rem;
  background: white;
  border: 1px solid rgba(0, 0, 0, 0.1);
  border-radius: 0.5rem;
  cursor: pointer;
  transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
  line-height: 1.5;
}

.dark .select-trigger {
  background: rgba(31, 41, 55, 0.5);
  border-color: rgba(255, 255, 255, 0.1);
}

.select-trigger:hover {
  border-color: rgba(5, 150, 105, 0.3);
  box-shadow: 0 0 0 3px rgba(5, 150, 105, 0.05);
}

.dark .select-trigger:hover {
  border-color: rgba(5, 150, 105, 0.4);
}

.select-trigger-open {
  border-color: #059669;
  box-shadow: 0 0 0 3px rgba(5, 150, 105, 0.1);
}

.dark .select-trigger-open {
  border-color: #34d399;
  box-shadow: 0 0 0 3px rgba(5, 150, 105, 0.15);
}

.select-trigger-disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.select-value {
  flex: 1;
  min-width: 0;
}

.selected-text {
  font-size: 0.875rem;
  color: #111827;
  display: block;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  line-height: 1.5;
}

.dark .selected-text {
  color: #f9fafb;
}

.placeholder-text {
  font-size: 0.875rem;
  color: #9ca3af;
  line-height: 1.5;
}

.dark .placeholder-text {
  color: #6b7280;
}

.select-icons {
  display: flex;
  align-items: center;
  gap: 0.25rem;
  margin-left: 0.5rem;
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

.dropdown-icon {
  width: 1rem;
  height: 1rem;
  color: #6b7280;
  transition: transform 0.2s cubic-bezier(0.4, 0, 0.2, 1);
  flex-shrink: 0;
}

.dark .dropdown-icon {
  color: #9ca3af;
}

.select-dropdown {
  position: absolute;
  top: calc(100% + 0.5rem);
  left: 0;
  right: 0;
  background: white;
  border: 1px solid rgba(0, 0, 0, 0.1);
  border-radius: 0.5rem;
  box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.1), 0 8px 10px -6px rgba(0, 0, 0, 0.08);
  z-index: 50;
  overflow: hidden;
}

.dark .select-dropdown {
  background: #1f2937;
  border-color: rgba(255, 255, 255, 0.1);
  box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.5), 0 8px 10px -6px rgba(0, 0, 0, 0.4);
}

.search-container {
  padding: 0.75rem;
  border-bottom: 1px solid rgba(0, 0, 0, 0.06);
}

.dark .search-container {
  border-color: rgba(255, 255, 255, 0.06);
}

.search-wrapper {
  position: relative;
  display: flex;
  align-items: center;
}

.search-icon {
  position: absolute;
  left: 0.75rem;
  width: 1rem;
  height: 1rem;
  color: #9ca3af;
  pointer-events: none;
}

.search-input {
  width: 100%;
  padding: 0.5rem 0.75rem 0.5rem 2.5rem;
  background: rgba(0, 0, 0, 0.02);
  border: 1px solid rgba(0, 0, 0, 0.08);
  border-radius: 0.375rem;
  font-size: 0.875rem;
  color: #111827;
  transition: all 0.15s;
}

.dark .search-input {
  background: rgba(255, 255, 255, 0.05);
  border-color: rgba(255, 255, 255, 0.1);
  color: #f9fafb;
}

.search-input:focus {
  outline: none;
  border-color: #059669;
  background: white;
}

.dark .search-input:focus {
  background: rgba(255, 255, 255, 0.08);
}

.options-container {
  overflow-y: auto;
  overscroll-behavior: contain;
}

.select-option {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0.625rem 0.875rem;
  cursor: pointer;
  transition: all 0.15s cubic-bezier(0.4, 0, 0.2, 1);
  font-size: 0.875rem;
  color: #374151;
}

.dark .select-option {
  color: #d1d5db;
}

.select-option:hover,
.select-option-focused {
  background: rgba(5, 150, 105, 0.08);
  color: #059669;
}

.dark .select-option:hover,
.dark .select-option-focused {
  background: rgba(5, 150, 105, 0.15);
  color: #34d399;
}

.select-option-selected {
  background: rgba(5, 150, 105, 0.12);
  color: #059669;
  font-weight: 500;
}

.dark .select-option-selected {
  background: rgba(5, 150, 105, 0.2);
  color: #34d399;
}

.option-text {
  flex: 1;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.check-icon {
  width: 1rem;
  height: 1rem;
  margin-left: 0.5rem;
  flex-shrink: 0;
}

.no-options {
  padding: 1rem;
  text-align: center;
  font-size: 0.875rem;
  color: #9ca3af;
}

/* Dropdown animation */
.dropdown-enter-active,
.dropdown-leave-active {
  transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
}

.dropdown-enter-from {
  opacity: 0;
  transform: translateY(-0.5rem) scale(0.95);
}

.dropdown-enter-to {
  opacity: 1;
  transform: translateY(0) scale(1);
}

.dropdown-leave-from {
  opacity: 1;
  transform: translateY(0) scale(1);
}

.dropdown-leave-to {
  opacity: 0;
  transform: translateY(-0.5rem) scale(0.95);
}
</style>

