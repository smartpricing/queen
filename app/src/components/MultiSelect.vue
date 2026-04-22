<template>
  <div ref="rootEl" class="relative inline-block w-full" style="max-width: 220px;">
    <!-- Trigger -->
    <button
      class="ms-trigger"
      @click="open = !open"
    >
      <span class="ms-label">
        <template v-if="modelValue.length === 0">{{ placeholder }}</template>
        <template v-else-if="modelValue.length === 1">{{ modelValue[0] }}</template>
        <template v-else>{{ modelValue.length }} selected</template>
      </span>
      <svg class="ms-chevron" :class="{ 'ms-chevron-open': open }" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
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
      <div v-if="open" class="ms-dropdown">
        <!-- Search -->
        <div style="padding:8px; border-bottom:1px solid var(--bd);">
          <input
            ref="searchEl"
            v-model="search"
            type="text"
            class="ms-search"
            :placeholder="searchPlaceholder"
            @keydown.esc="open = false"
          />
        </div>

        <!-- Options -->
        <div style="max-height:192px; overflow-y:auto; padding:4px 0;">
          <div v-if="filteredOptions.length === 0" style="padding:8px 12px; font-size:12px; color:var(--text-low);">
            No matches
          </div>
          <label
            v-for="opt in filteredOptions"
            :key="opt"
            class="ms-option"
          >
            <input
              type="checkbox"
              class="ms-checkbox"
              :checked="modelValue.includes(opt)"
              @change="toggle(opt)"
            />
            <span class="ms-option-text">{{ opt }}</span>
          </label>
        </div>

        <!-- Footer -->
        <div v-if="options.length > 1" class="ms-footer">
          <button class="ms-action ms-action-primary" @click="selectAll">Select all</button>
          <button class="ms-action" @click="clearAll">Clear</button>
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
  if (idx >= 0) current.splice(idx, 1)
  else current.push(opt)
  emit('update:modelValue', current)
}

const selectAll = () => { emit('update:modelValue', [...props.options]) }
const clearAll = () => { emit('update:modelValue', []) }

watch(open, (val) => {
  if (val) { search.value = ''; nextTick(() => searchEl.value?.focus()) }
})

const handleClickOutside = (e) => {
  if (open.value && rootEl.value && !rootEl.value.contains(e.target)) open.value = false
}
onMounted(() => document.addEventListener('click', handleClickOutside, true))
onUnmounted(() => document.removeEventListener('click', handleClickOutside, true))
</script>

<style>
.ms-trigger {
  width: 100%; display: flex; align-items: center; justify-content: space-between; gap: 8px;
  padding: 5px 10px; font-size: 12px; border-radius: 5px;
  border: 1px solid var(--bd); color: var(--text-hi); cursor: pointer;
  transition: border-color .12s, background .12s;
}
html:not(.light) .ms-trigger { background: var(--ink-3); }
html.light .ms-trigger { background: #fff; }
.ms-trigger:hover { border-color: var(--bd-hi); }
.ms-trigger:focus-visible { outline: 1px solid rgba(255,255,255,0.2); outline-offset: 1px; }

.ms-label { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; text-align: left; flex: 1; }
.ms-chevron { width: 12px; height: 12px; flex-shrink: 0; color: var(--text-low); transition: transform .12s; }
.ms-chevron-open { transform: rotate(180deg); }

.ms-dropdown {
  position: absolute; z-index: 50; margin-top: 4px; width: 100%;
  border-radius: 6px; border: 1px solid var(--bd-hi);
  overflow: hidden; transform-origin: top;
}
html:not(.light) .ms-dropdown { background: var(--ink-3); box-shadow: 0 12px 32px -8px rgba(0,0,0,.6); }
html.light .ms-dropdown { background: #fff; box-shadow: 0 10px 30px -10px rgba(0,0,0,.15); }

.ms-search {
  width: 100%; padding: 5px 9px; font-size: 12px; border-radius: 4px;
  border: 1px solid var(--bd); color: var(--text-hi); outline: none;
  transition: border-color .12s;
}
html:not(.light) .ms-search { background: var(--ink-2); }
html.light .ms-search { background: var(--paper-1); }
.ms-search:focus { border-color: var(--bd-hi); box-shadow: 0 0 0 1px rgba(255,255,255,0.1); }
.ms-search::placeholder { color: var(--text-low); }

.ms-option {
  display: flex; align-items: center; gap: 8px;
  padding: 5px 12px; font-size: 12px; cursor: pointer; transition: background .1s;
}
.ms-option:hover { background: var(--ink-4); }
html.light .ms-option:hover { background: var(--paper-1); }

.ms-checkbox {
  width: 13px; height: 13px; border-radius: 3px; cursor: pointer;
  accent-color: var(--accent);
}

.ms-option-text { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; color: var(--text-hi); font-size: 12px; }

.ms-footer {
  display: flex; align-items: center; justify-content: space-between;
  padding: 6px 12px; border-top: 1px solid var(--bd);
}
.ms-action { font-size: 11px; color: var(--text-low); cursor: pointer; border: none; background: none; padding: 2px 0; font-family: inherit; transition: color .1s; }
.ms-action:hover { color: var(--text-hi); }
.ms-action-primary { color: var(--text-hi); font-weight: 500; }
.ms-action-primary:hover { color: var(--accent-dim); }
</style>
