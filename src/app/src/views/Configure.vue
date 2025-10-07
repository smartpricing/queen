<template>
  <div class="configure-page">
    <div class="page-header">
      <h1 class="page-title">Configure</h1>
    </div>

    <div class="grid">
      <div class="col-12 lg:col-8">
        <Card>
          <template #title>Queue Configuration</template>
          <template #content>
            <div class="config-section">
              <h3>Select Queue</h3>
              <div class="grid">
                <div class="col-12 md:col-4">
                  <label>Namespace</label>
                  <Dropdown 
                    v-model="selectedNamespace" 
                    :options="namespaces" 
                    optionLabel="label"
                    optionValue="value"
                    placeholder="Select Namespace"
                    class="w-full"
                  />
                </div>
                <div class="col-12 md:col-4">
                  <label>Task</label>
                  <Dropdown 
                    v-model="selectedTask" 
                    :options="tasks" 
                    optionLabel="label"
                    optionValue="value"
                    placeholder="Select Task"
                    class="w-full"
                    :disabled="!selectedNamespace"
                  />
                </div>
                <div class="col-12 md:col-4">
                  <label>Queue</label>
                  <Dropdown 
                    v-model="selectedQueue" 
                    :options="queues" 
                    optionLabel="label"
                    optionValue="value"
                    placeholder="Select Queue"
                    class="w-full"
                    :disabled="!selectedTask"
                  />
                </div>
              </div>
            </div>

            <div v-if="selectedQueue" class="config-form">
              <h3>Queue Settings</h3>
              <QueueConfigForm 
                :queue="currentQueueConfig" 
                @save="saveConfiguration"
              />
            </div>
          </template>
        </Card>
      </div>

      <div class="col-12 lg:col-4">
        <Card>
          <template #title>Global Settings</template>
          <template #content>
            <div class="global-settings">
              <div class="setting-item">
                <label>Default Lease Time (seconds)</label>
                <InputNumber 
                  v-model="globalSettings.defaultLeaseTime" 
                  :min="60" 
                  :max="3600"
                  class="w-full"
                />
              </div>
              <div class="setting-item">
                <label>Max Retry Attempts</label>
                <InputNumber 
                  v-model="globalSettings.maxRetries" 
                  :min="0" 
                  :max="10"
                  class="w-full"
                />
              </div>
              <div class="setting-item">
                <label>Dead Letter Queue</label>
                <InputSwitch v-model="globalSettings.enableDLQ" />
              </div>
              <Button 
                label="Save Global Settings" 
                icon="pi pi-save" 
                class="w-full mt-3"
                @click="saveGlobalSettings"
              />
            </div>
          </template>
        </Card>

        <Card class="mt-3">
          <template #title>Import/Export</template>
          <template #content>
            <div class="import-export">
              <Button 
                label="Export Configuration" 
                icon="pi pi-download" 
                class="w-full mb-2"
                @click="exportConfig"
              />
              <Button 
                label="Import Configuration" 
                icon="pi pi-upload" 
                class="p-button-outlined w-full"
                @click="importConfig"
              />
            </div>
          </template>
        </Card>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, watch } from 'vue'
import { useToast } from 'primevue/usetoast'
import Button from 'primevue/button'
import Card from 'primevue/card'
import Dropdown from 'primevue/dropdown'
import InputNumber from 'primevue/inputnumber'
import InputSwitch from 'primevue/inputswitch'
import QueueConfigForm from '../components/queues/QueueConfigForm.vue'
import { api } from '../utils/api'

const toast = useToast()

// State
const selectedNamespace = ref(null)
const selectedTask = ref(null)
const selectedQueue = ref(null)
const currentQueueConfig = ref(null)

const namespaces = ref([
  { label: 'Production', value: 'production' },
  { label: 'Staging', value: 'staging' },
  { label: 'Development', value: 'development' }
])

const tasks = ref([])
const queues = ref([])

const globalSettings = ref({
  defaultLeaseTime: 300,
  maxRetries: 3,
  enableDLQ: true
})

// Watch for namespace changes
watch(selectedNamespace, (newVal) => {
  if (newVal) {
    tasks.value = [
      { label: 'Email', value: 'email' },
      { label: 'SMS', value: 'sms' },
      { label: 'Push', value: 'push' }
    ]
  } else {
    tasks.value = []
    selectedTask.value = null
  }
})

// Watch for task changes
watch(selectedTask, (newVal) => {
  if (newVal) {
    queues.value = [
      { label: 'High Priority', value: 'high' },
      { label: 'Normal', value: 'normal' },
      { label: 'Low Priority', value: 'low' }
    ]
  } else {
    queues.value = []
    selectedQueue.value = null
  }
})

// Watch for queue selection
watch(selectedQueue, async (newVal) => {
  if (newVal) {
    await loadQueueConfig()
  }
})

// Methods
const loadQueueConfig = async () => {
  try {
    // Load queue configuration
    currentQueueConfig.value = {
      ns: selectedNamespace.value,
      task: selectedTask.value,
      queue: selectedQueue.value,
      options: {
        leaseTime: 300,
        retryLimit: 3,
        delayedProcessing: 0,
        windowBuffer: 0,
        priority: 0
      }
    }
  } catch (error) {
    console.error('Failed to load queue config:', error)
  }
}

const saveConfiguration = async (config) => {
  try {
    await api.post('/configure', {
      ns: selectedNamespace.value,
      task: selectedTask.value,
      queue: selectedQueue.value,
      options: config
    })
    
    toast.add({
      severity: 'success',
      summary: 'Success',
      detail: 'Queue configuration saved',
      life: 3000
    })
  } catch (error) {
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to save configuration',
      life: 3000
    })
  }
}

const saveGlobalSettings = () => {
  toast.add({
    severity: 'success',
    summary: 'Success',
    detail: 'Global settings saved',
    life: 3000
  })
}

const exportConfig = () => {
  const config = {
    globalSettings: globalSettings.value,
    queues: []
  }
  
  const data = JSON.stringify(config, null, 2)
  const blob = new Blob([data], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = 'queen-config.json'
  a.click()
  URL.revokeObjectURL(url)
  
  toast.add({
    severity: 'success',
    summary: 'Success',
    detail: 'Configuration exported',
    life: 3000
  })
}

const importConfig = () => {
  const input = document.createElement('input')
  input.type = 'file'
  input.accept = '.json'
  input.onchange = (e) => {
    const file = e.target.files[0]
    const reader = new FileReader()
    reader.onload = (event) => {
      try {
        const config = JSON.parse(event.target.result)
        // Apply imported configuration
        toast.add({
          severity: 'success',
          summary: 'Success',
          detail: 'Configuration imported',
          life: 3000
        })
      } catch (error) {
        toast.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Invalid configuration file',
          life: 3000
        })
      }
    }
    reader.readAsText(file)
  }
  input.click()
}
</script>

<style scoped>
.configure-page {
  padding: 1rem;
}

.page-header {
  margin-bottom: 1.5rem;
}

.page-title {
  font-size: 1.75rem;
  font-weight: 600;
  color: white;
  margin: 0;
}

.config-section {
  margin-bottom: 2rem;
  padding-bottom: 2rem;
  border-bottom: 1px solid var(--surface-300);
}

.config-section h3 {
  color: white;
  margin-bottom: 1rem;
}

.config-section label {
  display: block;
  margin-bottom: 0.5rem;
  font-weight: 500;
  color: #a3a3a3;
}

.config-form h3 {
  color: white;
  margin-bottom: 1rem;
}

.global-settings .setting-item {
  margin-bottom: 1.5rem;
}

.global-settings label {
  display: block;
  margin-bottom: 0.5rem;
  font-weight: 500;
  color: #a3a3a3;
}

.w-full {
  width: 100%;
}

.mt-3 {
  margin-top: 1rem;
}

.mb-2 {
  margin-bottom: 0.5rem;
}
</style>
