<template>
  <div class="filter-container">
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
      <div class="sm:w-48">
        <CustomSelect
          v-model="localNamespace"
          @update:model-value="onNamespaceChange"
          :options="namespaceOptions"
          placeholder="All Namespaces"
          :clearable="true"
          :searchable="true"
        />
      </div>
      
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

<style scoped>
.filter-container {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem;
}

.dark .filter-container {
  background: #0a0d14;
}
</style>

<script setup>
import { ref, watch, computed } from 'vue';
import CustomSelect from '../common/CustomSelect.vue';

const props = defineProps({
  search: String,
  namespace: String,
  namespaces: Array,
});

const emit = defineEmits(['update:search', 'update:namespace']);

const localSearch = ref(props.search || '');
const localNamespace = ref(props.namespace || '');

const namespaceOptions = computed(() => [
  { value: '', label: 'All Namespaces' },
  ...(props.namespaces || []).map(ns => ({ 
    value: ns.namespace, 
    label: `${ns.namespace} (${ns.queues})` 
  }))
]);

watch(() => props.search, (newVal) => {
  localSearch.value = newVal || '';
});

watch(() => props.namespace, (newVal) => {
  localNamespace.value = newVal || '';
});

function onSearchChange() {
  emit('update:search', localSearch.value);
}

function onNamespaceChange(value) {
  localNamespace.value = value;
  emit('update:namespace', value);
}

function clearFilters() {
  localSearch.value = '';
  localNamespace.value = '';
  emit('update:search', '');
  emit('update:namespace', '');
}
</script>

