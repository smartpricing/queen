<template>
  <div class="view-container">

    <!-- Page head -->
    <!-- Stats -->
    <div class="grid-4" style="margin-bottom:20px;">
      <div class="stat">
        <div class="stat-label">Total Groups</div>
        <div class="stat-value font-mono">{{ formatNumber(consumers.length) }}</div>
      </div>
      <div class="stat">
        <div class="stat-label">Active Consumers</div>
        <div class="stat-value font-mono">{{ formatNumber(totalConsumers) }}</div>
      </div>
      <div class="stat">
        <div class="stat-label">Partitions Behind</div>
        <div class="stat-value font-mono num" :class="{ warn: totalPartitionsBehind > 0 && totalPartitionsBehind < 1000, bad: totalPartitionsBehind >= 1000 }">{{ formatNumber(totalPartitionsBehind) }}</div>
      </div>
      <div class="stat">
        <div class="stat-label">Lagging Groups</div>
        <div class="stat-value font-mono num" :class="{ bad: laggingGroups > 0 }">
          {{ laggingGroups }}
        </div>
      </div>
    </div>

    <!-- Lagging Partitions Section -->
    <div class="card" style="margin-bottom:20px;">
      <div class="card-header" style="justify-content:space-between;">
        <div style="display:flex; align-items:center; gap:10px;">
          <svg style="width:14px; height:14px; color:var(--warn-400);" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
          </svg>
          <h3>Lagging Partitions</h3>
          <span v-if="laggingPartitions.length > 0" class="chip chip-warn">
            {{ laggingPartitions.length }}
          </span>
        </div>
        <button
          @click="showLaggingSection = !showLaggingSection"
          class="btn btn-ghost"
        >
          {{ showLaggingSection ? 'Hide' : 'Show' }}
        </button>
      </div>

      <div v-if="showLaggingSection" class="card-body">
        <!-- Lag Threshold Selector -->
        <div style="margin-bottom:24px;">
          <div style="margin-bottom:12px;">
            <span class="label-xs">
              Minimum Lag Threshold:
              <span style="color:var(--warn-400); font-weight:600;">{{ getLagLabel(lagThreshold) }}</span>
            </span>
          </div>

          <div style="display:flex; flex-wrap:wrap; align-items:center; gap:12px;">
            <div class="seg">
              <button
                v-for="preset in lagPresets"
                :key="preset.value"
                @click="lagThreshold = preset.value; loadLaggingPartitions()"
                :class="{ on: lagThreshold === preset.value }"
              >
                {{ preset.label }}
              </button>
            </div>

            <button
              @click="loadLaggingPartitions"
              :disabled="laggingLoading"
              class="btn btn-primary" style="margin-left:auto;"
            >
              <svg style="width:14px; height:14px;" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
              </svg>
              {{ laggingLoading ? 'Loading...' : 'Refresh' }}
            </button>
          </div>
        </div>

        <!-- Loading state -->
        <div v-if="laggingLoading" style="display:flex; align-items:center; justify-content:center; padding:32px 0;">
          <span class="spinner" style="margin-right:12px;"></span>
          <span style="color:var(--text-mid);">Loading lagging partitions…</span>
        </div>

        <!-- Results Table -->
        <div v-else-if="laggingPartitions.length > 0" style="overflow-x:auto;">
          <table class="t">
            <thead>
              <tr>
                <th>Consumer Group</th>
                <th>Queue</th>
                <th>Partition</th>
                <th>Worker ID</th>
                <th style="text-align:right;">Offset Lag</th>
                <th style="text-align:right;">Time Lag</th>
                <th style="text-align:right;">Lag Hours</th>
                <th>Oldest Unconsumed</th>
                <th style="text-align:right;">Actions</th>
              </tr>
            </thead>
            <tbody>
              <tr
                v-for="partition in laggingPartitions"
                :key="`${partition.consumer_group}-${partition.queue_name}-${partition.partition_name}`"
              >
                <td style="font-weight:500; color:var(--text-hi);">
                  {{ partition.consumer_group }}
                </td>
                <td>
                  <span class="chip chip-ice">{{ partition.queue_name }}</span>
                </td>
                <td style="font-weight:500; color:var(--text-mid);">
                  {{ partition.partition_name }}
                </td>
                <td>
                  <code class="font-mono" style="font-size:12px; padding:2px 6px; border-radius:4px; background:rgba(255,255,255,.04); border:1px solid var(--bd); color:var(--text-mid);">
                    {{ partition.worker_id || 'N/A' }}
                  </code>
                </td>
                <td style="text-align:right;">
                  <span class="font-mono tabular-nums num warn" style="font-weight:500;">{{ formatNumber(partition.offset_lag) }}</span>
                </td>
                <td style="text-align:right;">
                  <span class="font-mono tabular-nums" style="font-weight:500;" :style="{ color: (partition.time_lag_seconds || 0) > 600 ? '#f43f5e' : 'var(--text-mid)' }">
                    {{ formatDuration(partition.time_lag_seconds * 1000) }}
                  </span>
                </td>
                <td style="text-align:right;">
                  <span class="font-mono tabular-nums" style="font-weight:600; color:var(--text-hi);">{{ partition.lag_hours }}h</span>
                </td>
                <td>
                  <span class="font-mono" style="font-size:12px; color:var(--text-mid);">
                    {{ formatTimestamp(partition.oldest_unconsumed_at) }}
                  </span>
                </td>
                <td style="text-align:right;">
                  <button
                    @click="handleSkipPartition(partition)"
                    class="btn btn-ghost" style="font-size:11px;"
                    :disabled="skippingPartition === `${partition.consumer_group}-${partition.queue_name}-${partition.partition_name}`"
                  >
                    <span v-if="skippingPartition === `${partition.consumer_group}-${partition.queue_name}-${partition.partition_name}`">Skipping…</span>
                    <span v-else>Skip to End</span>
                  </button>
                </td>
              </tr>
            </tbody>
          </table>
        </div>

        <!-- Empty state -->
        <div v-else style="text-align:center; padding:32px 0;">
          <svg style="width:48px; height:48px; margin:0 auto 12px; color:var(--ok-500);" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z" />
          </svg>
          <p style="font-weight:500; color:var(--text-hi);">No lagging partitions found</p>
          <p style="font-size:13px; color:var(--text-mid); margin-top:4px;">All partitions are within the {{ getLagLabel(lagThreshold) }} threshold</p>
        </div>
      </div>
    </div>

    <!-- Filter -->
    <div class="card" style="padding:14px 16px; margin-bottom:20px;">
      <div style="display:flex; flex-wrap:wrap; align-items:center; gap:14px;">
        <div style="flex:1; min-width:200px; max-width:400px; position:relative;">
          <input
            v-model="searchQuery"
            type="text"
            placeholder="Search consumer groups…"
            class="input"
            style="padding-left:36px;"
          />
          <svg
            style="position:absolute; left:10px; top:50%; transform:translateY(-50%); width:16px; height:16px; color:var(--text-low);"
            fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5"
          >
            <path stroke-linecap="round" stroke-linejoin="round" d="M21 21l-5.197-5.197m0 0A7.5 7.5 0 105.196 5.196a7.5 7.5 0 0010.607 10.607z" />
          </svg>
        </div>

        <label style="display:flex; align-items:center; gap:8px; font-size:13px; color:var(--text-mid); cursor:pointer;">
          <input
            v-model="showLaggingOnly"
            type="checkbox"
            style="width:16px; height:16px; accent-color:var(--accent);"
          />
          Show lagging only
        </label>

        <select v-model="sortBy" class="input" style="width:160px;">
          <option value="name">Sort by Name</option>
          <option value="lag">Sort by Lag</option>
          <option value="members">Sort by Members</option>
        </select>

        <button
          @click="handleHardRefresh"
          :disabled="hardRefreshLoading"
          class="btn btn-ghost" style="margin-left:auto;"
          title="Force refresh stats from database (fixes stale lag data)"
        >
          <svg
            style="width:14px; height:14px;"
            :class="{ 'animate-spin': hardRefreshLoading }"
            fill="none" stroke="currentColor" viewBox="0 0 24 24"
          >
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
          </svg>
          {{ hardRefreshLoading ? 'Refreshing…' : 'Hard Refresh' }}
        </button>
      </div>
    </div>

    <!-- Consumer Groups Table -->
    <div class="card" style="overflow:hidden;">
      <div style="overflow-x:auto;">
        <table class="t">
          <thead>
            <tr>
              <th>Group Name</th>
              <th>Status</th>
              <th>Queue</th>
              <th style="text-align:right;">Members</th>
              <th style="text-align:right;">Lag Parts</th>
              <th style="text-align:right;">Time Lag</th>
              <th style="text-align:center;">Actions</th>
            </tr>
          </thead>
          <tbody v-if="loading">
            <tr v-for="i in 5" :key="i">
              <td><div class="skeleton" style="height:16px; width:160px;"></div></td>
              <td><div class="skeleton" style="height:20px; width:64px;"></div></td>
              <td><div class="skeleton" style="height:16px; width:96px;"></div></td>
              <td><div class="skeleton" style="height:16px; width:32px; margin-left:auto;"></div></td>
              <td><div class="skeleton" style="height:16px; width:32px; margin-left:auto;"></div></td>
              <td><div class="skeleton" style="height:16px; width:64px; margin-left:auto;"></div></td>
              <td><div class="skeleton" style="height:16px; width:120px; margin:0 auto;"></div></td>
            </tr>
          </tbody>
          <tbody v-else-if="filteredConsumers.length > 0">
            <tr
              v-for="consumer in filteredConsumers"
              :key="`${consumer.name}-${consumer.queueName}`"
            >
              <td style="font-weight:500; color:var(--text-hi);">
                {{ consumer.name }}
              </td>
              <td>
                <span
                  style="display:inline-flex; align-items:center; gap:6px; font-size:11px; font-weight:500; padding:2px 8px; border-radius:999px;"
                  :style="consumer.state === 'Lagging'
                    ? { color: 'var(--ember-400)', background: 'rgba(244,113,133,0.10)', border: '1px solid rgba(244,113,133,0.22)' }
                    : consumer.state === 'Dead'
                      ? { color: 'var(--text-mid)', background: 'rgba(255,255,255,.04)', border: '1px solid var(--bd-hi)' }
                      : { color: '#4ade80', background: 'rgba(74,222,128,.1)', border: '1px solid rgba(74,222,128,.2)' }"
                >
                  <span
                    style="width:6px; height:6px; border-radius:99px; display:inline-block;"
                    :style="consumer.state === 'Lagging'
                      ? { background: '#f43f5e', animation: 'pulse-ring 1.8s ease-out infinite' }
                      : consumer.state === 'Dead'
                        ? { background: 'var(--text-low)' }
                        : { background: '#4ade80', boxShadow: 'none' }"
                  />
                  {{ getStatusText(consumer) }}
                </span>
              </td>
              <td>
                <span style="font-size:13px; color:var(--warn-400);">
                  {{ consumer.queueName || '-' }}
                </span>
              </td>
              <td style="text-align:right;">
                <span class="font-mono tabular-nums" style="font-weight:500;">
                  {{ consumer.members || 0 }}
                </span>
              </td>
              <td style="text-align:right;">
                <span
                  v-if="(consumer.partitionsWithLag || 0) > 0"
                  class="chip chip-warn"
                >
                  {{ consumer.partitionsWithLag }}
                </span>
                <span v-else style="font-size:13px; color:var(--text-low);">-</span>
              </td>
              <td style="text-align:right;">
                <span
                  class="font-mono tabular-nums"
                  style="font-size:13px; font-weight:500;"
                  :style="{ color: (consumer.maxTimeLag || 0) > 600 ? '#f43f5e' : 'var(--text-mid)' }"
                >
                  {{ (consumer.maxTimeLag || 0) > 0 ? formatDuration(consumer.maxTimeLag * 1000) : '-' }}
                </span>
              </td>
              <td>
                <div style="display:flex; align-items:center; justify-content:center; gap:4px;">
                  <button
                    @click="viewConsumer(consumer)"
                    class="btn btn-ghost btn-icon"
                    title="View details"
                  >
                    <svg style="width:16px; height:16px;" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
                      <path stroke-linecap="round" stroke-linejoin="round" d="M2.036 12.322a1.012 1.012 0 010-.639C3.423 7.51 7.36 4.5 12 4.5c4.638 0 8.573 3.007 9.963 7.178.07.207.07.431 0 .639C20.577 16.49 16.64 19.5 12 19.5c-4.638 0-8.573-3.007-9.963-7.178z" />
                      <path stroke-linecap="round" stroke-linejoin="round" d="M15 12a3 3 0 11-6 0 3 3 0 016 0z" />
                    </svg>
                  </button>
                  <button
                    v-if="consumer.queueName"
                    @click="handleMoveToNow(consumer)"
                    class="btn btn-ghost btn-icon"
                    title="Move to Now (skip pending)"
                  >
                    <svg style="width:16px; height:16px;" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
                      <path stroke-linecap="round" stroke-linejoin="round" d="M3 8.688c0-.864.933-1.405 1.683-.977l7.108 4.062a1.125 1.125 0 010 1.953l-7.108 4.062A1.125 1.125 0 013 16.81V8.688zM12.75 8.688c0-.864.933-1.405 1.683-.977l7.108 4.062a1.125 1.125 0 010 1.953l-7.108 4.062a1.125 1.125 0 01-1.683-.977V8.688z" />
                    </svg>
                  </button>
                  <button
                    v-if="consumer.queueName"
                    @click="openSeekModal(consumer)"
                    class="btn btn-ghost btn-icon"
                    title="Seek to timestamp"
                  >
                    <svg style="width:16px; height:16px;" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
                      <path stroke-linecap="round" stroke-linejoin="round" d="M12 6v6h4.5m4.5 0a9 9 0 11-18 0 9 9 0 0118 0z" />
                    </svg>
                  </button>
                  <button
                    @click="confirmDelete(consumer)"
                    class="btn btn-ghost btn-icon btn-danger"
                    title="Delete consumer group for this queue"
                  >
                    <svg style="width:16px; height:16px;" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
                      <path stroke-linecap="round" stroke-linejoin="round" d="M14.74 9l-.346 9m-4.788 0L9.26 9m9.968-3.21c.342.052.682.107 1.022.166m-1.022-.165L18.16 19.673a2.25 2.25 0 01-2.244 2.077H8.084a2.25 2.25 0 01-2.244-2.077L4.772 5.79m14.456 0a48.108 48.108 0 00-3.478-.397m-12 .562c.34-.059.68-.114 1.022-.165m0 0a48.11 48.11 0 013.478-.397m7.5 0v-.916c0-1.18-.91-2.164-2.09-2.201a51.964 51.964 0 00-3.32 0c-1.18.037-2.09 1.022-2.09 2.201v.916m7.5 0a48.667 48.667 0 00-7.5 0" />
                    </svg>
                  </button>
                </div>
              </td>
            </tr>
          </tbody>
          <tbody v-else>
            <tr>
              <td colspan="7" style="padding:48px 16px; text-align:center;">
                <svg style="width:48px; height:48px; margin:0 auto 12px; color:var(--text-low);" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M18 18.72a9.094 9.094 0 003.741-.479 3 3 0 00-4.682-2.72m.94 3.198l.001.031c0 .225-.012.447-.037.666A11.944 11.944 0 0112 21c-2.17 0-4.207-.576-5.963-1.584A6.062 6.062 0 016 18.719m12 0a5.971 5.971 0 00-.941-3.197m0 0A5.995 5.995 0 0012 12.75a5.995 5.995 0 00-5.058 2.772m0 0a3 3 0 00-4.681 2.72 8.986 8.986 0 003.74.477m.94-3.197a5.971 5.971 0 00-.94 3.197M15 6.75a3 3 0 11-6 0 3 3 0 016 0zm6 3a2.25 2.25 0 11-4.5 0 2.25 2.25 0 014.5 0zm-13.5 0a2.25 2.25 0 11-4.5 0 2.25 2.25 0 014.5 0z" />
                </svg>
                <h3 style="font-size:13px; font-weight:600; color:var(--text-hi); margin-bottom:4px;">No consumer groups found</h3>
                <p style="font-size:13px; color:var(--text-mid);">
                  Consumer groups will appear here when clients connect
                </p>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>

    <!-- Consumer detail modal -->
    <div
      v-if="selectedConsumer"
      style="position:fixed; inset:0; z-index:50; display:flex; align-items:center; justify-content:center; padding:16px; background:rgba(7,7,10,.6); backdrop-filter:blur(12px);"
      @click="selectedConsumer = null"
    >
      <div
        class="card" style="width:100%; max-width:672px; max-height:80vh; overflow:hidden;"
        @click.stop
      >
        <div class="card-header" style="justify-content:space-between;">
          <h3>{{ selectedConsumer.name }}</h3>
          <button @click="selectedConsumer = null" class="btn btn-ghost btn-icon">
            <svg style="width:18px; height:18px;" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
              <path stroke-linecap="round" stroke-linejoin="round" d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>
        <div class="card-body" style="overflow-y:auto; max-height:60vh;">
          <div class="grid-4" style="margin-bottom:24px;">
            <div class="stat" style="text-align:center; min-height:auto; padding:16px;">
              <div class="stat-label" style="justify-content:center;">Members</div>
              <div class="stat-value font-mono" style="font-size:24px;">
                {{ selectedConsumer.members || 0 }}
              </div>
            </div>
            <div class="stat" style="text-align:center; min-height:auto; padding:16px;">
              <div class="stat-label" style="justify-content:center;">State</div>
              <div
                style="font-size:18px; font-weight:600; margin-top:8px;"
                :style="getStatusText(selectedConsumer) === 'Stable'
                  ? { color: '#4ade80' }
                  : getStatusText(selectedConsumer) === 'Warning'
                    ? { color: 'var(--warn-400)' }
                    : getStatusText(selectedConsumer) === 'Lagging'
                      ? { color: '#f43f5e' }
                      : { color: 'var(--text-hi)' }"
              >
                {{ getStatusText(selectedConsumer) }}
              </div>
            </div>
            <div class="stat" style="text-align:center; min-height:auto; padding:16px;">
              <div class="stat-label" style="justify-content:center;">Lag Parts</div>
              <div class="stat-value font-mono" style="font-size:24px;" :style="{ color: (selectedConsumer.partitionsWithLag || 0) > 0 ? 'var(--warn-400)' : 'var(--text-hi)' }">
                {{ selectedConsumer.partitionsWithLag || 0 }}
              </div>
            </div>
            <div class="stat" style="text-align:center; min-height:auto; padding:16px;">
              <div class="stat-label" style="justify-content:center;">Time Lag</div>
              <div class="stat-value font-mono" style="font-size:24px; color:var(--text-hi);">
                {{ (selectedConsumer.maxTimeLag || 0) > 0 ? formatDuration(selectedConsumer.maxTimeLag * 1000) : '-' }}
              </div>
            </div>
          </div>

          <div style="margin-bottom:16px;">
            <span class="label-xs" style="display:block; margin-bottom:8px;">Queue</span>
            <div style="padding:12px; border-radius:10px; border:1px solid var(--bd); background:rgba(255,255,255,.02);">
              <span style="font-weight:500; color:var(--text-hi);">{{ selectedConsumer.queueName || '-' }}</span>
            </div>
          </div>

          <div v-if="selectedConsumer.topics?.length > 0">
            <span class="label-xs" style="display:block; margin-bottom:12px;">Topics</span>
            <div style="display:flex; flex-direction:column; gap:8px;">
              <div
                v-for="topic in selectedConsumer.topics"
                :key="topic"
                style="display:flex; align-items:center; justify-content:space-between; padding:12px; border-radius:10px; border:1px solid var(--bd); background:rgba(255,255,255,.02);"
              >
                <span style="font-weight:500; color:var(--text-hi);">{{ topic }}</span>
                <button
                  @click="seekQueue(selectedConsumer.name, topic)"
                  class="btn btn-ghost"
                >
                  Seek
                </button>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Delete confirmation modal -->
    <div
      v-if="consumerToDelete"
      style="position:fixed; inset:0; z-index:50; display:flex; align-items:center; justify-content:center; padding:16px; background:rgba(7,7,10,.6); backdrop-filter:blur(12px);"
      @click="consumerToDelete = null"
    >
      <div
        class="card" style="padding:24px; width:100%; max-width:448px;"
        @click.stop
      >
        <h3 style="font-size:18px; font-weight:600; color:var(--text-hi); margin-bottom:8px;">
          Delete Consumer Group
        </h3>
        <p style="color:var(--text-mid); margin-bottom:16px;">
          Are you sure you want to delete <strong>{{ consumerToDelete.name }}</strong>
          <span v-if="consumerToDelete.queueName"> for queue <strong>{{ consumerToDelete.queueName }}</strong></span>?
        </p>
        <p style="font-size:13px; color:var(--text-low); margin-bottom:24px;">
          This will remove partition consumer state{{ consumerToDelete.queueName ? ' for this queue only' : '' }}.
        </p>

        <label style="display:flex; align-items:center; gap:8px; font-size:13px; color:var(--text-mid); margin-bottom:24px; cursor:pointer;">
          <input
            v-model="deleteMetadata"
            type="checkbox"
            style="width:16px; height:16px; accent-color:var(--accent);"
          />
          Also delete subscription metadata
        </label>

        <div style="display:flex; align-items:center; justify-content:flex-end; gap:12px;">
          <button @click="consumerToDelete = null" class="btn btn-ghost">
            Cancel
          </button>
          <button @click="deleteConsumer" :disabled="actionLoading" class="btn btn-danger">
            {{ actionLoading ? 'Deleting…' : 'Delete' }}
          </button>
        </div>
      </div>
    </div>

    <!-- Seek Modal -->
    <div
      v-if="showSeekModal"
      style="position:fixed; inset:0; z-index:50; display:flex; align-items:center; justify-content:center; padding:16px; background:rgba(7,7,10,.6); backdrop-filter:blur(12px);"
      @click="showSeekModal = false"
    >
      <div
        class="card" style="padding:24px; width:100%; max-width:448px;"
        @click.stop
      >
        <h3 style="font-size:18px; font-weight:600; color:var(--text-hi); margin-bottom:8px;">
          Seek Cursor Position
        </h3>
        <p style="color:var(--text-mid); margin-bottom:16px;">
          {{ seekConsumer?.name }} / {{ seekConsumer?.queueName }}
        </p>

        <div style="margin-bottom:16px;">
          <span class="label-xs" style="display:block; margin-bottom:8px;">
            Target Timestamp
          </span>
          <input
            v-model="seekTimestamp"
            type="datetime-local"
            class="input"
          />
          <p style="font-size:12px; color:var(--text-low); margin-top:8px;">
            The cursor will move to the last message at or before this timestamp. Messages after this point will be re-consumed.
          </p>
        </div>

        <div style="display:flex; align-items:center; justify-content:flex-end; gap:12px;">
          <button @click="showSeekModal = false" class="btn btn-ghost">
            Cancel
          </button>
          <button
            @click="handleSeekToTimestamp"
            :disabled="actionLoading || !seekTimestamp"
            class="btn btn-primary"
          >
            {{ actionLoading ? 'Seeking…' : 'Seek to Timestamp' }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, watch } from 'vue'
import { useRoute } from 'vue-router'
import { consumers as consumersApi } from '@/api'
import { formatNumber, formatDuration } from '@/composables/useApi'
import { useRefresh } from '@/composables/useRefresh'

const route = useRoute()

// State
const consumers = ref([])
const loading = ref(true)
const searchQuery = ref('')
const showLaggingOnly = ref(false)
const sortBy = ref('name')

const selectedConsumer = ref(null)
const consumerToDelete = ref(null)
const deleteMetadata = ref(true)
const actionLoading = ref(false)

// Seek modal state
const showSeekModal = ref(false)
const seekConsumer = ref(null)
const seekTimestamp = ref('')

// Lagging partitions state
const showLaggingSection = ref(false)
const laggingPartitions = ref([])
const laggingLoading = ref(false)
const lagThreshold = ref(3600) // Default: 1 hour in seconds

// Skip partition state
const skippingPartition = ref(null)

// Hard refresh state
const hardRefreshLoading = ref(false)

const lagPresets = [
  { value: 60, label: '1m' },
  { value: 300, label: '5m' },
  { value: 600, label: '10m' },
  { value: 1800, label: '30m' },
  { value: 3600, label: '1h' },
  { value: 10800, label: '3h' },
  { value: 21600, label: '6h' },
  { value: 43200, label: '12h' },
  { value: 86400, label: '24h' },
]

// Helper to check if consumer is lagging
const isLagging = (consumer) => {
  // Use state field from API (Lagging, Stable, Dead)
  if (consumer.state === 'Lagging') return true
  // Fallback: check if maxTimeLag > 0 or partitionsWithLag > 0
  return (consumer.maxTimeLag || 0) > 0 || (consumer.partitionsWithLag || 0) > 0
}

// Computed
const filteredConsumers = computed(() => {
  let result = [...consumers.value]
  
  if (searchQuery.value) {
    const query = searchQuery.value.toLowerCase()
    result = result.filter(c => c.name.toLowerCase().includes(query))
  }
  
  if (showLaggingOnly.value) {
    result = result.filter(c => isLagging(c))
  }
  
  // Sort
  result.sort((a, b) => {
    if (sortBy.value === 'name') {
      return a.name.localeCompare(b.name)
    } else if (sortBy.value === 'lag') {
      // Sort by maxTimeLag (primary metric for lag detection)
      const aLag = (a.maxTimeLag || 0)
      const bLag = (b.maxTimeLag || 0)
      return bLag - aLag
    } else if (sortBy.value === 'members') {
      return (b.members || 0) - (a.members || 0)
    }
    return 0
  })
  
  return result
})

const totalConsumers = computed(() =>
  consumers.value.reduce((sum, c) => sum + (c.members || 0), 0)
)

const totalPartitionsBehind = computed(() =>
  consumers.value.reduce((sum, c) => sum + (c.partitionsWithLag || 0), 0)
)

const laggingGroups = computed(() =>
  consumers.value.filter(c => c.state === 'Lagging').length
)

// Methods
const getStatusText = (consumer) => {
  // Use API state directly
  return consumer.state || 'Unknown'
}

const getStatusClass = (consumer) => {
  const status = consumer.state
  if (status === 'Lagging') return 'bg-rose-100 dark:bg-rose-900/30 text-rose-700 dark:text-rose-400'
  if (status === 'Dead') return 'chip-mute'
  return 'bg-emerald-100 dark:bg-emerald-900/30 text-emerald-700 dark:text-emerald-400'
}

const getStatusDotClass = (consumer) => {
  const status = consumer.state
  if (status === 'Lagging') return 'bg-rose-500 animate-pulse'
  if (status === 'Dead') return 'opacity-40'
  return 'bg-emerald-500'
}

const fetchConsumers = async () => {
  // Only show loading skeleton if we don't have data yet (smooth background refresh)
  if (!consumers.value.length) loading.value = true
  try {
    const response = await consumersApi.list()
    // API returns array directly
    consumers.value = Array.isArray(response.data) ? response.data : response.data?.consumer_groups || []
  } catch (err) {
    console.error('Failed to fetch consumers:', err)
  } finally {
    loading.value = false
  }
}

// Hard refresh - force stats recomputation then fetch
const handleHardRefresh = async () => {
  hardRefreshLoading.value = true
  try {
    // First, trigger stats refresh on the server
    await consumersApi.refreshStats()
    // Then fetch the updated consumer groups
    await fetchConsumers()
  } catch (err) {
    console.error('Failed to hard refresh:', err)
  } finally {
    hardRefreshLoading.value = false
  }
}

const viewConsumer = (consumer) => {
  selectedConsumer.value = consumer
}

const confirmDelete = (consumer) => {
  consumerToDelete.value = consumer
}

const deleteConsumer = async () => {
  if (!consumerToDelete.value) return
  
  actionLoading.value = true
  try {
    if (consumerToDelete.value.queueName) {
      // Delete for specific queue
      await consumersApi.deleteForQueue(
        consumerToDelete.value.name, 
        consumerToDelete.value.queueName,
        deleteMetadata.value
      )
    } else {
      // Delete entire consumer group
      await consumersApi.delete(consumerToDelete.value.name, deleteMetadata.value)
    }
    consumerToDelete.value = null
    fetchConsumers()
  } catch (err) {
    console.error('Failed to delete consumer:', err)
  } finally {
    actionLoading.value = false
  }
}

const handleMoveToNow = async (consumer) => {
  if (!consumer.queueName) return
  
  if (!confirm(`Move cursor to now for "${consumer.name}" on queue "${consumer.queueName}"?\n\nThis will skip all pending messages and start consuming from the latest message.`)) {
    return
  }
  
  actionLoading.value = true
  try {
    await consumersApi.seek(consumer.name, consumer.queueName, { toEnd: true })
    fetchConsumers()
  } catch (err) {
    console.error('Failed to move to now:', err)
  } finally {
    actionLoading.value = false
  }
}

const handleSkipPartition = async (partition) => {
  const key = `${partition.consumer_group}-${partition.queue_name}-${partition.partition_name}`
  
  if (!confirm(`Skip to end for partition "${partition.partition_name}" on queue "${partition.queue_name}" (group: ${partition.consumer_group})?\n\nThis will advance the cursor to the latest message, skipping all pending messages on this partition.`)) {
    return
  }
  
  skippingPartition.value = key
  try {
    await consumersApi.seekPartition(partition.consumer_group, partition.queue_name, partition.partition_name)
    // Refresh lagging partitions list
    await loadLaggingPartitions()
  } catch (err) {
    console.error('Failed to skip partition:', err)
    alert('Failed to skip partition: ' + (err.response?.data?.error || err.message))
  } finally {
    skippingPartition.value = null
  }
}

const openSeekModal = (consumer) => {
  seekConsumer.value = consumer
  // Default to 1 hour ago
  const oneHourAgo = new Date(Date.now() - 60 * 60 * 1000)
  seekTimestamp.value = formatDateTimeLocal(oneHourAgo)
  showSeekModal.value = true
}

const handleSeekToTimestamp = async () => {
  if (!seekConsumer.value || !seekTimestamp.value) return
  
  actionLoading.value = true
  try {
    const isoTimestamp = new Date(seekTimestamp.value).toISOString()
    await consumersApi.seek(seekConsumer.value.name, seekConsumer.value.queueName, { timestamp: isoTimestamp })
    showSeekModal.value = false
    seekTimestamp.value = ''
    fetchConsumers()
  } catch (err) {
    console.error('Failed to seek:', err)
  } finally {
    actionLoading.value = false
  }
}

const formatDateTimeLocal = (date) => {
  const year = date.getFullYear()
  const month = String(date.getMonth() + 1).padStart(2, '0')
  const day = String(date.getDate()).padStart(2, '0')
  const hours = String(date.getHours()).padStart(2, '0')
  const minutes = String(date.getMinutes()).padStart(2, '0')
  return `${year}-${month}-${day}T${hours}:${minutes}`
}

// Lagging partitions methods
const loadLaggingPartitions = async () => {
  laggingLoading.value = true
  try {
    const response = await consumersApi.getLagging(lagThreshold.value)
    laggingPartitions.value = response.data || []
  } catch (err) {
    console.error('Failed to load lagging partitions:', err)
    laggingPartitions.value = []
  } finally {
    laggingLoading.value = false
  }
}

const getLagLabel = (seconds) => {
  if (seconds < 3600) {
    const minutes = Math.round(seconds / 60)
    return `${minutes} minute${minutes !== 1 ? 's' : ''}`
  } else if (seconds < 86400) {
    const hours = Math.round(seconds / 3600)
    return `${hours} hour${hours !== 1 ? 's' : ''}`
  } else {
    const days = Math.round(seconds / 86400)
    return `${days} day${days !== 1 ? 's' : ''}`
  }
}

const formatTimestamp = (timestamp) => {
  if (!timestamp) return 'N/A'
  try {
    const date = new Date(timestamp)
    return date.toLocaleString('en-US', { 
      month: 'short', 
      day: 'numeric',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit'
    })
  } catch (e) {
    return timestamp
  }
}

// Auto-load lagging partitions when section is shown
watch(showLaggingSection, (shown) => {
  if (shown && laggingPartitions.value.length === 0) {
    loadLaggingPartitions()
  }
})

// Register for global refresh
useRefresh(fetchConsumers)

onMounted(() => {
  // Read search query from URL if present
  if (route.query.search) {
    searchQuery.value = route.query.search
  }
  fetchConsumers()
})
</script>
