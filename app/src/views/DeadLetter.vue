<template>
  <div class="view-container">

    <!-- Summary stats -->
    <div class="grid-3" style="margin-bottom:24px;">
      <div class="stat">
        <div class="stat-label">Total DLQ messages</div>
        <div class="stat-value" style="color:var(--ember-400);">{{ dlqTotal }}</div>
      </div>
      <div class="stat">
        <div class="stat-label">Affected partitions</div>
        <div class="stat-value">{{ dlqPartitions }}</div>
      </div>
      <div class="stat">
        <div class="stat-label">Top error</div>
        <div style="margin-top:8px; font-size:13px; font-family:'JetBrains Mono',monospace; color:var(--ember-400); word-break:break-all; line-height:1.5;">
          {{ topError || 'None' }}
        </div>
      </div>
    </div>

    <!-- Top errors breakdown -->
    <div v-if="topErrors.length" class="card" style="margin-bottom:24px;">
      <div class="card-header">
        <h3>Top errors</h3>
        <span class="muted">{{ topErrors.length }} distinct</span>
      </div>
      <div class="card-body" style="display:flex; flex-direction:column; gap:12px;">
        <div v-for="(err, idx) in topErrors" :key="idx" style="display:flex; align-items:center; gap:12px;">
          <span class="font-mono" style="font-size:24px; font-weight:300; color:var(--ember-400); min-width:48px; text-align:right;">{{ err.count }}</span>
          <div style="flex:1; min-width:0;">
            <div style="font-size:13px; font-family:'JetBrains Mono',monospace; color:var(--text-hi); overflow:hidden; text-overflow:ellipsis; white-space:nowrap;">{{ err.error }}</div>
          </div>
          <div class="bar" style="width:120px; display:block;">
            <i style="background:linear-gradient(90deg, var(--ember-500), var(--ember-600));" :style="{ width: (err.count / maxErrorCount * 100) + '%' }" />
          </div>
        </div>
      </div>
    </div>

    <!-- Filters -->
    <div style="display:flex; align-items:center; gap:12px; margin-bottom:16px; flex-wrap:wrap;">
      <div>
        <label class="label-xs" style="display:block; margin-bottom:6px;">Queue</label>
        <select v-model="filterQueue" class="input" style="width:180px;">
          <option value="">All Queues</option>
          <option v-for="q in queueOptions" :key="q" :value="q">{{ q }}</option>
        </select>
      </div>
      <div>
        <label class="label-xs" style="display:block; margin-bottom:6px;">Consumer group</label>
        <select v-model="filterGroup" class="input" style="width:200px;">
          <option value="">All Groups</option>
          <option v-for="g in groupOptions" :key="g" :value="g">{{ g }}</option>
        </select>
      </div>
      <div style="margin-left:auto; align-self:flex-end;">
        <span style="font-size:12px; color:var(--text-low); font-family:'JetBrains Mono',monospace;">
          {{ filteredMessages.length }} of {{ messages.length }} messages
        </span>
      </div>
    </div>

    <!-- Messages table -->
    <div class="card">
      <div v-if="loading" style="padding:24px;">
        <div v-for="i in 6" :key="i" class="skeleton" style="height:44px; margin-bottom:8px; border-radius:6px;" />
      </div>

      <table v-else-if="filteredMessages.length" class="t">
        <thead>
          <tr>
            <th>Message</th>
            <th>Queue</th>
            <th>Consumer</th>
            <th>Error</th>
            <th>Retries</th>
            <th>Failed</th>
            <th style="text-align:right;">Actions</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="msg in filteredMessages"
            :key="msg.transactionId || msg.id"
            style="cursor:pointer;"
            :style="selectedMsg && msgKey(selectedMsg) === msgKey(msg) ? 'background:rgba(230,180,80,.06)' : ''"
            @click="selectMessage(msg)"
          >
            <td>
              <div style="display:flex; align-items:center; gap:6px;">
                <span class="pulse-ember" style="width:5px; height:5px;" />
                <span class="font-mono" style="font-size:12px;">{{ (msg.transactionId || msg.id || '-').slice(0, 14) }}…</span>
              </div>
            </td>
            <td style="font-weight:500;">{{ msg.queue || '-' }}</td>
            <td class="font-mono" style="font-size:12px; color:var(--text-mid);">{{ msg.consumerGroup || '-' }}</td>
            <td>
              <span class="font-mono" style="font-size:12px; color:var(--ember-400);">{{ truncateError(msg.errorMessage) }}</span>
            </td>
            <td class="font-mono" style="font-size:12px;">{{ msg.retryCount || 0 }}</td>
            <td class="font-mono" style="font-size:12px; color:var(--text-mid);">{{ formatAge(msg.failedAt) }}</td>
            <td style="text-align:right; white-space:nowrap;" @click.stop>
              <button class="btn btn-danger" style="padding:4px 10px; font-size:11px;" @click="deleteMessage(msg)" :disabled="msg._deleting">
                {{ msg._deleting ? '…' : 'Purge' }}
              </button>
            </td>
          </tr>
        </tbody>
      </table>

      <div v-else style="padding:48px; text-align:center; color:var(--text-low);">
        <svg style="width:40px; height:40px; margin:0 auto 12px; opacity:.4;" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.2"><path d="M5 7h14l-1.2 11.2a2 2 0 01-2 1.8H8.2a2 2 0 01-2-1.8L5 7Z"/><path d="M9 4h6v3H9z"/></svg>
        <p style="font-size:14px; font-weight:500;">Dead letter queue is empty</p>
        <p style="font-size:13px; margin-top:4px;">No failed messages to review.</p>
      </div>
    </div>

    <!-- Detail panel -->
    <Teleport to="body">
      <div v-if="selectedMsg" class="dlq-panel-backdrop" @click="selectedMsg = null" />
      <div v-if="selectedMsg" class="dlq-panel">
        <div style="padding:20px 24px;">
          <!-- Header -->
          <div style="display:flex; align-items:flex-start; justify-content:space-between; margin-bottom:20px; padding-bottom:16px; border-bottom:1px solid var(--bd);">
            <div style="flex:1; min-width:0;">
              <h3 style="font-size:15px; font-weight:700; margin-bottom:4px; color:var(--text-hi);">DLQ Message Detail</h3>
              <p class="font-mono" style="font-size:11px; color:var(--text-low); word-break:break-all;">
                {{ selectedMsg.transactionId || selectedMsg.id }}
              </p>
            </div>
            <button @click="selectedMsg = null" class="btn btn-ghost btn-icon">
              <svg style="width:18px; height:18px;" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12"/></svg>
            </button>
          </div>

          <!-- Status chip -->
          <div style="margin-bottom:20px;">
            <span class="chip chip-bad" style="font-size:12px;">dead_letter</span>
            <span v-if="selectedMsg.retryCount" class="chip chip-warn" style="font-size:12px; margin-left:6px;">
              {{ selectedMsg.retryCount }} retries
            </span>
          </div>

          <!-- Key-value fields -->
          <div class="dlq-kv">
            <div class="dlq-kv-row">
              <label class="label-xs">Queue</label>
              <span style="font-weight:500; color:var(--text-hi);">{{ selectedMsg.queue }}</span>
            </div>
            <div class="dlq-kv-row">
              <label class="label-xs">Partition</label>
              <span class="font-mono" style="font-size:12px; color:var(--text-mid); word-break:break-all;">{{ selectedMsg.partition }}</span>
            </div>
            <div class="dlq-kv-row">
              <label class="label-xs">Partition ID</label>
              <span class="font-mono" style="font-size:12px; color:var(--text-mid); word-break:break-all;">{{ selectedMsg.partitionId }}</span>
            </div>
            <div class="dlq-kv-row">
              <label class="label-xs">Transaction ID</label>
              <span class="font-mono" style="font-size:12px; color:var(--text-mid); word-break:break-all;">{{ selectedMsg.transactionId }}</span>
            </div>
            <div class="dlq-kv-row">
              <label class="label-xs">Consumer group</label>
              <span class="font-mono" style="font-size:12px; color:var(--ice-400);">{{ selectedMsg.consumerGroup }}</span>
            </div>
            <div class="dlq-kv-row">
              <label class="label-xs">Created</label>
              <span style="font-size:13px; color:var(--text-mid);">{{ formatDateTime(selectedMsg.createdAt) }}</span>
            </div>
            <div class="dlq-kv-row">
              <label class="label-xs">Failed at</label>
              <span style="font-size:13px; color:var(--text-mid);">{{ formatDateTime(selectedMsg.failedAt) }}</span>
            </div>
            <div v-if="selectedMsg.errorMessage" class="dlq-kv-row">
              <label class="label-xs">Error</label>
              <span class="font-mono" style="font-size:12px; color:var(--ember-400); word-break:break-all;">{{ selectedMsg.errorMessage }}</span>
            </div>
          </div>

          <!-- Payload -->
          <div v-if="selectedMsg.data" style="margin-top:24px;">
            <div style="display:flex; align-items:center; justify-content:space-between; margin-bottom:8px;">
              <label class="label-xs">Payload</label>
              <button class="btn btn-ghost" style="padding:2px 8px; font-size:11px;" @click="copyPayload">
                {{ copied ? 'Copied!' : 'Copy' }}
              </button>
            </div>
            <pre class="dlq-code">{{ JSON.stringify(selectedMsg.data, null, 2) }}</pre>
          </div>

          <!-- Actions -->
          <div style="display:flex; gap:8px; margin-top:24px; padding-top:16px; border-top:1px solid var(--bd);">
            <button class="btn btn-danger" style="flex:1;" @click="deleteMessage(selectedMsg)" :disabled="selectedMsg._deleting">
              {{ selectedMsg._deleting ? 'Purging…' : 'Purge message' }}
            </button>
          </div>
        </div>
      </div>
    </Teleport>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { dlq } from '@/api'
import { formatNumber } from '@/composables/useApi'
import { useRefresh } from '@/composables/useRefresh'

const messages = ref([])
const statusData = ref(null)
const loading = ref(true)
const selectedMsg = ref(null)
const copied = ref(false)
const filterQueue = ref('')
const filterGroup = ref('')

const dlqTotal = computed(() => statusData.value?.deadLetterQueue?.totalMessages || messages.value.length || 0)
const dlqPartitions = computed(() => statusData.value?.deadLetterQueue?.affectedPartitions || 0)
const topErrors = computed(() => statusData.value?.deadLetterQueue?.topErrors || [])
const topError = computed(() => topErrors.value[0]?.error || null)
const maxErrorCount = computed(() => Math.max(...topErrors.value.map(e => e.count), 1))

const queueOptions = computed(() => [...new Set(messages.value.map(m => m.queue).filter(Boolean))].sort())
const groupOptions = computed(() => [...new Set(messages.value.map(m => m.consumerGroup).filter(Boolean))].sort())

const filteredMessages = computed(() => {
  let result = messages.value
  if (filterQueue.value) result = result.filter(m => m.queue === filterQueue.value)
  if (filterGroup.value) result = result.filter(m => m.consumerGroup === filterGroup.value)
  return result
})

const msgKey = (msg) => msg.transactionId || msg.id

const truncateError = (err) => {
  if (!err || err === '-') return '-'
  return err.length > 50 ? err.slice(0, 50) + '…' : err
}

const formatAge = (ts) => {
  if (!ts) return '-'
  const diff = (Date.now() - new Date(ts).getTime()) / 1000
  if (diff < 60) return `${Math.round(diff)}s`
  if (diff < 3600) return `${Math.floor(diff / 60)}m`
  if (diff < 86400) {
    const h = Math.floor(diff / 3600)
    const m = Math.floor((diff % 3600) / 60)
    return m ? `${h}h ${m}m` : `${h}h`
  }
  return `${Math.floor(diff / 86400)}d`
}

const formatDateTime = (ts) => {
  if (!ts) return '-'
  return new Date(ts).toLocaleString('en-US', {
    year: 'numeric', month: 'short', day: 'numeric',
    hour: '2-digit', minute: '2-digit', second: '2-digit'
  })
}

const selectMessage = (msg) => {
  selectedMsg.value = selectedMsg.value && msgKey(selectedMsg.value) === msgKey(msg) ? null : msg
  copied.value = false
}

const copyPayload = async () => {
  if (!selectedMsg.value?.data) return
  try {
    await navigator.clipboard.writeText(JSON.stringify(selectedMsg.value.data, null, 2))
    copied.value = true
    setTimeout(() => { copied.value = false }, 2000)
  } catch {}
}

const fetchMessages = async () => {
  if (!messages.value.length) loading.value = true
  try {
    const res = await dlq.list()
    messages.value = (res.data?.messages || res.data || []).map(m => ({ ...m, _retrying: false, _deleting: false }))
  } catch (err) {
    console.error('Failed to fetch DLQ messages:', err)
  } finally {
    loading.value = false
  }
}

const fetchStatus = async () => {
  try {
    const res = await dlq.getStatus()
    statusData.value = res.data
  } catch {}
}

const fetchAll = async () => {
  await Promise.all([fetchMessages(), fetchStatus()])
}

const retryMessage = async (msg) => {
  msg._retrying = true
  try {
    await dlq.retry(msg.partitionId, msg.transactionId)
    if (selectedMsg.value && msgKey(selectedMsg.value) === msgKey(msg)) selectedMsg.value = null
    messages.value = messages.value.filter(m => msgKey(m) !== msgKey(msg))
    await fetchStatus()
  } catch (err) {
    alert(`Failed to retry: ${err.message}`)
  } finally {
    msg._retrying = false
  }
}

const deleteMessage = async (msg) => {
  if (!confirm(`Purge message ${msgKey(msg).slice(0, 16)}…?\n\nThis cannot be undone.`)) return
  msg._deleting = true
  try {
    await dlq.delete(msg.partitionId, msg.transactionId)
    if (selectedMsg.value && msgKey(selectedMsg.value) === msgKey(msg)) selectedMsg.value = null
    messages.value = messages.value.filter(m => msgKey(m) !== msgKey(msg))
    await fetchStatus()
  } catch (err) {
    alert(`Failed to delete: ${err.message}`)
  } finally {
    msg._deleting = false
  }
}


useRefresh(fetchAll)

let interval = null
onMounted(() => { fetchAll(); interval = setInterval(fetchAll, 30000) })
onUnmounted(() => { if (interval) clearInterval(interval) })
</script>

<style>

.dlq-panel-backdrop {
  position: fixed; inset: 0; z-index: 49;
  background: rgba(4,4,6,.4); backdrop-filter: blur(4px);
}

.dlq-panel {
  position: fixed; top: 0; right: 0; bottom: 0;
  width: 100%; max-width: 560px; z-index: 50;
  overflow-y: auto;
  border-left: 1px solid var(--bd);
  backdrop-filter: blur(16px); -webkit-backdrop-filter: blur(16px);
}
html:not(.light) .dlq-panel {
  background: linear-gradient(180deg, rgba(14,14,18,.97), rgba(10,10,14,.97));
}
html.light .dlq-panel {
  background: rgba(255,255,255,.97);
  box-shadow: -10px 0 40px -10px rgba(0,0,0,.1);
}

.dlq-kv { display: flex; flex-direction: column; gap: 16px; }
.dlq-kv-row { display: flex; flex-direction: column; gap: 4px; }

.dlq-code {
  font-family: 'JetBrains Mono', monospace;
  font-size: 12px; line-height: 1.6;
  padding: 14px 16px; border-radius: 10px;
  border: 1px solid var(--bd);
  color: var(--text-mid);
  white-space: pre; overflow-x: auto;
  max-height: 400px; overflow-y: auto;
}
html:not(.light) .dlq-code { background: var(--ink-0); }
html.light .dlq-code { background: var(--paper-1); }
</style>
