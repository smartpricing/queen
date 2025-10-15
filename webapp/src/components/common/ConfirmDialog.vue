<template>
  <div v-if="isOpen" class="fixed inset-0 z-50 overflow-y-auto">
    <div class="flex items-center justify-center min-h-screen px-4">
      <!-- Backdrop -->
      <div 
        class="fixed inset-0 bg-black bg-opacity-50 transition-opacity"
        @click="$emit('cancel')"
      ></div>
      
      <!-- Modal -->
      <div class="relative bg-white dark:bg-slate-800 rounded-xl shadow-xl max-w-md w-full p-6">
        <div class="mb-4">
          <h3 class="text-lg font-bold mb-2">{{ title }}</h3>
          <p class="text-sm text-gray-600 dark:text-gray-400">{{ message }}</p>
        </div>
        
        <div class="flex gap-3">
          <button @click="$emit('cancel')" class="btn btn-secondary flex-1">
            {{ cancelText }}
          </button>
          <button @click="$emit('confirm')" :class="`btn ${confirmClass} flex-1`">
            {{ confirmText }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
defineProps({
  isOpen: Boolean,
  title: {
    type: String,
    default: 'Confirm Action',
  },
  message: {
    type: String,
    default: 'Are you sure you want to proceed?',
  },
  confirmText: {
    type: String,
    default: 'Confirm',
  },
  cancelText: {
    type: String,
    default: 'Cancel',
  },
  confirmClass: {
    type: String,
    default: 'btn-primary',
  },
});

defineEmits(['confirm', 'cancel']);
</script>

