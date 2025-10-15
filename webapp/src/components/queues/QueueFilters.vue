<template>
  <div class="card">
    <div class="flex flex-col sm:flex-row gap-3">
      <!-- Search -->
      <div class="flex-1">
        <input
          v-model="localSearch"
          @input="onSearchChange"
          type="text"
          placeholder="Search queues..."
          class="input"
        />
      </div>
      
      <!-- Namespace Filter -->
      <select
        v-model="localNamespace"
        @change="onNamespaceChange"
        class="input sm:w-48"
      >
        <option value="">All Namespaces</option>
        <option v-for="ns in namespaces" :key="ns.namespace" :value="ns.namespace">
          {{ ns.namespace }} ({{ ns.queues }})
        </option>
      </select>
      
      <!-- Clear Filters -->
      <button
        v-if="localSearch || localNamespace"
        @click="clearFilters"
        class="btn btn-secondary whitespace-nowrap"
      >
        Clear Filters
      </button>
    </div>
  </div>
</template>

<script setup>
import { ref, watch } from 'vue';

const props = defineProps({
  search: String,
  namespace: String,
  namespaces: Array,
});

const emit = defineEmits(['update:search', 'update:namespace']);

const localSearch = ref(props.search || '');
const localNamespace = ref(props.namespace || '');

watch(() => props.search, (newVal) => {
  localSearch.value = newVal || '';
});

watch(() => props.namespace, (newVal) => {
  localNamespace.value = newVal || '';
});

function onSearchChange() {
  emit('update:search', localSearch.value);
}

function onNamespaceChange() {
  emit('update:namespace', localNamespace.value);
}

function clearFilters() {
  localSearch.value = '';
  localNamespace.value = '';
  emit('update:search', '');
  emit('update:namespace', '');
}
</script>

