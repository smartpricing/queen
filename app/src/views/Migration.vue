<template>
  <div class="view-container">

    <!-- Page head -->
    <div style="display:flex; flex-direction:column; gap:20px;">

      <!-- Step 1: Maintenance Mode -->
      <div class="card">
        <div class="card-header">
          <h3>Step 1 · Maintenance Mode</h3>
          <span class="muted">recommended</span>
        </div>
        <div class="card-body">
          <p style="font-size:12px; color:var(--text-low); margin-bottom:14px;">Stop all push/pop operations to ensure a consistent snapshot.</p>
          <div style="display:flex; flex-wrap:wrap; align-items:center; gap:16px;">
            <!-- Push Maintenance -->
            <div style="display:flex; align-items:center; gap:10px;">
              <button
                @click="togglePushMaintenance"
                :disabled="maintenanceLoading"
                style="display:inline-flex; align-items:center; gap:8px; padding:7px 14px; font-size:13px; font-weight:500; border-radius:9px; cursor:pointer; transition:all .15s;"
                :style="pushMaintenanceMode
                  ? 'background:rgba(230,180,80,.12); color:var(--warn-400); border:1px solid rgba(230,180,80,.25);'
                  : 'background:rgba(255,255,255,.04); color:var(--text-mid); border:1px solid var(--bd-hi);'"
              >
                <span style="width:8px; height:8px; border-radius:99px;" :style="pushMaintenanceMode ? 'background:var(--warn-400);' : 'background:var(--text-low);'" />
                Push Maintenance {{ pushMaintenanceMode ? 'ON' : 'OFF' }}
              </button>
              <span v-if="pushMaintenanceMode && bufferedMessages > 0" style="font-size:12px; color:var(--warn-400);">
                {{ bufferedMessages }} buffered
              </span>
            </div>

            <!-- Pop Maintenance -->
            <div style="display:flex; align-items:center; gap:10px;">
              <button
                @click="togglePopMaintenance"
                :disabled="maintenanceLoading"
                style="display:inline-flex; align-items:center; gap:8px; padding:7px 14px; font-size:13px; font-weight:500; border-radius:9px; cursor:pointer; transition:all .15s;"
                :style="popMaintenanceMode
                  ? 'background:rgba(230,180,80,.12); color:var(--warn-400); border:1px solid rgba(230,180,80,.25);'
                  : 'background:rgba(255,255,255,.04); color:var(--text-mid); border:1px solid var(--bd-hi);'"
              >
                <span style="width:8px; height:8px; border-radius:99px;" :style="popMaintenanceMode ? 'background:var(--warn-400);' : 'background:var(--text-low);'" />
                Pop Maintenance {{ popMaintenanceMode ? 'ON' : 'OFF' }}
              </button>
            </div>

            <!-- Status indicator -->
            <div style="margin-left:auto; display:flex; align-items:center; gap:8px;">
              <span v-if="bothMaintenanceOn" style="display:flex; align-items:center; gap:6px; font-size:12px; font-weight:500; color:var(--ok-500);">
                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M9 12.75L11.25 15 15 9.75M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
                </svg>
                Ready to migrate
              </span>
              <span v-else style="display:flex; align-items:center; gap:6px; font-size:12px; font-weight:500; color:var(--warn-400);">
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
          <h3>Step 2 · Target Database</h3>
          <span class="muted">PostgreSQL</span>
        </div>
        <div class="card-body">
          <p style="font-size:12px; color:var(--text-low); margin-bottom:14px;">Configure the destination PostgreSQL database.</p>
          <div class="grid-3">
            <div>
              <label style="display:block; font-size:12px; font-weight:500; color:var(--text-low); margin-bottom:4px;">Host</label>
              <input
                v-model="targetConfig.host"
                type="text"
                placeholder="db.example.com"
                class="input"
                :disabled="isMigrating"
              />
            </div>
            <div>
              <label style="display:block; font-size:12px; font-weight:500; color:var(--text-low); margin-bottom:4px;">Port</label>
              <input
                v-model="targetConfig.port"
                type="text"
                placeholder="5432"
                class="input"
                :disabled="isMigrating"
              />
            </div>
            <div>
              <label style="display:block; font-size:12px; font-weight:500; color:var(--text-low); margin-bottom:4px;">Database</label>
              <input
                v-model="targetConfig.database"
                type="text"
                placeholder="postgres"
                class="input"
                :disabled="isMigrating"
              />
            </div>
            <div>
              <label style="display:block; font-size:12px; font-weight:500; color:var(--text-low); margin-bottom:4px;">User</label>
              <input
                v-model="targetConfig.user"
                type="text"
                placeholder="postgres"
                class="input"
                :disabled="isMigrating"
              />
            </div>
            <div>
              <label style="display:block; font-size:12px; font-weight:500; color:var(--text-low); margin-bottom:4px;">Password</label>
              <input
                v-model="targetConfig.password"
                type="password"
                placeholder="••••••••"
                class="input"
                :disabled="isMigrating"
              />
            </div>
            <div style="display:flex; align-items:flex-end;">
              <label style="display:flex; align-items:center; gap:8px; cursor:pointer; padding-bottom:8px;">
                <input
                  v-model="targetConfig.ssl"
                  type="checkbox"
                  style="accent-color:var(--warn-400);"
                  :disabled="isMigrating"
                />
                <span style="font-size:13px; color:var(--text-mid);">SSL</span>
              </label>
            </div>
          </div>

          <!-- Test Connection -->
          <div style="margin-top:16px; display:flex; align-items:center; gap:12px;">
            <button
              @click="testConnection"
              :disabled="!canTestConnection || testingConnection || isMigrating"
              class="btn btn-primary"
            >
              <svg v-if="testingConnection" class="w-4 h-4 animate-spin" style="margin-right:6px;" fill="none" viewBox="0 0 24 24">
                <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4" />
                <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
              </svg>
              {{ testingConnection ? 'Testing...' : 'Test Connection' }}
            </button>

            <span v-if="connectionResult" style="display:flex; align-items:center; gap:6px; font-size:13px;" :style="connectionResult.success ? 'color:var(--ok-500);' : 'color:var(--ember-500);'">
              <svg v-if="connectionResult.success" class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M9 12.75L11.25 15 15 9.75M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
              </svg>
              <svg v-else class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M9.75 9.75l4.5 4.5m0-4.5l-4.5 4.5M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
              </svg>
              {{ connectionResult.message }}
            </span>
          </div>
          <p v-if="connectionResult?.version" class="font-mono" style="margin-top:6px; font-size:12px; color:var(--text-low);">
            {{ connectionResult.version }}
          </p>
        </div>
      </div>

      <!-- Step 3: Select Data -->
      <div class="card">
        <div class="card-header">
          <h3>Step 3 · Select Data</h3>
          <span class="muted">choose what to migrate</span>
        </div>
        <div class="card-body">
          <p style="font-size:12px; color:var(--text-low); margin-bottom:14px;">Table structures are always created. Uncheck groups to skip their row data (faster migration, smaller transfer).</p>

          <!-- Core tables — always migrated -->
          <div style="padding:12px; background:rgba(255,255,255,.04); border:1px solid var(--bd); border-radius:10px; margin-bottom:16px;">
            <p style="font-size:12px; font-weight:600; color:var(--text-mid); margin-bottom:6px;">Always migrated (core operational state)</p>
            <p class="font-mono" style="font-size:12px; color:var(--text-low); line-height:1.6;">
              queues, partitions, partition_consumers, partition_lookup,<br/>
              consumer_groups_metadata, system_state, dead_letter_queue
            </p>
          </div>

          <!-- Optional groups -->
          <div style="display:flex; flex-direction:column; gap:8px;">
            <label
              v-for="group in tableGroups"
              :key="group.key"
              style="display:flex; align-items:flex-start; gap:12px; cursor:pointer; padding:8px; border-radius:10px; transition:background .15s;"
              :style="isMigrating ? 'opacity:0.5; pointer-events:none;' : ''"
            >
              <input
                v-model="group.include"
                type="checkbox"
                style="margin-top:2px; accent-color:var(--warn-400);"
              />
              <div style="flex:1; min-width:0;">
                <div style="display:flex; align-items:center; gap:8px;">
                  <span style="font-size:13px; font-weight:500; color:var(--text-hi);">{{ group.label }}</span>
                  <span v-if="!group.include" class="chip chip-warn">skipped — table created empty</span>
                </div>
                <p style="font-size:12px; color:var(--text-low); margin-top:2px;">{{ group.description }}</p>
                <p class="font-mono" style="font-size:12px; color:var(--text-faint); margin-top:2px;">{{ group.tables.join(', ') }}</p>
              </div>
            </label>
          </div>

          <div style="padding-top:8px; display:flex; justify-content:flex-end; gap:12px; align-items:center;">
            <button @click="selectAllGroups" :disabled="isMigrating" style="font-size:12px; color:var(--warn-400); cursor:pointer; border:none; background:none;" :style="isMigrating ? 'opacity:0.4;' : ''">Select all</button>
            <span style="font-size:12px; color:var(--text-faint);">|</span>
            <button @click="selectNoneGroups" :disabled="isMigrating" style="font-size:12px; color:var(--warn-400); cursor:pointer; border:none; background:none;" :style="isMigrating ? 'opacity:0.4;' : ''">Schema only</button>
          </div>
        </div>
      </div>

      <!-- Step 4: Start Migration -->
      <div class="card">
        <div class="card-header">
          <h3>Step 4 · Migrate</h3>
          <span class="muted">pg_dump → pg_restore</span>
          <button
            v-if="migrationStatus?.status === 'complete' || migrationStatus?.status === 'error'"
            @click="resetMigration"
            style="font-size:12px; color:var(--text-low); cursor:pointer; border:none; background:none; transition:color .15s;"
          >
            Reset
          </button>
        </div>
        <div class="card-body">
          <!-- Start button -->
          <div v-if="!isMigrating && migrationStatus?.status !== 'complete'" style="display:flex; align-items:center; gap:12px;">
            <button
              @click="startMigration"
              :disabled="!canStartMigration"
              class="btn"
              :class="canStartMigration ? 'btn-primary' : ''"
              :style="!canStartMigration ? 'opacity:0.4; cursor:not-allowed;' : ''"
              style="font-size:13px; padding:9px 24px; font-weight:600;"
            >
              Start Migration
            </button>
            <div v-if="!canStartMigration" style="font-size:12px; color:var(--text-low);">
              <span v-if="!connectionTested">Test the target connection first</span>
            </div>
            <div v-else-if="!bothMaintenanceOn" style="display:flex; align-items:center; gap:6px; font-size:12px; color:var(--warn-400);">
              <svg class="w-4 h-4" style="flex-shrink:0;" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M12 9v3.75m-9.303 3.376c-.866 1.5.217 3.374 1.948 3.374h14.71c1.73 0 2.813-1.874 1.948-3.374L13.949 3.378c-.866-1.5-3.032-1.5-3.898 0L2.697 16.126zM12 15.75h.007v.008H12v-.008z" />
              </svg>
              Maintenance modes not enabled — data written during migration may be lost
            </div>
          </div>

          <!-- Progress -->
          <div v-if="migrationStatus && migrationStatus.status !== 'idle'" style="display:flex; flex-direction:column; gap:16px;">
            <!-- Status bar -->
            <div style="display:flex; align-items:center; gap:12px;">
              <!-- Spinner for in-progress -->
              <svg v-if="isMigrating" class="w-5 h-5 animate-spin" style="color:var(--warn-400);" fill="none" viewBox="0 0 24 24">
                <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4" />
                <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
              </svg>
              <!-- Success icon -->
              <svg v-else-if="migrationStatus.status === 'complete'" class="w-5 h-5" style="color:var(--ok-500);" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M9 12.75L11.25 15 15 9.75M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
              </svg>
              <!-- Error icon -->
              <svg v-else-if="migrationStatus.status === 'error'" class="w-5 h-5" style="color:var(--ember-500);" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M12 9v3.75m9.303 3.376c-.866 1.5.217 3.374 1.948 3.374h14.71c1.73 0 2.813-1.874 1.948-3.374L13.949 3.378c-.866-1.5-3.032-1.5-3.898 0L2.697 16.126zM12 15.75h.007v.008H12v-.008z" />
              </svg>

              <div>
                <p style="font-size:13px; font-weight:500; color:var(--text-hi);">
                  {{ migrationStatus.currentStep || statusLabel }}
                </p>
                <p class="font-mono tabular-nums" style="font-size:12px; color:var(--text-low);">
                  {{ migrationStatus.elapsedSeconds }}s elapsed
                </p>
              </div>
            </div>

            <!-- Progress bar -->
            <div style="width:100%; height:8px; border-radius:99px; overflow:hidden; background:rgba(255,255,255,.06);">
              <div
                :class="progressBarClass"
                :style="{ width: progressPercent + '%', height: '100%', borderRadius: '99px', transition: 'width .5s' }"
              />
            </div>

            <!-- Error message -->
            <div v-if="migrationStatus.error" style="padding:12px; background:rgba(244,63,94,.08); border:1px solid rgba(244,63,94,.2); border-radius:10px;">
              <p class="font-mono" style="font-size:13px; color:var(--ember-400); white-space:pre-wrap; word-break:break-all;">{{ migrationStatus.error }}</p>
            </div>

            <!-- Output logs (collapsible) -->
            <details v-if="migrationStatus.dumpOutput || migrationStatus.restoreOutput" style="margin-top:4px;">
              <summary style="font-size:12px; color:var(--text-low); cursor:pointer;">
                Show command output
              </summary>
              <div style="margin-top:8px; display:flex; flex-direction:column; gap:8px;">
                <div v-if="migrationStatus.dumpOutput">
                  <p style="font-size:12px; font-weight:500; color:var(--text-low); margin-bottom:4px;">pg_dump output:</p>
                  <pre class="font-mono" style="font-size:12px; background:rgba(255,255,255,.04); padding:10px; border-radius:10px; overflow-x:auto; max-height:160px; color:var(--text-mid); border:1px solid var(--bd);">{{ migrationStatus.dumpOutput }}</pre>
                </div>
                <div v-if="migrationStatus.restoreOutput">
                  <p style="font-size:12px; font-weight:500; color:var(--text-low); margin-bottom:4px;">pg_restore output:</p>
                  <pre class="font-mono" style="font-size:12px; background:rgba(255,255,255,.04); padding:10px; border-radius:10px; overflow-x:auto; max-height:160px; color:var(--text-mid); border:1px solid var(--bd);">{{ migrationStatus.restoreOutput }}</pre>
                </div>
              </div>
            </details>
          </div>
        </div>
      </div>

      <!-- Step 5: Validate -->
      <div class="card">
        <div class="card-header">
          <h3>Step 5 · Validate</h3>
          <span class="muted">row count comparison</span>
        </div>
        <div class="card-body">
          <button
            @click="validateMigration"
            :disabled="!connectionTested || validating"
            class="btn btn-primary"
          >
            <svg v-if="validating" class="w-4 h-4 animate-spin" style="margin-right:6px;" fill="none" viewBox="0 0 24 24">
              <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4" />
              <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
            </svg>
            {{ validating ? 'Validating...' : 'Validate Row Counts' }}
          </button>

          <!-- Validation results -->
          <div v-if="validationResult" style="margin-top:16px;">
            <div v-if="validationResult.error" style="padding:12px; background:rgba(244,63,94,.08); border:1px solid rgba(244,63,94,.2); border-radius:10px;">
              <p style="font-size:13px; color:var(--ember-400);">{{ validationResult.error }}</p>
            </div>

            <div v-else>
              <div style="display:flex; align-items:center; gap:8px; margin-bottom:12px;">
                <span v-if="validationResult.allMatch" class="chip chip-ok" style="font-weight:500;">
                  <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                    <path stroke-linecap="round" stroke-linejoin="round" d="M9 12.75L11.25 15 15 9.75M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
                  </svg>
                  All tables match
                </span>
                <span v-else class="chip chip-bad" style="font-weight:500;">
                  <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                    <path stroke-linecap="round" stroke-linejoin="round" d="M12 9v3.75m-9.303 3.376c-.866 1.5.217 3.374 1.948 3.374h14.71c1.73 0 2.813-1.874 1.948-3.374L13.949 3.378c-.866-1.5-3.032-1.5-3.898 0L2.697 16.126zM12 15.75h.007v.008H12v-.008z" />
                  </svg>
                  Some tables have mismatched counts
                </span>
              </div>

              <div style="overflow-x:auto;">
                <table class="t">
                  <thead>
                    <tr>
                      <th>Table</th>
                      <th style="text-align:right;">Source</th>
                      <th style="text-align:right;">Target</th>
                      <th style="text-align:center;">Match</th>
                    </tr>
                  </thead>
                  <tbody>
                    <tr
                      v-for="row in validationResult.tables"
                      :key="row.table"
                    >
                      <td class="font-mono">{{ row.table }}</td>
                      <td class="font-mono tabular-nums" style="text-align:right;">{{ formatNumber(row.sourceCount) }}</td>
                      <td class="font-mono tabular-nums" style="text-align:right;">{{ formatNumber(row.targetCount) }}</td>
                      <td style="text-align:center;">
                        <svg v-if="row.match" class="w-4 h-4" style="color:var(--ok-500); margin:0 auto;" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                          <path stroke-linecap="round" stroke-linejoin="round" d="M4.5 12.75l6 6 9-13.5" />
                        </svg>
                        <svg v-else class="w-4 h-4" style="color:var(--ember-500); margin:0 auto;" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
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
          <h3>Step 6 · Switch Deployment</h3>
          <span class="muted">final steps</span>
        </div>
        <div class="card-body">
          <p style="font-size:13px; color:var(--text-mid); margin-bottom:12px;">After validation, redeploy Queen pointing to the new database:</p>
          <ol style="list-style:decimal; list-style-position:inside; font-size:13px; color:var(--text-mid); display:flex; flex-direction:column; gap:8px; padding-left:8px;">
            <li>Update the Kubernetes secret with new database credentials</li>
            <li>Update the Helm values file (<code class="kbd">db</code> field) to point to the new database</li>
            <li>Run <code class="kbd">./upgrade.sh &lt;env&gt; --no-build</code> to redeploy</li>
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
  if (!migrationStatus.value) return 'opacity-50'
  switch (migrationStatus.value.status) {
    case 'dumping': return 'bg-queen-500 animate-pulse'
    case 'restoring': return 'bg-queen-500 animate-pulse'
    case 'complete': return 'bg-emerald-500'
    case 'error': return 'bg-rose-500'
    default: return 'opacity-50'
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
