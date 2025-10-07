<template>
  <div class="test-tool-page">
    <div class="page-header">
      <h1 class="page-title">Test Tool</h1>
      <Tag :value="testStatus" :severity="testSeverity" />
    </div>

    <div class="grid">
      <div class="col-12 lg:col-6">
        <Card>
          <template #title>Message Producer</template>
          <template #content>
            <div class="test-form">
              <div class="field">
                <label>Target Queue</label>
                <div class="grid">
                  <div class="col-4">
                    <InputText v-model="producer.ns" placeholder="Namespace" class="w-full" />
                  </div>
                  <div class="col-4">
                    <InputText v-model="producer.task" placeholder="Task" class="w-full" />
                  </div>
                  <div class="col-4">
                    <InputText v-model="producer.queue" placeholder="Queue" class="w-full" />
                  </div>
                </div>
              </div>
              
              <div class="field">
                <label>Message Count</label>
                <InputNumber v-model="producer.count" :min="1" :max="10000" class="w-full" />
              </div>
              
              <div class="field">
                <label>Delay Between Messages (ms)</label>
                <InputNumber v-model="producer.delay" :min="0" :max="5000" class="w-full" />
              </div>
              
              <div class="field">
                <label>Message Template</label>
                <Textarea 
                  v-model="producer.template" 
                  rows="5" 
                  class="w-full font-mono"
                  placeholder='{"type": "test", "id": "{{index}}", "timestamp": "{{timestamp}}"}'
                />
              </div>
              
              <div class="actions">
                <Button 
                  label="Start Producer" 
                  icon="pi pi-play" 
                  @click="startProducer"
                  :disabled="producerRunning"
                />
                <Button 
                  label="Stop" 
                  icon="pi pi-stop" 
                  class="p-button-danger"
                  @click="stopProducer"
                  :disabled="!producerRunning"
                />
              </div>
              
              <ProgressBar 
                v-if="producerRunning" 
                :value="producerProgress" 
                class="mt-3"
              />
            </div>
          </template>
        </Card>
      </div>

      <div class="col-12 lg:col-6">
        <Card>
          <template #title>Message Consumer</template>
          <template #content>
            <div class="test-form">
              <div class="field">
                <label>Source Queue</label>
                <div class="grid">
                  <div class="col-4">
                    <InputText v-model="consumer.ns" placeholder="Namespace" class="w-full" />
                  </div>
                  <div class="col-4">
                    <InputText v-model="consumer.task" placeholder="Task" class="w-full" />
                  </div>
                  <div class="col-4">
                    <InputText v-model="consumer.queue" placeholder="Queue" class="w-full" />
                  </div>
                </div>
              </div>
              
              <div class="field">
                <label>Batch Size</label>
                <InputNumber v-model="consumer.batchSize" :min="1" :max="100" class="w-full" />
              </div>
              
              <div class="field">
                <label>Processing Delay (ms)</label>
                <InputNumber v-model="consumer.processingDelay" :min="0" :max="5000" class="w-full" />
              </div>
              
              <div class="field">
                <label>Error Rate (%)</label>
                <Slider v-model="consumer.errorRate" :min="0" :max="100" class="w-full" />
                <small>{{ consumer.errorRate }}% of messages will fail</small>
              </div>
              
              <div class="actions">
                <Button 
                  label="Start Consumer" 
                  icon="pi pi-play" 
                  @click="startConsumer"
                  :disabled="consumerRunning"
                />
                <Button 
                  label="Stop" 
                  icon="pi pi-stop" 
                  class="p-button-danger"
                  @click="stopConsumer"
                  :disabled="!consumerRunning"
                />
              </div>
              
              <div v-if="consumerRunning" class="consumer-stats mt-3">
                <div class="stat">
                  <span>Processed:</span>
                  <span>{{ consumerStats.processed }}</span>
                </div>
                <div class="stat">
                  <span>Success:</span>
                  <span class="text-green-500">{{ consumerStats.success }}</span>
                </div>
                <div class="stat">
                  <span>Failed:</span>
                  <span class="text-red-500">{{ consumerStats.failed }}</span>
                </div>
              </div>
            </div>
          </template>
        </Card>
      </div>
    </div>

    <Card class="mt-3">
      <template #title>Test Results</template>
      <template #content>
        <DataTable :value="testResults" responsiveLayout="scroll">
          <Column field="timestamp" header="Time" :sortable="true">
            <template #body="{ data }">
              {{ formatTime(data.timestamp) }}
            </template>
          </Column>
          <Column field="type" header="Type" :sortable="true">
            <template #body="{ data }">
              <Tag :value="data.type" :severity="data.type === 'producer' ? 'info' : 'warning'" />
            </template>
          </Column>
          <Column field="action" header="Action"></Column>
          <Column field="details" header="Details"></Column>
          <Column field="status" header="Status">
            <template #body="{ data }">
              <Tag :value="data.status" :severity="getStatusSeverity(data.status)" />
            </template>
          </Column>
        </DataTable>
      </template>
    </Card>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import { useToast } from 'primevue/usetoast'
import { format } from 'date-fns'
import Button from 'primevue/button'
import Card from 'primevue/card'
import Tag from 'primevue/tag'
import InputText from 'primevue/inputtext'
import InputNumber from 'primevue/inputnumber'
import Textarea from 'primevue/textarea'
import Slider from 'primevue/slider'
import ProgressBar from 'primevue/progressbar'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import { api } from '../utils/api'

const toast = useToast()

// State
const producerRunning = ref(false)
const consumerRunning = ref(false)
const producerProgress = ref(0)
const testResults = ref([])

const producer = ref({
  ns: 'test',
  task: 'benchmark',
  queue: 'items',
  count: 100,
  delay: 10,
  template: JSON.stringify({
    type: 'test',
    id: '{{index}}',
    timestamp: '{{timestamp}}',
    data: {
      value: '{{random}}'
    }
  }, null, 2)
})

const consumer = ref({
  ns: 'test',
  task: 'benchmark',
  queue: 'items',
  batchSize: 10,
  processingDelay: 50,
  errorRate: 5
})

const consumerStats = ref({
  processed: 0,
  success: 0,
  failed: 0
})

// Computed
const testStatus = computed(() => {
  if (producerRunning.value && consumerRunning.value) return 'Running'
  if (producerRunning.value) return 'Producing'
  if (consumerRunning.value) return 'Consuming'
  return 'Idle'
})

const testSeverity = computed(() => {
  if (producerRunning.value || consumerRunning.value) return 'warning'
  return 'secondary'
})

// Methods
const startProducer = async () => {
  producerRunning.value = true
  producerProgress.value = 0
  
  const template = producer.value.template
  const total = producer.value.count
  
  for (let i = 0; i < total; i++) {
    if (!producerRunning.value) break
    
    const message = template
      .replace('{{index}}', i)
      .replace('{{timestamp}}', new Date().toISOString())
      .replace('{{random}}', Math.random().toString(36).substring(7))
    
    try {
      await api.post('/push', {
        items: [{
          ns: producer.value.ns,
          task: producer.value.task,
          queue: producer.value.queue,
          data: JSON.parse(message)
        }]
      })
      
      addTestResult('producer', 'Push message', `Message ${i + 1}/${total}`, 'success')
    } catch (error) {
      addTestResult('producer', 'Push message', error.message, 'error')
    }
    
    producerProgress.value = ((i + 1) / total) * 100
    
    if (producer.value.delay > 0) {
      await new Promise(resolve => setTimeout(resolve, producer.value.delay))
    }
  }
  
  producerRunning.value = false
  toast.add({
    severity: 'success',
    summary: 'Producer Complete',
    detail: `Pushed ${total} messages`,
    life: 3000
  })
}

const stopProducer = () => {
  producerRunning.value = false
  toast.add({
    severity: 'info',
    summary: 'Producer Stopped',
    life: 2000
  })
}

const startConsumer = async () => {
  consumerRunning.value = true
  consumerStats.value = { processed: 0, success: 0, failed: 0 }
  
  while (consumerRunning.value) {
    try {
      const response = await api.get(`/pop/ns/${consumer.value.ns}/task/${consumer.value.task}/queue/${consumer.value.queue}?batch=${consumer.value.batchSize}`)
      
      if (response.messages && response.messages.length > 0) {
        for (const message of response.messages) {
          consumerStats.value.processed++
          
          // Simulate processing with error rate
          const shouldFail = Math.random() * 100 < consumer.value.errorRate
          
          if (shouldFail) {
            consumerStats.value.failed++
            await api.post('/ack', {
              transactionId: message.transactionId,
              status: 'failed',
              errorMessage: 'Simulated error'
            })
            addTestResult('consumer', 'Process message', 'Simulated failure', 'error')
          } else {
            consumerStats.value.success++
            await api.post('/ack', {
              transactionId: message.transactionId,
              status: 'completed'
            })
            addTestResult('consumer', 'Process message', `ID: ${message.transactionId.substring(0, 8)}...`, 'success')
          }
          
          if (consumer.value.processingDelay > 0) {
            await new Promise(resolve => setTimeout(resolve, consumer.value.processingDelay))
          }
        }
      } else {
        // No messages, wait a bit
        await new Promise(resolve => setTimeout(resolve, 1000))
      }
    } catch (error) {
      console.error('Consumer error:', error)
      await new Promise(resolve => setTimeout(resolve, 1000))
    }
  }
}

const stopConsumer = () => {
  consumerRunning.value = false
  toast.add({
    severity: 'info',
    summary: 'Consumer Stopped',
    detail: `Processed: ${consumerStats.value.processed}, Success: ${consumerStats.value.success}, Failed: ${consumerStats.value.failed}`,
    life: 3000
  })
}

const addTestResult = (type, action, details, status) => {
  testResults.value.unshift({
    timestamp: new Date(),
    type,
    action,
    details,
    status
  })
  
  // Keep only last 100 results
  if (testResults.value.length > 100) {
    testResults.value.pop()
  }
}

const formatTime = (date) => {
  return format(new Date(date), 'HH:mm:ss.SSS')
}

const getStatusSeverity = (status) => {
  return status === 'success' ? 'success' : 'danger'
}
</script>

<style scoped>
.test-tool-page {
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

.test-form .field {
  margin-bottom: 1.5rem;
}

.test-form label {
  display: block;
  margin-bottom: 0.5rem;
  font-weight: 500;
  color: #a3a3a3;
}

.actions {
  display: flex;
  gap: 0.5rem;
}

.consumer-stats {
  display: flex;
  justify-content: space-around;
  padding: 1rem;
  background: var(--surface-100);
  border-radius: 4px;
}

.consumer-stats .stat {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 0.25rem;
}

.consumer-stats .stat span:first-child {
  font-size: 0.875rem;
  color: #a3a3a3;
}

.consumer-stats .stat span:last-child {
  font-size: 1.25rem;
  font-weight: 600;
}

.font-mono {
  font-family: monospace;
}

.text-green-500 {
  color: #10b981;
}

.text-red-500 {
  color: #ef4444;
}

.w-full {
  width: 100%;
}

.mt-3 {
  margin-top: 1rem;
}
</style>
