<template>
  <div class="queue-config-form">
    <div class="form-grid">
      <div class="field">
        <label for="ns">Namespace</label>
        <InputText 
          id="ns" 
          v-model="form.ns" 
          placeholder="e.g., email-service"
          :disabled="!!queue"
        />
      </div>

      <div class="field">
        <label for="task">Task</label>
        <InputText 
          id="task" 
          v-model="form.task" 
          placeholder="e.g., send-notifications"
          :disabled="!!queue"
        />
      </div>

      <div class="field span-2">
        <label for="queue">Queue Name</label>
        <InputText 
          id="queue" 
          v-model="form.queue" 
          placeholder="e.g., high-priority"
          :disabled="!!queue"
        />
      </div>

      <div class="field">
        <label for="priority">Priority</label>
        <InputNumber 
          id="priority" 
          v-model="form.options.priority" 
          :min="0" 
          :max="100"
          showButtons
        />
        <small>Higher priority queues are processed first</small>
      </div>

      <div class="field">
        <label for="leaseTime">Lease Time (seconds)</label>
        <InputNumber 
          id="leaseTime" 
          v-model="form.options.leaseTime" 
          :min="1" 
          :max="3600"
          suffix=" sec"
        />
        <small>Time a message is leased to a worker</small>
      </div>

      <div class="field">
        <label for="retryLimit">Retry Limit</label>
        <InputNumber 
          id="retryLimit" 
          v-model="form.options.retryLimit" 
          :min="0" 
          :max="10"
          showButtons
        />
        <small>Max retry attempts before dead letter</small>
      </div>

      <div class="field">
        <label for="maxSize">Max Size</label>
        <InputNumber 
          id="maxSize" 
          v-model="form.options.maxSize" 
          :min="1" 
          :max="1000000"
        />
        <small>Maximum messages in queue</small>
      </div>

      <div class="field">
        <label for="delayedProcessing">Delayed Processing (seconds)</label>
        <InputNumber 
          id="delayedProcessing" 
          v-model="form.options.delayedProcessing" 
          :min="0" 
          :max="3600"
          suffix=" sec"
        />
        <small>Wait time before messages become eligible</small>
      </div>

      <div class="field">
        <label for="windowBuffer">Window Buffer (seconds)</label>
        <InputNumber 
          id="windowBuffer" 
          v-model="form.options.windowBuffer" 
          :min="0" 
          :max="3600"
          suffix=" sec"
        />
        <small>Wait until last message is this old</small>
      </div>

      <div class="field span-2">
        <label>Additional Options</label>
        <div class="checkbox-group">
          <div class="checkbox-item">
            <Checkbox 
              id="deadLetterQueue" 
              v-model="form.options.deadLetterQueue" 
              :binary="true"
            />
            <label for="deadLetterQueue">Enable Dead Letter Queue</label>
          </div>
        </div>
      </div>
    </div>

    <div class="form-actions">
      <Button 
        label="Cancel" 
        class="p-button-text" 
        @click="$emit('cancel')"
      />
      <Button 
        label="Save Configuration" 
        icon="pi pi-save"
        :loading="saving"
        @click="save"
      />
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, watch } from 'vue'
import InputText from 'primevue/inputtext'
import InputNumber from 'primevue/inputnumber'
import Checkbox from 'primevue/checkbox'
import Button from 'primevue/button'
import { useQueuesStore } from '../../stores/queues'
import { useToast } from 'primevue/usetoast'

const props = defineProps({
  queue: Object
})

const emit = defineEmits(['save', 'cancel'])

const queuesStore = useQueuesStore()
const toast = useToast()

const saving = ref(false)

const form = reactive({
  ns: '',
  task: '',
  queue: '',
  options: {
    priority: 0,
    leaseTime: 300,
    retryLimit: 3,
    maxSize: 10000,
    delayedProcessing: 0,
    windowBuffer: 0,
    deadLetterQueue: false,
    ttl: 3600,
    retryDelay: 1000
  }
})

// Initialize form if editing existing queue
watch(() => props.queue, (queue) => {
  if (queue) {
    const [ns, task, queueName] = queue.queue.split('/')
    form.ns = ns
    form.task = task
    form.queue = queueName
    if (queue.options) {
      Object.assign(form.options, queue.options)
    }
  }
}, { immediate: true })

async function save() {
  // Validate
  if (!form.ns || !form.task || !form.queue) {
    toast.add({
      severity: 'error',
      summary: 'Validation Error',
      detail: 'Please fill in all required fields',
      life: 3000
    })
    return
  }

  saving.value = true
  try {
    await queuesStore.configureQueue({
      ns: form.ns,
      task: form.task,
      queue: form.queue,
      options: form.options
    })
    emit('save')
  } catch (error) {
    toast.add({
      severity: 'error',
      summary: 'Configuration Failed',
      detail: error.message,
      life: 5000
    })
  } finally {
    saving.value = false
  }
}
</script>

<style scoped>
.queue-config-form {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

.form-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 1rem;
}

.field {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.field.span-2 {
  grid-column: span 2;
}

.field label {
  font-weight: 500;
  color: #a3a3a3;
  font-size: 0.875rem;
}

.field small {
  color: #737373;
  font-size: 0.75rem;
}

.checkbox-group {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.checkbox-item {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.form-actions {
  display: flex;
  justify-content: flex-end;
  gap: 1rem;
  padding-top: 1rem;
  border-top: 1px solid var(--surface-300);
}
</style>
