<template>
  <div ref="rootEl" class="relative inline-block w-full max-w-xs">
    <!-- Trigger button -->
    <button
      class="w-full flex items-center justify-between gap-2 px-3 py-1.5 text-sm rounded-lg border
             border-light-200 dark:border-dark-200 bg-white dark:bg-dark-400
             text-light-900 dark:text-white hover:border-queen-400 dark:hover:border-queen-500
             transition-colors cursor-pointer"
      @click="open = !open"
    >
      <span class="truncate text-left">
        <template v-if="modelValue.length === 0">{{ placeholder }}</template>
        <template v-else-if="modelValue.length === 1">{{ modelValue[0] }}</template>
        <template v-else>{{ modelValue.length }} selected</template>
      </span>
      <svg class="w-4 h-4 shrink-0 text-light-400 transition-transform" :class="{ 'rotate-180': open }" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
        <path stroke-linecap="round" stroke-linejoin="round" d="M19 9l-7 7-7-7" />
      </svg>
    </button>

    <!-- Dropdown -->
    <Transition
      enter-active-class="transition ease-out duration-100"
      enter-from-class="opacity-0 scale-95"
      enter-to-class="opacity-100 scale-100"
      leave-active-class="transition ease-in duration-75"
      leave-from-class="opacity-100 scale-100"
      leave-to-class="opacity-0 scale-95"
    >
      <div
        v-if="open"
        class="absolute z-50 mt-1 w-full rounded-lg border border-light-200 dark:border-dark-200
               bg-white dark:bg-dark-400 shadow-lg origin-top"
      >
        <!-- Search input -->
        <div class="p-2 border-b border-light-100 dark:border-dark-200">
          <input
            ref="searchEl"
            v-model="search"
            type="text"
            class="w-full px-2.5 py-1.5 text-xs rounded-md border border-light-200 dark:border-dark-300
                   bg-light-50 dark:bg-dark-500 text-light-900 dark:text-white
                   placeholder-light-400 dark:placeholder-dark-100
                   focus:outline-none focus:ring-1 focus:ring-queen-500 focus:border-queen-500"
            :placeholder="searchPlaceholder"
            @keydown.esc="open = false"
          />
        </div>

        <!-- Options list -->
        <div class="max-h-48 overflow-y-auto py-1">
          <div v-if="filteredOptions.length === 0" class="px-3 py-2 text-xs text-light-500">
            No matches
          </div>
          <label
            v-for="opt in filteredOptions"
            :key="opt"
            class="flex items-center gap-2 px-3 py-1.5 text-sm cursor-pointer
                   hover:bg-light-50 dark:hover:bg-dark-300 transition-colors"
          >
            <input
              type="checkbox"
              class="rounded border-light-300 dark:border-dark-200 text-queen-600
                     focus:ring-queen-500 cursor-pointer"
              :checked="modelValue.includes(opt)"
              @change="toggle(opt)"
            />
            <span class="truncate text-light-800 dark:text-light-200 text-xs">{{ opt }}</span>
          </label>
        </div>

        <!-- Footer actions -->
        <div v-if="options.length > 1" class="flex items-center justify-between px-3 py-1.5 border-t border-light-100 dark:border-dark-200">
          <button
            class="text-xs text-queen-600 dark:text-queen-400 hover:underline cursor-pointer"
            @click="selectAll"
          >Select all</button>
          <button
            class="text-xs text-light-500 hover:text-light-700 dark:hover:text-light-300 hover:underline cursor-pointer"
            @click="clearAll"
          >Clear</button>
        </div>
      </div>
    </Transition>
  </div>
</template>

<script setup>
import { ref, computed, watch, nextTick, onMounted, onUnmounted } from 'vue'

const props = defineProps({
  modelValue: { type: Array, default: () => [] },
  options: { type: Array, default: () => [] },
  placeholder: { type: String, default: 'All' },
  searchPlaceholder: { type: String, default: 'Search…' }
})

const emit = defineEmits(['update:modelValue'])

const rootEl = ref(null)
const searchEl = ref(null)
const open = ref(false)
const search = ref('')

const filteredOptions = computed(() => {
  if (!search.value) return props.options
  const q = search.value.toLowerCase()
  return props.options.filter(o => o.toLowerCase().includes(q))
})

const toggle = (opt) => {
  const current = [...props.modelValue]
  const idx = current.indexOf(opt)
  if (idx >= 0) {
    current.splice(idx, 1)
  } else {
    current.push(opt)
  }
  emit('update:modelValue', current)
}

const selectAll = () => {
  emit('update:modelValue', [...props.options])
}

const clearAll = () => {
  emit('update:modelValue', [])
}

// Focus search when opening
watch(open, (val) => {
  if (val) {
    search.value = ''
    nextTick(() => searchEl.value?.focus())
  }
})

// Click outside to close
const handleClickOutside = (e) => {
  if (open.value && rootEl.value && !rootEl.value.contains(e.target)) {
    open.value = false
  }
}

onMounted(() => document.addEventListener('click', handleClickOutside, true))
onUnmounted(() => document.removeEventListener('click', handleClickOutside, true))
</script>
