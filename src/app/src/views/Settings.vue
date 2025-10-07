<template>
  <div class="settings-page">
    <div class="page-header">
      <h1 class="page-title">Settings</h1>
    </div>

    <div class="grid">
      <div class="col-12 lg:col-8">
        <TabView>
          <TabPanel header="General">
            <div class="settings-section">
              <h3>Application Settings</h3>
              <div class="setting-item">
                <label>Dashboard Refresh Interval</label>
                <div class="setting-control">
                  <InputNumber 
                    v-model="settings.refreshInterval" 
                    :min="1" 
                    :max="60"
                    suffix=" seconds"
                    class="w-full"
                  />
                  <small>How often to refresh dashboard data</small>
                </div>
              </div>
              
              <div class="setting-item">
                <label>Default Page Size</label>
                <div class="setting-control">
                  <Dropdown 
                    v-model="settings.pageSize" 
                    :options="pageSizeOptions" 
                    class="w-full"
                  />
                  <small>Default number of items per page in tables</small>
                </div>
              </div>
              
              <div class="setting-item">
                <label>Time Zone</label>
                <div class="setting-control">
                  <Dropdown 
                    v-model="settings.timezone" 
                    :options="timezoneOptions" 
                    optionLabel="label"
                    optionValue="value"
                    class="w-full"
                  />
                  <small>Display times in this timezone</small>
                </div>
              </div>
              
              <div class="setting-item">
                <label>Date Format</label>
                <div class="setting-control">
                  <Dropdown 
                    v-model="settings.dateFormat" 
                    :options="dateFormatOptions" 
                    optionLabel="label"
                    optionValue="value"
                    class="w-full"
                  />
                  <small>Format for displaying dates</small>
                </div>
              </div>
            </div>
          </TabPanel>
          
          <TabPanel header="Notifications">
            <div class="settings-section">
              <h3>Notification Preferences</h3>
              <div class="setting-item">
                <label>Enable Notifications</label>
                <div class="setting-control">
                  <InputSwitch v-model="settings.notifications.enabled" />
                  <small>Show toast notifications for events</small>
                </div>
              </div>
              
              <div class="setting-item">
                <label>Sound Alerts</label>
                <div class="setting-control">
                  <InputSwitch v-model="settings.notifications.sound" />
                  <small>Play sound for critical alerts</small>
                </div>
              </div>
              
              <div class="setting-item">
                <label>Email Alerts</label>
                <div class="setting-control">
                  <InputText 
                    v-model="settings.notifications.email" 
                    placeholder="admin@example.com"
                    class="w-full"
                  />
                  <small>Send critical alerts to this email</small>
                </div>
              </div>
              
              <div class="setting-item">
                <label>Alert Thresholds</label>
                <div class="threshold-grid">
                  <div class="threshold-item">
                    <span>Queue Depth</span>
                    <InputNumber 
                      v-model="settings.notifications.thresholds.queueDepth" 
                      :min="100" 
                      :max="10000"
                    />
                  </div>
                  <div class="threshold-item">
                    <span>Error Rate (%)</span>
                    <InputNumber 
                      v-model="settings.notifications.thresholds.errorRate" 
                      :min="1" 
                      :max="100"
                    />
                  </div>
                  <div class="threshold-item">
                    <span>Processing Time (ms)</span>
                    <InputNumber 
                      v-model="settings.notifications.thresholds.processingTime" 
                      :min="100" 
                      :max="10000"
                    />
                  </div>
                </div>
              </div>
            </div>
          </TabPanel>
          
          <TabPanel header="API">
            <div class="settings-section">
              <h3>API Configuration</h3>
              <div class="setting-item">
                <label>API Endpoint</label>
                <div class="setting-control">
                  <InputText 
                    v-model="settings.api.endpoint" 
                    placeholder="http://localhost:6632/api/v1"
                    class="w-full"
                  />
                  <small>Queen server API endpoint</small>
                </div>
              </div>
              
              <div class="setting-item">
                <label>Request Timeout (ms)</label>
                <div class="setting-control">
                  <InputNumber 
                    v-model="settings.api.timeout" 
                    :min="1000" 
                    :max="60000"
                    class="w-full"
                  />
                  <small>Maximum time to wait for API responses</small>
                </div>
              </div>
              
              <div class="setting-item">
                <label>Retry Failed Requests</label>
                <div class="setting-control">
                  <InputSwitch v-model="settings.api.retryEnabled" />
                  <small>Automatically retry failed API requests</small>
                </div>
              </div>
              
              <div class="setting-item" v-if="settings.api.retryEnabled">
                <label>Max Retries</label>
                <div class="setting-control">
                  <InputNumber 
                    v-model="settings.api.maxRetries" 
                    :min="1" 
                    :max="5"
                    class="w-full"
                  />
                  <small>Maximum number of retry attempts</small>
                </div>
              </div>
            </div>
          </TabPanel>
          
          <TabPanel header="Advanced">
            <div class="settings-section">
              <h3>Advanced Settings</h3>
              <div class="setting-item">
                <label>Debug Mode</label>
                <div class="setting-control">
                  <InputSwitch v-model="settings.advanced.debug" />
                  <small>Enable debug logging in console</small>
                </div>
              </div>
              
              <div class="setting-item">
                <label>WebSocket Reconnect</label>
                <div class="setting-control">
                  <InputSwitch v-model="settings.advanced.wsAutoReconnect" />
                  <small>Automatically reconnect WebSocket on disconnect</small>
                </div>
              </div>
              
              <div class="setting-item">
                <label>Cache Duration (minutes)</label>
                <div class="setting-control">
                  <InputNumber 
                    v-model="settings.advanced.cacheDuration" 
                    :min="1" 
                    :max="60"
                    class="w-full"
                  />
                  <small>How long to cache API responses</small>
                </div>
              </div>
              
              <div class="setting-item">
                <label>Export Data</label>
                <div class="setting-control">
                  <Button 
                    label="Export All Settings" 
                    icon="pi pi-download" 
                    class="p-button-outlined"
                    @click="exportSettings"
                  />
                  <small>Download all settings as JSON</small>
                </div>
              </div>
              
              <div class="setting-item">
                <label>Reset Settings</label>
                <div class="setting-control">
                  <Button 
                    label="Reset to Defaults" 
                    icon="pi pi-refresh" 
                    class="p-button-danger p-button-outlined"
                    @click="resetSettings"
                  />
                  <small>Restore all settings to default values</small>
                </div>
              </div>
            </div>
          </TabPanel>
        </TabView>
        
        <div class="actions-bar">
          <Button 
            label="Cancel" 
            class="p-button-text"
            @click="cancelChanges"
          />
          <Button 
            label="Save Changes" 
            icon="pi pi-save"
            @click="saveSettings"
          />
        </div>
      </div>
      
      <div class="col-12 lg:col-4">
        <Card>
          <template #title>About</template>
          <template #content>
            <div class="about-info">
              <div class="info-item">
                <i class="pi pi-crown text-4xl text-purple-400 mb-3"></i>
                <h3>Queen Dashboard</h3>
                <p>Version 1.0.0</p>
              </div>
              <Divider />
              <div class="info-item">
                <strong>Server Status</strong>
                <Tag :value="serverStatus" :severity="serverStatusSeverity" />
              </div>
              <div class="info-item">
                <strong>API Version</strong>
                <span>v1</span>
              </div>
              <div class="info-item">
                <strong>Database</strong>
                <span>PostgreSQL</span>
              </div>
              <Divider />
              <div class="info-item">
                <Button 
                  label="Documentation" 
                  icon="pi pi-book" 
                  class="p-button-text w-full"
                  @click="openDocs"
                />
              </div>
              <div class="info-item">
                <Button 
                  label="GitHub" 
                  icon="pi pi-github" 
                  class="p-button-text w-full"
                  @click="openGitHub"
                />
              </div>
            </div>
          </template>
        </Card>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useToast } from 'primevue/usetoast'
import { useConfirm } from 'primevue/useconfirm'
import Button from 'primevue/button'
import Card from 'primevue/card'
import TabView from 'primevue/tabview'
import TabPanel from 'primevue/tabpanel'
import InputText from 'primevue/inputtext'
import InputNumber from 'primevue/inputnumber'
import InputSwitch from 'primevue/inputswitch'
import Dropdown from 'primevue/dropdown'
import Divider from 'primevue/divider'
import Tag from 'primevue/tag'

const toast = useToast()
const confirm = useConfirm()

// State
const serverStatus = ref('Connected')
const serverStatusSeverity = ref('success')

const settings = ref({
  refreshInterval: 5,
  pageSize: 20,
  timezone: 'UTC',
  dateFormat: 'PPpp',
  notifications: {
    enabled: true,
    sound: false,
    email: '',
    thresholds: {
      queueDepth: 1000,
      errorRate: 10,
      processingTime: 5000
    }
  },
  api: {
    endpoint: 'http://localhost:6632/api/v1',
    timeout: 30000,
    retryEnabled: true,
    maxRetries: 3
  },
  advanced: {
    debug: false,
    wsAutoReconnect: true,
    cacheDuration: 5
  }
})

const originalSettings = ref(null)

const pageSizeOptions = [10, 20, 50, 100]

const timezoneOptions = [
  { label: 'UTC', value: 'UTC' },
  { label: 'America/New_York', value: 'America/New_York' },
  { label: 'America/Los_Angeles', value: 'America/Los_Angeles' },
  { label: 'Europe/London', value: 'Europe/London' },
  { label: 'Asia/Tokyo', value: 'Asia/Tokyo' }
]

const dateFormatOptions = [
  { label: 'Full (Jan 1, 2024, 12:00:00 PM)', value: 'PPpp' },
  { label: 'Short (01/01/2024 12:00)', value: 'MM/dd/yyyy HH:mm' },
  { label: 'ISO (2024-01-01T12:00:00)', value: "yyyy-MM-dd'T'HH:mm:ss" }
]

// Methods
const loadSettings = () => {
  // Load settings from localStorage or API
  const saved = localStorage.getItem('queenSettings')
  if (saved) {
    settings.value = JSON.parse(saved)
  }
  originalSettings.value = JSON.parse(JSON.stringify(settings.value))
}

const saveSettings = () => {
  localStorage.setItem('queenSettings', JSON.stringify(settings.value))
  originalSettings.value = JSON.parse(JSON.stringify(settings.value))
  
  toast.add({
    severity: 'success',
    summary: 'Success',
    detail: 'Settings saved successfully',
    life: 3000
  })
}

const cancelChanges = () => {
  settings.value = JSON.parse(JSON.stringify(originalSettings.value))
  toast.add({
    severity: 'info',
    summary: 'Changes Cancelled',
    detail: 'Settings reverted to last saved state',
    life: 2000
  })
}

const resetSettings = () => {
  confirm.require({
    message: 'Are you sure you want to reset all settings to default values?',
    header: 'Reset Settings',
    icon: 'pi pi-exclamation-triangle',
    acceptClass: 'p-button-danger',
    accept: () => {
      localStorage.removeItem('queenSettings')
      window.location.reload()
    }
  })
}

const exportSettings = () => {
  const data = JSON.stringify(settings.value, null, 2)
  const blob = new Blob([data], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = 'queen-settings.json'
  a.click()
  URL.revokeObjectURL(url)
  
  toast.add({
    severity: 'success',
    summary: 'Success',
    detail: 'Settings exported successfully',
    life: 3000
  })
}

const openDocs = () => {
  // Documentation not yet available
  toast.add({
    severity: 'info',
    summary: 'Coming Soon',
    detail: 'Documentation is being prepared',
    life: 3000
  })
}

const openGitHub = () => {
  // Repository not yet public
  toast.add({
    severity: 'info',
    summary: 'Coming Soon',
    detail: 'Repository will be made public soon',
    life: 3000
  })
}

// Lifecycle
onMounted(() => {
  loadSettings()
})
</script>

<style scoped>
.settings-page {
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

.settings-section {
  padding: 1rem;
}

.settings-section h3 {
  color: white;
  margin-bottom: 1.5rem;
}

.setting-item {
  margin-bottom: 2rem;
}

.setting-item label {
  display: block;
  font-weight: 500;
  color: white;
  margin-bottom: 0.5rem;
}

.setting-control {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.setting-control small {
  color: #a3a3a3;
  font-size: 0.875rem;
}

.threshold-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 1rem;
}

.threshold-item {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.threshold-item span {
  font-size: 0.875rem;
  color: #a3a3a3;
}

.actions-bar {
  display: flex;
  justify-content: flex-end;
  gap: 0.5rem;
  padding: 1rem;
  border-top: 1px solid var(--surface-300);
  margin-top: 2rem;
}

.about-info .info-item {
  margin-bottom: 1rem;
  text-align: center;
}

.about-info .info-item h3 {
  color: white;
  margin: 0.5rem 0;
}

.about-info .info-item p {
  color: #a3a3a3;
  margin: 0;
}

.about-info .info-item strong {
  color: #a3a3a3;
  margin-right: 0.5rem;
}

.text-4xl {
  font-size: 2.25rem;
}

.text-purple-400 {
  color: #a855f7;
}

.mb-3 {
  margin-bottom: 0.75rem;
}

.w-full {
  width: 100%;
}
</style>
