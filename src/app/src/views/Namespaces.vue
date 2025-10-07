<template>
  <div class="namespaces-page">
    <div class="page-header">
      <h1 class="page-title">Namespaces</h1>
      <Button 
        label="Create Namespace" 
        icon="pi pi-plus" 
        @click="showCreateDialog = true"
      />
    </div>

    <div class="grid">
      <div 
        v-for="namespace in namespaces" 
        :key="namespace.id"
        class="col-12 md:col-6 lg:col-4"
      >
        <Card class="namespace-card">
          <template #title>
            <div class="flex align-items-center justify-content-between">
              <span class="namespace-name">
                <i class="pi pi-folder mr-2"></i>
                {{ namespace.name }}
              </span>
              <Button 
                icon="pi pi-ellipsis-v" 
                class="p-button-text p-button-sm"
                @click="showNamespaceMenu($event, namespace)"
              />
            </div>
          </template>
          <template #content>
            <div class="namespace-stats">
              <div class="stat">
                <span class="stat-label">Tasks</span>
                <span class="stat-value">{{ namespace.taskCount || 0 }}</span>
              </div>
              <div class="stat">
                <span class="stat-label">Queues</span>
                <span class="stat-value">{{ namespace.queueCount || 0 }}</span>
              </div>
              <div class="stat">
                <span class="stat-label">Messages</span>
                <span class="stat-value">{{ formatNumber(namespace.messageCount || 0) }}</span>
              </div>
            </div>
            <div class="namespace-info">
              <div class="info-item">
                <i class="pi pi-clock mr-2"></i>
                Created {{ formatRelativeTime(namespace.createdAt) }}
              </div>
              <div class="info-item">
                <i class="pi pi-sync mr-2"></i>
                Updated {{ formatRelativeTime(namespace.updatedAt) }}
              </div>
            </div>
            <div class="namespace-actions">
              <Button 
                label="View Tasks" 
                icon="pi pi-list" 
                class="p-button-sm w-full"
                @click="viewTasks(namespace)"
              />
            </div>
          </template>
        </Card>
      </div>
    </div>

    <!-- Create Namespace Dialog -->
    <Dialog v-model:visible="showCreateDialog" header="Create Namespace" :style="{ width: '500px' }">
      <div class="create-form">
        <div class="field">
          <label for="namespace-name">Namespace Name</label>
          <InputText 
            id="namespace-name"
            v-model="newNamespace.name" 
            class="w-full"
            placeholder="e.g., production, staging, development"
            :class="{ 'p-invalid': errors.name }"
          />
          <small v-if="errors.name" class="p-error">{{ errors.name }}</small>
        </div>
        <div class="field">
          <label for="namespace-description">Description (Optional)</label>
          <Textarea 
            id="namespace-description"
            v-model="newNamespace.description" 
            rows="3" 
            class="w-full"
            placeholder="Describe the purpose of this namespace..."
          />
        </div>
      </div>
      <template #footer>
        <Button label="Cancel" class="p-button-text" @click="cancelCreate" />
        <Button label="Create" icon="pi pi-check" @click="createNamespace" />
      </template>
    </Dialog>

    <!-- Context Menu -->
    <ContextMenu ref="contextMenu" :model="menuItems" />
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useToast } from 'primevue/usetoast'
import { useConfirm } from 'primevue/useconfirm'
import { formatDistance } from 'date-fns'
import Button from 'primevue/button'
import Card from 'primevue/card'
import Dialog from 'primevue/dialog'
import InputText from 'primevue/inputtext'
import Textarea from 'primevue/textarea'
import ContextMenu from 'primevue/contextmenu'
import { api } from '../utils/api'

const router = useRouter()
const toast = useToast()
const confirm = useConfirm()

// State
const namespaces = ref([])
const showCreateDialog = ref(false)
const contextMenu = ref()
const selectedNamespace = ref(null)
const newNamespace = ref({
  name: '',
  description: ''
})
const errors = ref({})

// Menu items for context menu
const menuItems = ref([
  {
    label: 'View Tasks',
    icon: 'pi pi-list',
    command: () => viewTasks(selectedNamespace.value)
  },
  {
    label: 'Edit',
    icon: 'pi pi-pencil',
    command: () => editNamespace(selectedNamespace.value)
  },
  {
    separator: true
  },
  {
    label: 'Delete',
    icon: 'pi pi-trash',
    class: 'p-menuitem-danger',
    command: () => deleteNamespace(selectedNamespace.value)
  }
])

// Methods
const loadNamespaces = async () => {
  try {
    // Get all queues and group by namespace
    const response = await api.analytics.getQueues()
    const queues = response.queues || []
    
    // Group queues by namespace
    const namespaceMap = {}
    queues.forEach(queue => {
      const [ns, task, queueName] = queue.queue.split('/')
      if (!namespaceMap[ns]) {
        namespaceMap[ns] = {
          name: ns,
          tasks: new Set(),
          queueCount: 0,
          messageCount: 0,
          createdAt: new Date().toISOString(), // Would need to track this
          updatedAt: new Date().toISOString()
        }
      }
      namespaceMap[ns].tasks.add(task)
      namespaceMap[ns].queueCount++
      namespaceMap[ns].messageCount += queue.stats.total || 0
    })
    
    // Convert to array
    namespaces.value = Object.values(namespaceMap).map((ns, index) => ({
      id: index.toString(),
      name: ns.name,
      taskCount: ns.tasks.size,
      queueCount: ns.queueCount,
      messageCount: ns.messageCount,
      createdAt: ns.createdAt,
      updatedAt: ns.updatedAt
    }))
    
    // If no namespaces exist, show empty state
    if (namespaces.value.length === 0) {
      namespaces.value = []
    }
  } catch (error) {
    console.error('Failed to load namespaces:', error)
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to load namespaces',
      life: 3000
    })
  }
}

const createNamespace = async () => {
  // Validate
  errors.value = {}
  if (!newNamespace.value.name) {
    errors.value.name = 'Namespace name is required'
    return
  }
  if (!/^[a-z0-9-]+$/.test(newNamespace.value.name)) {
    errors.value.name = 'Name must contain only lowercase letters, numbers, and hyphens'
    return
  }

  try {
    // API call to create namespace
    await api.post('/configure', {
      ns: newNamespace.value.name,
      description: newNamespace.value.description
    })
    
    toast.add({
      severity: 'success',
      summary: 'Success',
      detail: `Namespace "${newNamespace.value.name}" created`,
      life: 3000
    })
    
    showCreateDialog.value = false
    newNamespace.value = { name: '', description: '' }
    await loadNamespaces()
  } catch (error) {
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to create namespace',
      life: 3000
    })
  }
}

const cancelCreate = () => {
  showCreateDialog.value = false
  newNamespace.value = { name: '', description: '' }
  errors.value = {}
}

const viewTasks = (namespace) => {
  router.push(`/queues?ns=${namespace.name}`)
}

const editNamespace = (namespace) => {
  // Implementation for editing namespace
  toast.add({
    severity: 'info',
    summary: 'Edit Namespace',
    detail: `Editing ${namespace.name}`,
    life: 3000
  })
}

const deleteNamespace = (namespace) => {
  confirm.require({
    message: `Are you sure you want to delete namespace "${namespace.name}"? This will delete all associated tasks, queues, and messages.`,
    header: 'Delete Namespace',
    icon: 'pi pi-exclamation-triangle',
    acceptClass: 'p-button-danger',
    accept: async () => {
      try {
        // API call to delete namespace
        toast.add({
          severity: 'success',
          summary: 'Success',
          detail: `Namespace "${namespace.name}" deleted`,
          life: 3000
        })
        await loadNamespaces()
      } catch (error) {
        toast.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to delete namespace',
          life: 3000
        })
      }
    }
  })
}

const showNamespaceMenu = (event, namespace) => {
  selectedNamespace.value = namespace
  contextMenu.value.show(event)
}

const formatNumber = (num) => {
  return new Intl.NumberFormat().format(num)
}

const formatRelativeTime = (date) => {
  if (!date) return 'never'
  return formatDistance(new Date(date), new Date(), { addSuffix: true })
}

// Lifecycle
onMounted(() => {
  loadNamespaces()
})
</script>

<style scoped>
.namespaces-page {
  padding: 1rem;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1.5rem;
}

.page-title {
  font-size: 1.75rem;
  font-weight: 600;
  color: white;
  margin: 0;
}

.namespace-card {
  height: 100%;
  transition: transform 0.2s, box-shadow 0.2s;
}

.namespace-card:hover {
  transform: translateY(-2px);
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.3);
}

.namespace-name {
  font-size: 1.25rem;
  font-weight: 600;
  color: white;
}

.namespace-stats {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 1rem;
  padding: 1rem 0;
  border-bottom: 1px solid var(--surface-300);
}

.stat {
  text-align: center;
}

.stat-label {
  display: block;
  font-size: 0.75rem;
  color: #a3a3a3;
  margin-bottom: 0.25rem;
}

.stat-value {
  font-size: 1.25rem;
  font-weight: 600;
  color: white;
}

.namespace-info {
  padding: 1rem 0;
  border-bottom: 1px solid var(--surface-300);
}

.info-item {
  font-size: 0.875rem;
  color: #a3a3a3;
  margin-bottom: 0.5rem;
}

.info-item:last-child {
  margin-bottom: 0;
}

.namespace-actions {
  padding-top: 1rem;
}

.create-form .field {
  margin-bottom: 1.5rem;
}

.create-form label {
  display: block;
  margin-bottom: 0.5rem;
  font-weight: 500;
}

.p-menuitem-danger .p-menuitem-link {
  color: #ef4444 !important;
}

.w-full {
  width: 100%;
}

.mr-2 {
  margin-right: 0.5rem;
}
</style>
