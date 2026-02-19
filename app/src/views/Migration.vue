<template>
  <div class="space-y-4 sm:space-y-6 animate-fade-in">

    <!-- Step 1: Maintenance Mode -->
    <div class="card">
      <div class="card-header">
        <h3 class="font-semibold text-light-900 dark:text-white">Step 1: Enable Maintenance Mode <span class="text-xs font-normal text-light-500">(recommended)</span></h3>
        <p class="text-xs text-light-500 mt-0.5">Stop all push/pop operations to ensure a consistent snapshot</p>
      </div>
      <div class="card-body">
        <div class="flex flex-wrap items-center gap-4">
          <!-- Push Maintenance -->
          <div class="flex items-center gap-3">
            <button
              @click="togglePushMaintenance"
              :disabled="maintenanceLoading"
              class="flex items-center gap-2 px-4 py-2 text-sm font-medium rounded-lg transition-colors"
              :class="pushMaintenanceMode
                ? 'bg-yellow-100 dark:bg-yellow-900/30 text-yellow-700 dark:text-yellow-400 border border-yellow-300 dark:border-yellow-700'
                : 'bg-light-100 dark:bg-dark-300 text-light-700 dark:text-light-300 hover:bg-light-200 dark:hover:bg-dark-200 border border-light-300 dark:border-dark-50'"
            >
              <span class="w-2 h-2 rounded-full" :class="pushMaintenanceMode ? 'bg-yellow-500' : 'bg-light-400'" />
              Push Maintenance {{ pushMaintenanceMode ? 'ON' : 'OFF' }}
            </button>
            <span v-if="pushMaintenanceMode && bufferedMessages > 0" class="text-xs text-yellow-600 dark:text-yellow-400">
              {{ bufferedMessages }} buffered
            </span>
          </div>

          <!-- Pop Maintenance -->
          <div class="flex items-center gap-3">
            <button
              @click="togglePopMaintenance"
              :disabled="maintenanceLoading"
              class="flex items-center gap-2 px-4 py-2 text-sm font-medium rounded-lg transition-colors"
              :class="popMaintenanceMode
                ? 'bg-orange-100 dark:bg-orange-900/30 text-orange-700 dark:text-orange-400 border border-orange-300 dark:border-orange-700'
                : 'bg-light-100 dark:bg-dark-300 text-light-700 dark:text-light-300 hover:bg-light-200 dark:hover:bg-dark-200 border border-light-300 dark:border-dark-50'"
            >
              <span class="w-2 h-2 rounded-full" :class="popMaintenanceMode ? 'bg-orange-500' : 'bg-light-400'" />
              Pop Maintenance {{ popMaintenanceMode ? 'ON' : 'OFF' }}
            </button>
          </div>

          <!-- Status indicator -->
          <div class="ml-auto flex items-center gap-2">
            <span v-if="bothMaintenanceOn" class="flex items-center gap-1.5 text-xs font-medium text-emerald-600 dark:text-emerald-400">
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M9 12.75L11.25 15 15 9.75M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
              </svg>
              Ready to migrate
            </span>
            <span v-else class="flex items-center gap-1.5 text-xs font-medium text-amber-600 dark:text-amber-400">
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M12 9v3.75m-9.303 3.376c-.866 1.5.217 3.374 1.948 3.374h14.71c1.73 0 2.813-1.874 1.948-3.374L13.949 3.378c-.866-1.5-3.032-1.5-3.898 0L2.697 16.126zM12 15.75h.007v.008H12v-.008z" />
              </svg>
              Enable both maintenance modes first
            </span>
          </div>
        </div>
      </div>
    </div>

    <!-- Step 2: Target Database Configuration -->
    <div class="card">
      <div class="card-header">
        <h3 class="font-semibold text-light-900 dark:text-white">Step 2: Target Database</h3>
        <p class="text-xs text-light-500 mt-0.5">Configure the destination PostgreSQL database</p>
      </div>
      <div class="card-body">
        <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-4">
          <div>
            <label class="block text-xs font-medium text-light-600 dark:text-light-400 mb-1">Host</label>
            <input
              v-model="targetConfig.host"
              type="text"
              placeholder="db.example.com"
              class="input w-full"
              :disabled="isMigrating"
            />
          </div>
          <div>
            <label class="block text-xs font-medium text-light-600 dark:text-light-400 mb-1">Port</label>
            <input
              v-model="targetConfig.port"
              type="text"
              placeholder="5432"
              class="input w-full"
              :disabled="isMigrating"
            />
          </div>
          <div>
            <label class="block text-xs font-medium text-light-600 dark:text-light-400 mb-1">Database</label>
            <input
              v-model="targetConfig.database"
              type="text"
              placeholder="postgres"
              class="input w-full"
              :disabled="isMigrating"
            />
          </div>
          <div>
            <label class="block text-xs font-medium text-light-600 dark:text-light-400 mb-1">User</label>
            <input
              v-model="targetConfig.user"
              type="text"
              placeholder="postgres"
              class="input w-full"
              :disabled="isMigrating"
            />
          </div>
          <div>
            <label class="block text-xs font-medium text-light-600 dark:text-light-400 mb-1">Password</label>
            <input
              v-model="targetConfig.password"
              type="password"
              placeholder="••••••••"
              class="input w-full"
              :disabled="isMigrating"
            />
          </div>
          <div class="flex items-end gap-4">
            <label class="flex items-center gap-2 cursor-pointer">
              <input
                v-model="targetConfig.ssl"
                type="checkbox"
                class="rounded border-light-300 dark:border-dark-50 text-queen-500 focus:ring-queen-500"
                :disabled="isMigrating"
              />
              <span class="text-sm text-light-700 dark:text-light-300">SSL</span>
            </label>
          </div>
        </div>

        <!-- Test Connection -->
        <div class="mt-4 flex items-center gap-3">
          <button
            @click="testConnection"
            :disabled="!canTestConnection || testingConnection || isMigrating"
            class="btn btn-primary text-sm"
          >
            <svg v-if="testingConnection" class="w-4 h-4 animate-spin mr-1.5" fill="none" viewBox="0 0 24 24">
              <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4" />
              <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
            </svg>
            {{ testingConnection ? 'Testing...' : 'Test Connection' }}
          </button>

          <span v-if="connectionResult" class="flex items-center gap-1.5 text-sm" :class="connectionResult.success ? 'text-emerald-600 dark:text-emerald-400' : 'text-rose-600 dark:text-rose-400'">
            <svg v-if="connectionResult.success" class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
              <path stroke-linecap="round" stroke-linejoin="round" d="M9 12.75L11.25 15 15 9.75M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
            </svg>
            <svg v-else class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
              <path stroke-linecap="round" stroke-linejoin="round" d="M9.75 9.75l4.5 4.5m0-4.5l-4.5 4.5M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
            </svg>
            {{ connectionResult.message }}
          </span>
        </div>
        <p v-if="connectionResult?.version" class="mt-1 text-xs text-light-500 font-mono">
          {{ connectionResult.version }}
        </p>
      </div>
    </div>

    <!-- Step 3: Select Data -->
    <div class="card">
      <div class="card-header">
        <h3 class="font-semibold text-light-900 dark:text-white">Step 3: Select Data to Migrate</h3>
        <p class="text-xs text-light-500 mt-0.5">Table structures are always created. Uncheck groups to skip their row data (faster migration, smaller transfer)</p>
      </div>
      <div class="card-body space-y-4">

        <!-- Core tables — always migrated -->
        <div class="p-3 bg-light-50 dark:bg-dark-300 rounded-lg">
          <p class="text-xs font-semibold text-light-700 dark:text-light-300 mb-1.5">Always migrated (core operational state)</p>
          <p class="text-xs font-mono text-light-500 leading-relaxed">
            queues, partitions, partition_consumers, partition_lookup,<br/>
            consumer_groups_metadata, system_state, dead_letter_queue
          </p>
        </div>

        <!-- Optional groups -->
        <div class="space-y-2">
          <label
            v-for="group in tableGroups"
            :key="group.key"
            class="flex items-start gap-3 cursor-pointer p-2 rounded-lg hover:bg-light-50 dark:hover:bg-dark-300 transition-colors"
            :class="isMigrating ? 'opacity-50 pointer-events-none' : ''"
          >
            <input
              v-model="group.include"
              type="checkbox"
              class="mt-0.5 rounded border-light-300 dark:border-dark-50 text-queen-500 focus:ring-queen-500"
            />
            <div class="flex-1 min-w-0">
              <div class="flex items-center gap-2">
                <span class="text-sm font-medium text-light-900 dark:text-white">{{ group.label }}</span>
                <span v-if="!group.include" class="text-xs text-amber-600 dark:text-amber-400 font-medium">skipped — table created empty</span>
              </div>
              <p class="text-xs text-light-500 mt-0.5">{{ group.description }}</p>
              <p class="text-xs font-mono text-light-400 mt-0.5">{{ group.tables.join(', ') }}</p>
            </div>
          </label>
        </div>

        <div class="pt-1 flex justify-end gap-3">
          <button @click="selectAllGroups" :disabled="isMigrating" class="text-xs text-queen-500 hover:text-queen-600 disabled:opacity-40">Select all</button>
          <span class="text-xs text-light-300">|</span>
          <button @click="selectNoneGroups" :disabled="isMigrating" class="text-xs text-queen-500 hover:text-queen-600 disabled:opacity-40">Schema only</button>
        </div>
      </div>
    </div>

    <!-- Step 4: Start Migration -->
    <div class="card">
      <div class="card-header flex items-center justify-between">
        <div>
          <h3 class="font-semibold text-light-900 dark:text-white">Step 4: Migrate</h3>
          <p class="text-xs text-light-500 mt-0.5">Stream pg_dump directly into pg_restore (no temp file)</p>
        </div>
        <button
          v-if="migrationStatus?.status === 'complete' || migrationStatus?.status === 'error'"
          @click="resetMigration"
          class="text-xs text-light-500 hover:text-light-700 dark:hover:text-light-300"
        >
          Reset
        </button>
      </div>
      <div class="card-body">
        <!-- Start button -->
        <div v-if="!isMigrating && migrationStatus?.status !== 'complete'" class="flex items-center gap-3">
          <button
            @click="startMigration"
            :disabled="!canStartMigration"
            class="btn text-sm px-6 py-2.5 font-semibold"
            :class="canStartMigration
              ? 'bg-queen-500 hover:bg-queen-600 text-white'
              : 'bg-light-200 dark:bg-dark-300 text-light-400 dark:text-light-600 cursor-not-allowed'"
          >
            Start Migration
          </button>
          <div v-if="!canStartMigration" class="text-xs text-light-500">
            <span v-if="!connectionTested">Test the target connection first</span>
          </div>
          <div v-else-if="!bothMaintenanceOn" class="flex items-center gap-1.5 text-xs text-amber-600 dark:text-amber-400">
            <svg class="w-4 h-4 flex-shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
              <path stroke-linecap="round" stroke-linejoin="round" d="M12 9v3.75m-9.303 3.376c-.866 1.5.217 3.374 1.948 3.374h14.71c1.73 0 2.813-1.874 1.948-3.374L13.949 3.378c-.866-1.5-3.032-1.5-3.898 0L2.697 16.126zM12 15.75h.007v.008H12v-.008z" />
            </svg>
            Maintenance modes not enabled — data written during migration may be lost
          </div>
        </div>

        <!-- Progress -->
        <div v-if="migrationStatus && migrationStatus.status !== 'idle'" class="space-y-4">
          <!-- Status bar -->
          <div class="flex items-center gap-3">
            <!-- Spinner for in-progress -->
            <svg v-if="isMigrating" class="w-5 h-5 animate-spin text-queen-500" fill="none" viewBox="0 0 24 24">
              <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4" />
              <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
            </svg>
            <!-- Success icon -->
            <svg v-else-if="migrationStatus.status === 'complete'" class="w-5 h-5 text-emerald-500" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
              <path stroke-linecap="round" stroke-linejoin="round" d="M9 12.75L11.25 15 15 9.75M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
            </svg>
            <!-- Error icon -->
            <svg v-else-if="migrationStatus.status === 'error'" class="w-5 h-5 text-rose-500" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
              <path stroke-linecap="round" stroke-linejoin="round" d="M12 9v3.75m9.303 3.376c-.866 1.5.217 3.374 1.948 3.374h14.71c1.73 0 2.813-1.874 1.948-3.374L13.949 3.378c-.866-1.5-3.032-1.5-3.898 0L2.697 16.126zM12 15.75h.007v.008H12v-.008z" />
            </svg>

            <div>
              <p class="text-sm font-medium text-light-900 dark:text-white">
                {{ migrationStatus.currentStep || statusLabel }}
              </p>
              <p class="text-xs text-light-500">
                {{ migrationStatus.elapsedSeconds }}s elapsed
              </p>
            </div>
          </div>

          <!-- Progress bar -->
          <div class="w-full h-2 bg-light-200 dark:bg-dark-200 rounded-full overflow-hidden">
            <div
              class="h-full rounded-full transition-all duration-500"
              :class="progressBarClass"
              :style="{ width: progressPercent + '%' }"
            />
          </div>

          <!-- Error message -->
          <div v-if="migrationStatus.error" class="p-3 bg-rose-50 dark:bg-rose-900/20 border border-rose-200 dark:border-rose-800 rounded-lg">
            <p class="text-sm text-rose-700 dark:text-rose-400 font-mono whitespace-pre-wrap break-all">{{ migrationStatus.error }}</p>
          </div>

          <!-- Output logs (collapsible) -->
          <details v-if="migrationStatus.dumpOutput || migrationStatus.restoreOutput" class="mt-2">
            <summary class="text-xs text-light-500 cursor-pointer hover:text-light-700 dark:hover:text-light-300">
              Show command output
            </summary>
            <div class="mt-2 space-y-2">
              <div v-if="migrationStatus.dumpOutput">
                <p class="text-xs font-medium text-light-600 dark:text-light-400 mb-1">pg_dump output:</p>
                <pre class="text-xs bg-light-100 dark:bg-dark-300 p-2 rounded-lg overflow-x-auto max-h-40 text-light-700 dark:text-light-300">{{ migrationStatus.dumpOutput }}</pre>
              </div>
              <div v-if="migrationStatus.restoreOutput">
                <p class="text-xs font-medium text-light-600 dark:text-light-400 mb-1">pg_restore output:</p>
                <pre class="text-xs bg-light-100 dark:bg-dark-300 p-2 rounded-lg overflow-x-auto max-h-40 text-light-700 dark:text-light-300">{{ migrationStatus.restoreOutput }}</pre>
              </div>
            </div>
          </details>
        </div>
      </div>
    </div>

    <!-- Step 5: Validate -->
    <div class="card">
      <div class="card-header">
        <h3 class="font-semibold text-light-900 dark:text-white">Step 5: Validate</h3>
        <p class="text-xs text-light-500 mt-0.5">Compare row counts between source and target databases</p>
      </div>
      <div class="card-body">
        <button
          @click="validateMigration"
          :disabled="!connectionTested || validating"
          class="btn btn-primary text-sm"
        >
          <svg v-if="validating" class="w-4 h-4 animate-spin mr-1.5" fill="none" viewBox="0 0 24 24">
            <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4" />
            <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
          </svg>
          {{ validating ? 'Validating...' : 'Validate Row Counts' }}
        </button>

        <!-- Validation results -->
        <div v-if="validationResult" class="mt-4">
          <div v-if="validationResult.error" class="p-3 bg-rose-50 dark:bg-rose-900/20 border border-rose-200 dark:border-rose-800 rounded-lg">
            <p class="text-sm text-rose-700 dark:text-rose-400">{{ validationResult.error }}</p>
          </div>

          <div v-else>
            <div class="flex items-center gap-2 mb-3">
              <span v-if="validationResult.allMatch" class="flex items-center gap-1.5 text-sm font-medium text-emerald-600 dark:text-emerald-400">
                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M9 12.75L11.25 15 15 9.75M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
                </svg>
                All tables match
              </span>
              <span v-else class="flex items-center gap-1.5 text-sm font-medium text-rose-600 dark:text-rose-400">
                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M12 9v3.75m-9.303 3.376c-.866 1.5.217 3.374 1.948 3.374h14.71c1.73 0 2.813-1.874 1.948-3.374L13.949 3.378c-.866-1.5-3.032-1.5-3.898 0L2.697 16.126zM12 15.75h.007v.008H12v-.008z" />
                </svg>
                Some tables have mismatched counts
              </span>
            </div>

            <div class="overflow-x-auto">
              <table class="w-full text-sm">
                <thead>
                  <tr class="border-b border-light-200 dark:border-dark-50">
                    <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Table</th>
                    <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Source</th>
                    <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Target</th>
                    <th class="text-center px-3 py-2 text-xs font-semibold text-light-500 uppercase">Match</th>
                  </tr>
                </thead>
                <tbody>
                  <tr
                    v-for="row in validationResult.tables"
                    :key="row.table"
                    class="border-b border-light-100 dark:border-dark-100"
                  >
                    <td class="px-3 py-2 font-mono text-sm">{{ row.table }}</td>
                    <td class="px-3 py-2 text-right tabular-nums">{{ formatNumber(row.sourceCount) }}</td>
                    <td class="px-3 py-2 text-right tabular-nums">{{ formatNumber(row.targetCount) }}</td>
                    <td class="px-3 py-2 text-center">
                      <svg v-if="row.match" class="w-4 h-4 text-emerald-500 mx-auto" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                        <path stroke-linecap="round" stroke-linejoin="round" d="M4.5 12.75l6 6 9-13.5" />
                      </svg>
                      <svg v-else class="w-4 h-4 text-rose-500 mx-auto" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                        <path stroke-linecap="round" stroke-linejoin="round" d="M6 18L18 6M6 6l12 12" />
                      </svg>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Step 6: Post-migration instructions -->
    <div class="card">
      <div class="card-header">
        <h3 class="font-semibold text-light-900 dark:text-white">Step 6: Switch Deployment</h3>
        <p class="text-xs text-light-500 mt-0.5">After validation, redeploy Queen pointing to the new database</p>
      </div>
      <div class="card-body">
        <div class="space-y-3 text-sm text-light-700 dark:text-light-300">
          <p>Once migration is validated, update the Kubernetes deployment to use the new database:</p>
          <ol class="list-decimal list-inside space-y-2 ml-2">
            <li>Update the Kubernetes secret with new database credentials</li>
            <li>Update the Helm values file (<code class="text-xs bg-light-100 dark:bg-dark-300 px-1.5 py-0.5 rounded font-mono">db</code> field) to point to the new database</li>
            <li>Run <code class="text-xs bg-light-100 dark:bg-dark-300 px-1.5 py-0.5 rounded font-mono">./upgrade.sh &lt;env&gt; --no-build</code> to redeploy</li>
            <li>Disable maintenance modes on the new deployment</li>
          </ol>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, onUnmounted } from 'vue'
import { system, migration } from '@/api'

// Maintenance state
const pushMaintenanceMode = ref(false)
const popMaintenanceMode = ref(false)
const bufferedMessages = ref(0)
const maintenanceLoading = ref(false)

// Target DB config
const targetConfig = reactive({
  host: '',
  port: '5432',
  database: '',
  user: '',
  password: '',
  ssl: false,
})

// Table data groups — uncheck to skip row data (DDL always migrated for all tables)
const tableGroups = reactive([
  {
    key: 'messages',
    label: 'Messages',
    description: 'Queue payloads. Skip to start with an empty queue on the new DB.',
    tables: ['messages', 'dead_letter_queue'],
    include: true,
  },
  {
    key: 'traces',
    label: 'Traces',
    description: 'Message trace debug data. No operational impact if skipped.',
    tables: ['message_traces', 'message_trace_names'],
    include: true,
  },
  {
    key: 'history',
    label: 'Consumption history',
    description: 'Audit log of consumed messages. Consumers work fine without it.',
    tables: ['messages_consumed'],
    include: true,
  },
  {
    key: 'metrics',
    label: 'Metrics & stats',
    description: 'Time-series metrics and stats. Starts accumulating fresh on the new DB.',
    tables: ['stats', 'stats_history', 'system_metrics', 'worker_metrics', 'worker_metrics_summary', 'queue_lag_metrics', 'retention_history'],
    include: true,
  },
])

const excludeTableData = computed(() =>
  tableGroups
    .filter(g => !g.include)
    .flatMap(g => g.tables)
)

const selectAllGroups = () => tableGroups.forEach(g => { g.include = true })
const selectNoneGroups = () => tableGroups.forEach(g => { g.include = false })

// Connection test
const testingConnection = ref(false)
const connectionResult = ref(null)
const connectionTested = computed(() => connectionResult.value?.success === true)

// Migration
const migrationStatus = ref(null)
const validating = ref(false)
const validationResult = ref(null)

let pollInterval = null

const bothMaintenanceOn = computed(() => pushMaintenanceMode.value && popMaintenanceMode.value)

const isMigrating = computed(() =>
  migrationStatus.value?.status === 'dumping' || migrationStatus.value?.status === 'restoring'
)

const canTestConnection = computed(() =>
  targetConfig.host && targetConfig.database && targetConfig.user
)

const canStartMigration = computed(() =>
  connectionTested.value && !isMigrating.value
)

const statusLabel = computed(() => {
  if (!migrationStatus.value) return ''
  switch (migrationStatus.value.status) {
    case 'dumping': return 'Preparing target schema...'
    case 'restoring': return 'Streaming pg_dump → pg_restore (no temp file)...'
    case 'complete': return 'Migration complete'
    case 'error': return 'Migration failed'
    default: return ''
  }
})

const progressPercent = computed(() => {
  if (!migrationStatus.value) return 0
  switch (migrationStatus.value.status) {
    case 'dumping': return 10   // schema prep only — completes in seconds
    case 'restoring': return 60 // streaming phase — bulk of the time
    case 'complete': return 100
    case 'error': return 100
    default: return 0
  }
})

const progressBarClass = computed(() => {
  if (!migrationStatus.value) return 'bg-light-300'
  switch (migrationStatus.value.status) {
    case 'dumping': return 'bg-queen-500 animate-pulse'
    case 'restoring': return 'bg-queen-500 animate-pulse'
    case 'complete': return 'bg-emerald-500'
    case 'error': return 'bg-rose-500'
    default: return 'bg-light-300'
  }
})

const formatNumber = (num) => {
  if (num === undefined || num === null || num < 0) return '-'
  return num.toLocaleString()
}

// Maintenance mode functions
const loadMaintenanceStatus = async () => {
  try {
    const response = await system.getMaintenance()
    pushMaintenanceMode.value = response.data.maintenanceMode || false
    popMaintenanceMode.value = response.data.popMaintenanceMode || false
    bufferedMessages.value = response.data.bufferedMessages || 0
  } catch (error) {
    console.error('Failed to load maintenance status:', error)
  }
}

const togglePushMaintenance = async () => {
  const enable = !pushMaintenanceMode.value
  if (enable) {
    if (!confirm('Enable PUSH maintenance mode?\n\nAll PUSH operations will be routed to file buffer.')) return
  } else {
    if (!confirm('Disable PUSH maintenance mode?\n\nFile buffer will drain to database.')) return
  }
  maintenanceLoading.value = true
  try {
    const response = await system.setMaintenance(enable)
    pushMaintenanceMode.value = response.data.maintenanceMode || false
    bufferedMessages.value = response.data.bufferedMessages || 0
  } catch (error) {
    alert('Failed to toggle push maintenance: ' + error.message)
  } finally {
    maintenanceLoading.value = false
  }
}

const togglePopMaintenance = async () => {
  const enable = !popMaintenanceMode.value
  if (enable) {
    if (!confirm('Enable POP maintenance mode?\n\nAll POP operations will return empty arrays.')) return
  } else {
    if (!confirm('Disable POP maintenance mode?\n\nConsumers will resume receiving messages.')) return
  }
  maintenanceLoading.value = true
  try {
    const response = await system.setPopMaintenance(enable)
    popMaintenanceMode.value = response.data.popMaintenanceMode || false
  } catch (error) {
    alert('Failed to toggle pop maintenance: ' + error.message)
  } finally {
    maintenanceLoading.value = false
  }
}

// Connection test
const testConnection = async () => {
  testingConnection.value = true
  connectionResult.value = null
  try {
    const response = await migration.testConnection(targetConfig)
    connectionResult.value = response.data
  } catch (error) {
    connectionResult.value = { success: false, message: error.message }
  } finally {
    testingConnection.value = false
  }
}

// Migration
const startMigration = async () => {
  const maintenanceWarning = !bothMaintenanceOn.value
    ? '\n\n⚠️  WARNING: Maintenance modes are NOT enabled.\nData written to the source during migration will NOT be copied to the target.'
    : ''

  const skipped = tableGroups.filter(g => !g.include).map(g => g.label)
  const skippedMsg = skipped.length
    ? `\n\nSkipping data for: ${skipped.join(', ')}`
    : ''

  if (!confirm(
    'Start database migration?\n\n' +
    'This will:\n' +
    '1. Drop and recreate the queen schema on the target\n' +
    '2. Stream pg_dump | pg_restore directly (no temp file)\n' +
    skippedMsg +
    maintenanceWarning
  )) return

  try {
    await migration.start({ ...targetConfig, excludeTableData: excludeTableData.value })
    startPolling()
  } catch (error) {
    alert('Failed to start migration: ' + error.message)
  }
}

const pollMigrationStatus = async () => {
  try {
    const response = await migration.getStatus()
    migrationStatus.value = response.data

    if (response.data.status === 'complete' || response.data.status === 'error') {
      stopPolling()
    }
  } catch (error) {
    console.error('Failed to poll migration status:', error)
  }
}

const startPolling = () => {
  stopPolling()
  pollMigrationStatus()
  pollInterval = setInterval(pollMigrationStatus, 2000)
}

const stopPolling = () => {
  if (pollInterval) {
    clearInterval(pollInterval)
    pollInterval = null
  }
}

const resetMigration = async () => {
  try {
    await migration.reset()
    migrationStatus.value = null
    validationResult.value = null
  } catch (error) {
    alert('Failed to reset: ' + error.message)
  }
}

// Validation
const validateMigration = async () => {
  validating.value = true
  validationResult.value = null
  try {
    const response = await migration.validate(targetConfig)
    validationResult.value = response.data
  } catch (error) {
    validationResult.value = { error: error.message }
  } finally {
    validating.value = false
  }
}

onMounted(async () => {
  await loadMaintenanceStatus()
  // Check if a migration is already running
  try {
    const response = await migration.getStatus()
    migrationStatus.value = response.data
    if (response.data.status === 'dumping' || response.data.status === 'restoring') {
      startPolling()
    }
  } catch {
    // Endpoint may not exist on older servers
  }
})

onUnmounted(() => {
  stopPolling()
})
</script>
