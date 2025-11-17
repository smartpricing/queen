<template>
  <div class="relative group">
    <label v-if="label" class="text-xs text-gray-500 dark:text-gray-400 block mb-2">{{ label }}</label>
    <div 
      class="bg-white dark:bg-[#161b22] rounded-lg p-4 overflow-x-auto scrollbar-thin border border-gray-200/60 dark:border-gray-800/60 relative"
      :class="compact ? 'text-xs' : 'text-sm'"
    >
      <!-- Copy button inside -->
      <button
        @click="copyToClipboard"
        class="absolute top-3 right-3 px-2 py-1 text-xs rounded transition-all flex items-center gap-1.5 z-10"
        :class="copied 
          ? 'bg-green-100 dark:bg-green-900/50 text-green-700 dark:text-green-400 border border-green-300 dark:border-green-700' 
          : 'bg-gray-100 dark:bg-slate-700 text-gray-600 dark:text-gray-400 hover:bg-gray-200 dark:hover:bg-slate-600 border border-gray-300 dark:border-gray-600'"
      >
        <svg v-if="!copied" class="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 16H6a2 2 0 01-2-2V6a2 2 0 012-2h8a2 2 0 012 2v2m-6 12h8a2 2 0 002-2v-8a2 2 0 00-2-2h-8a2 2 0 00-2 2v8a2 2 0 002 2z" />
        </svg>
        <svg v-else class="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7" />
        </svg>
        {{ copied ? 'Copied!' : 'Copy' }}
      </button>
      
      <pre class="font-mono leading-relaxed pr-20" v-html="highlightedJson"></pre>
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue';

const props = defineProps({
  data: {
    type: [Object, Array, String],
    required: true
  },
  label: {
    type: String,
    default: ''
  },
  compact: {
    type: Boolean,
    default: false
  }
});

const copied = ref(false);

const jsonString = computed(() => {
  if (typeof props.data === 'string') {
    try {
      // Try to parse and re-stringify for consistent formatting
      const parsed = JSON.parse(props.data);
      return JSON.stringify(parsed, null, 2);
    } catch {
      return props.data;
    }
  }
  return JSON.stringify(props.data, null, 2);
});

const highlightedJson = computed(() => {
  return syntaxHighlight(jsonString.value);
});

function syntaxHighlight(json) {
  // Escape HTML
  json = json.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  
  // Apply syntax highlighting with Tailwind classes
  return json.replace(
    /("(\\u[a-zA-Z0-9]{4}|\\[^u]|[^\\"])*"(\s*:)?|\b(true|false|null)\b|-?\d+(?:\.\d*)?(?:[eE][+\-]?\d+)?)/g,
    (match) => {
      let cls = 'text-orange-600 dark:text-orange-400'; // number
      if (/^"/.test(match)) {
        if (/:$/.test(match)) {
          cls = 'text-blue-600 dark:text-blue-400 font-medium'; // key
        } else {
          cls = 'text-green-600 dark:text-green-400'; // string
        }
      } else if (/true|false/.test(match)) {
        cls = 'text-purple-600 dark:text-purple-400'; // boolean
      } else if (/null/.test(match)) {
        cls = 'text-gray-500 dark:text-gray-500'; // null
      }
      return `<span class="${cls}">${match}</span>`;
    }
  );
}

async function copyToClipboard() {
  try {
    await navigator.clipboard.writeText(jsonString.value);
    copied.value = true;
    setTimeout(() => {
      copied.value = false;
    }, 2000);
  } catch (err) {
    console.error('Failed to copy:', err);
  }
}
</script>

