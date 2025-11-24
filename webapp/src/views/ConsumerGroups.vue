<template>
  <div class="page-professional">
    <div class="page-content">
      <div class="page-inner">
        <!-- Filters -->
        <div class="filter-card">
          <div class="flex flex-col sm:flex-row gap-3">
            <div class="flex-1">
              <input
                v-model="searchQuery"
                type="text"
                placeholder="Search consumer groups..."
                class="input"
              />
            </div>
            
            <div class="sm:w-48">
              <CustomSelect
                v-model="statusFilter"
                :options="statusOptions"
                placeholder="All States"
                :clearable="true"
                :searchable="false"
              />
            </div>
            
            <button
              v-if="searchQuery || statusFilter"
              @click="clearFilters"
              class="btn btn-secondary whitespace-nowrap"
            >
              Clear Filters
            </button>
          </div>
        </div>

        <!-- Lagging Partitions Filter -->
        <div class="chart-card">
          <div class="chart-header">
            <div class="flex items-center gap-2">
              <svg class="w-5 h-5 text-orange-600 dark:text-orange-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
              </svg>
              <h3 class="text-lg font-semibold text-gray-900 dark:text-white">Lagging Partitions</h3>
              <span v-if="laggingPartitions.length > 0" class="badge badge-warning text-xs">
                {{ laggingPartitions.length }}
              </span>
            </div>
            <button
              @click="toggleLaggingFilter"
              class="btn btn-secondary text-sm"
            >
              {{ showLaggingFilter ? 'Hide' : 'Show' }}
            </button>
          </div>

          <div v-if="showLaggingFilter" class="chart-body">
            <!-- Lag Threshold Selector -->
            <div class="mb-6">
              <div class="mb-3">
                <label class="text-sm font-medium text-gray-700 dark:text-gray-300">
                  Minimum Lag Threshold: <span class="text-purple-600 dark:text-purple-400 font-semibold">{{ getLagLabel(lagThreshold) }}</span>
                </label>
              </div>
              
              <div class="flex items-center justify-between gap-3">
                <div class="flex flex-wrap gap-2">
                <button 
                  @click="lagThreshold = 60; loadLaggingPartitions()" 
                  class="lag-threshold-btn"
                  :class="{ 'active': lagThreshold === 60 }"
                >
                  1m
                </button>
                <button 
                  @click="lagThreshold = 300; loadLaggingPartitions()" 
                  class="lag-threshold-btn"
                  :class="{ 'active': lagThreshold === 300 }"
                >
                  5m
                </button>
                <button 
                  @click="lagThreshold = 600; loadLaggingPartitions()" 
                  class="lag-threshold-btn"
                  :class="{ 'active': lagThreshold === 600 }"
                >
                  10m
                </button>
                <button 
                  @click="lagThreshold = 1800; loadLaggingPartitions()" 
                  class="lag-threshold-btn"
                  :class="{ 'active': lagThreshold === 1800 }"
                >
                  30m
                </button>
                <button 
                  @click="lagThreshold = 3600; loadLaggingPartitions()" 
                  class="lag-threshold-btn"
                  :class="{ 'active': lagThreshold === 3600 }"
                >
                  1h
                </button>
                <button 
                  @click="lagThreshold = 10800; loadLaggingPartitions()" 
                  class="lag-threshold-btn"
                  :class="{ 'active': lagThreshold === 10800 }"
                >
                  3h
                </button>
                <button 
                  @click="lagThreshold = 21600; loadLaggingPartitions()" 
                  class="lag-threshold-btn"
                  :class="{ 'active': lagThreshold === 21600 }"
                >
                  6h
                </button>
                <button 
                  @click="lagThreshold = 43200; loadLaggingPartitions()" 
                  class="lag-threshold-btn"
                  :class="{ 'active': lagThreshold === 43200 }"
                >
                  12h
                </button>
                <button 
                  @click="lagThreshold = 86400; loadLaggingPartitions()" 
                  class="lag-threshold-btn"
                  :class="{ 'active': lagThreshold === 86400 }"
                >
                  24h
                </button>
                </div>
                
                <button
                  @click="loadLaggingPartitions"
                  class="btn btn-primary text-xs whitespace-nowrap px-2.5 py-1"
                  :disabled="laggingLoading"
                >
                  <svg class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
                  </svg>
                  {{ laggingLoading ? 'Loading...' : 'Refresh' }}
                </button>
              </div>
            </div>

            <!-- Results Table -->
            <div v-if="laggingPartitions.length > 0 || laggingLoading" class="relative">
              <!-- Loading Overlay -->
              <div v-if="laggingLoading" class="absolute inset-0 bg-white/60 dark:bg-gray-900/60 backdrop-blur-sm z-10 flex items-center justify-center rounded-lg">
                <div class="flex items-center gap-2 bg-white dark:bg-gray-800 px-4 py-2 rounded-lg shadow-lg border border-gray-200 dark:border-gray-700">
                  <LoadingSpinner />
                  <span class="text-sm text-gray-700 dark:text-gray-300">Loading...</span>
                </div>
              </div>
              
              <div class="table-container scrollbar-thin">
              <table class="table">
                <thead>
                  <tr>
                    <th>Consumer Group</th>
                    <th>Queue</th>
                    <th>Partition</th>
                    <th>Worker ID</th>
                    <th class="text-right">Offset Lag</th>
                    <th class="text-right">Time Lag</th>
                    <th class="text-right">Lag Hours</th>
                    <th>Oldest Unconsumed</th>
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="partition in laggingPartitions" :key="`${partition.consumer_group}-${partition.queue_name}-${partition.partition_name}`">
                    <td>
                      <span class="font-medium text-gray-900 dark:text-gray-100">{{ partition.consumer_group }}</span>
                    </td>
                    <td>
                      <span class="badge badge-info text-xs">{{ partition.queue_name }}</span>
                    </td>
                    <td class="font-medium">{{ partition.partition_name }}</td>
                    <td>
                      <code class="worker-id">{{ partition.worker_id || 'N/A' }}</code>
                    </td>
                    <td class="text-right font-medium text-orange-600 dark:text-orange-400">
                      {{ formatNumber(partition.offset_lag) }}
                    </td>
                    <td class="text-right font-medium text-red-600 dark:text-red-400">
                      {{ formatDuration(partition.time_lag_seconds * 1000) }}
                    </td>
                    <td class="text-right font-semibold">
                      {{ partition.lag_hours }}h
                    </td>
                    <td class="text-sm">
                      {{ formatTimestamp(partition.oldest_unconsumed_at) }}
                    </td>
                  </tr>
                </tbody>
              </table>
              </div>
            </div>

            <!-- Empty State -->
            <div v-else-if="!laggingLoading" class="text-center py-12 text-gray-500 text-sm">
              <svg class="w-12 h-12 mx-auto mb-3 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z" />
              </svg>
              <p class="font-medium">No lagging partitions found</p>
              <p class="text-xs mt-2">All partitions are within the {{ getLagLabel(lagThreshold) }} threshold</p>
            </div>
          </div>
        </div>

        <LoadingSpinner v-if="loading && !consumerGroups.length" />

        <div v-else-if="error" class="error-card">
          <p><strong>Error loading consumer groups:</strong> {{ error }}</p>
        </div>

        <!-- Consumer Groups Stats -->
        <div class="metrics-grid">
          <div class="metric-card-compact">
            <span class="metric-label-sm">ACTIVE GROUPS</span>
            <div class="metric-value-sm text-gray-900 dark:text-gray-100">{{ stats.activeGroups }}</div>
          </div>
          <div class="metric-card-compact">
            <span class="metric-label-sm">TOTAL CONSUMERS</span>
            <div class="metric-value-sm text-gray-900 dark:text-gray-100">{{ stats.totalConsumers }}</div>
          </div>
          <div class="metric-card-compact">
            <span class="metric-label-sm">QUEUES MONITORED</span>
            <div class="metric-value-sm text-indigo-600 dark:text-indigo-400">{{ stats.totalTopics }}</div>
          </div>
          <div class="metric-card-compact">
            <span class="metric-label-sm">AVG LAG</span>
            <div class="metric-value-sm text-orange-600 dark:text-orange-400">{{ stats.avgLag }}</div>
          </div>
        </div>

        <!-- Consumer Groups Table -->
        <div class="chart-card">
          <div class="chart-body">
            <div class="table-container scrollbar-thin">
              <table class="table">
              <thead>
                <tr>
                  <th class="sortable-header" @click="sortGroups('name')">
                    <div class="sort-header-main">
                      Consumer Group
                      <span v-if="groupSortColumn === 'name'" class="sort-icon">
                        {{ groupSortDirection === 'asc' ? '↑' : '↓' }}
                      </span>
                    </div>
                  </th>
                  <th class="hidden md:table-cell sortable-header" @click="sortGroups('queueName')">
                    <div class="sort-header-main">
                      Queue
                      <span v-if="groupSortColumn === 'queueName'" class="sort-icon">
                        {{ groupSortDirection === 'asc' ? '↑' : '↓' }}
                      </span>
                    </div>
                  </th>
                  <th class="text-right sortable-header" @click="sortGroups('members')">
                    <div class="sort-header-main justify-end">
                      Members
                      <span v-if="groupSortColumn === 'members'" class="sort-icon">
                        {{ groupSortDirection === 'asc' ? '↑' : '↓' }}
                      </span>
                    </div>
                  </th>
                  <th class="text-right hidden lg:table-cell sortable-header" @click="sortGroups('totalLag')">
                    <div class="sort-header-main justify-end">
                      Offset Lag
                      <span v-if="groupSortColumn === 'totalLag'" class="sort-icon">
                        {{ groupSortDirection === 'asc' ? '↑' : '↓' }}
                      </span>
                    </div>
                  </th>
                  <th class="text-right sortable-header" @click="sortGroups('maxTimeLag')">
                    <div class="sort-header-main justify-end">
                      Time Lag
                      <span v-if="groupSortColumn === 'maxTimeLag'" class="sort-icon">
                        {{ groupSortDirection === 'asc' ? '↑' : '↓' }}
                      </span>
                    </div>
                  </th>
                  <th class="text-right sortable-header" @click="sortGroups('state')">
                    <div class="sort-header-main justify-end">
                      State
                      <span v-if="groupSortColumn === 'state'" class="sort-icon">
                        {{ groupSortDirection === 'asc' ? '↑' : '↓' }}
                      </span>
                    </div>
                  </th>
                </tr>
              </thead>
              <tbody>
                <tr 
                  v-for="group in filteredGroups" 
                  :key="`${group.name}-${group.queueName}`"
                  @click="selectGroup(group)"
                  class="cursor-pointer"
                >
                  <td>
                    <div class="flex items-center gap-2">
                      <div>
                        <div class="font-medium text-gray-900 dark:text-gray-100">{{ group.name }}</div>
                        <div class="flex items-center gap-2 md:hidden">
                          <span class="badge badge-info text-xs">{{ group.queueName }}</span>
                          <span 
                            v-if="group.subscriptionMode"
                            class="badge text-xs"
                            :class="{
                              'badge-success': group.subscriptionMode === 'new',
                              'badge-info': group.subscriptionMode === 'all',
                              'badge-warning': group.subscriptionMode === 'timestamp'
                            }"
                            :title="`Subscription mode: ${group.subscriptionMode}`"
                          >
                            {{ group.subscriptionMode }}
                          </span>
                        </div>
                      </div>
                      <span 
                        v-if="group.subscriptionMode"
                        class="badge text-xs hidden md:inline-flex"
                        :class="{
                          'badge-success': group.subscriptionMode === 'new',
                          'badge-info': group.subscriptionMode === 'all',
                          'badge-warning': group.subscriptionMode === 'timestamp'
                        }"
                        :title="`Subscription mode: ${group.subscriptionMode}`"
                      >
                        {{ group.subscriptionMode }}
                      </span>
                    </div>
                  </td>
                  <td class="hidden md:table-cell">
                    <span class="badge badge-info text-xs">
                      {{ group.queueName }}
                    </span>
                  </td>
                  <td class="text-right font-medium">{{ group.members }}</td>
                  <td class="text-right font-medium hidden lg:table-cell">
                    {{ formatNumber(group.totalLag) }}
                  </td>
                  <td class="text-right font-medium">
                    {{ group.maxTimeLag > 0 ? formatDuration(group.maxTimeLag * 1000) : '0ms' }}
                  </td>
                  <td class="text-right">
                    <StatusBadge :status="group.state" />
                  </td>
                </tr>
              </tbody>
            </table>
            
            <div v-if="!filteredGroups.length && !loading" class="text-center py-12 text-gray-500 text-sm">
              <svg class="w-12 h-12 mx-auto mb-3 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z" />
              </svg>
              <p>No consumer groups found</p>
              <p class="text-xs mt-2">Consumer groups are created automatically when consumers connect with a consumerGroup parameter.</p>
            </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Consumer Group Detail Modal -->
    <div v-if="selectedGroup" class="modal-overlay" @click="closeDetail">
      <div class="modal-content" @click.stop>
        <div class="modal-header">
          <div>
            <h2 class="text-2xl font-bold text-gray-900 dark:text-gray-100">
              {{ selectedGroup.name }}
            </h2>
            <p class="text-sm text-gray-500 dark:text-gray-400 mt-1">
              Consumer Group Details
              <span v-if="selectedGroup.queueName" class="text-purple-600 dark:text-purple-400 font-medium">
                • Queue: {{ selectedGroup.queueName }}
              </span>
            </p>
          </div>
          <div class="flex items-center gap-2">
            <button 
              v-if="selectedGroup.subscriptionMode"
              @click="openEditTimestamp"
              class="btn btn-secondary text-sm"
              title="Update subscription timestamp"
            >
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z" />
              </svg>
              Edit Timestamp
            </button>
            <button 
              @click="confirmDelete"
              class="btn btn-danger text-sm"
              title="Delete consumer group"
            >
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
              </svg>
              Delete
            </button>
            <button @click="closeDetail" class="close-btn">
              <svg class="w-6 h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
              </svg>
            </button>
          </div>
        </div>

        <div class="modal-body">
          <!-- Summary Stats -->
          <div class="grid grid-cols-2 lg:grid-cols-4 gap-3 mb-6">
            <div class="stat-card">
              <div class="stat-label">Partitions</div>
              <div class="stat-value">{{ selectedGroup.members }}</div>
            </div>
            <div class="stat-card">
              <div class="stat-label">Queue</div>
              <div class="stat-value text-base">{{ selectedGroup.queueName || 'All' }}</div>
            </div>
            <div class="stat-card">
              <div class="stat-label">Total Lag</div>
              <div class="stat-value">{{ formatNumber(selectedGroup.totalLag) }}</div>
            </div>
            <div class="stat-card">
              <div class="stat-label">Max Time Lag</div>
              <div class="stat-value">{{ selectedGroup.maxTimeLag > 0 ? formatDuration(selectedGroup.maxTimeLag * 1000) : '0ms' }}</div>
            </div>
          </div>

          <!-- Subscription Info -->
          <div v-if="selectedGroup.subscriptionMode" class="subscription-info-card">
            <div class="flex items-center gap-2 mb-3">
              <svg class="w-5 h-5 text-purple-600 dark:text-purple-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z" />
              </svg>
              <h3 class="text-sm font-semibold text-gray-900 dark:text-white">Subscription Info</h3>
            </div>
            <div class="grid grid-cols-1 md:grid-cols-3 gap-3">
              <div>
                <div class="text-xs text-gray-500 dark:text-gray-400 mb-1">Mode</div>
                <div class="flex items-center gap-2">
                  <span 
                    class="badge text-xs"
                    :class="{
                      'badge-success': selectedGroup.subscriptionMode === 'new',
                      'badge-info': selectedGroup.subscriptionMode === 'all',
                      'badge-warning': selectedGroup.subscriptionMode === 'timestamp'
                    }"
                  >
                    {{ selectedGroup.subscriptionMode.toUpperCase() }}
                  </span>
                </div>
              </div>
              <div>
                <div class="text-xs text-gray-500 dark:text-gray-400 mb-1">Subscribed At</div>
                <div class="text-sm font-mono text-gray-900 dark:text-gray-100">
                  {{ formatTimestamp(selectedGroup.subscriptionTimestamp) }}
                </div>
              </div>
              <div>
                <div class="text-xs text-gray-500 dark:text-gray-400 mb-1">Time Since Subscription</div>
                <div class="text-sm font-medium text-gray-900 dark:text-gray-100">
                  {{ formatDurationSince(selectedGroup.subscriptionCreatedAt) }}
                </div>
              </div>
            </div>
            <div class="mt-3 text-xs text-gray-600 dark:text-gray-400">
              <span v-if="selectedGroup.subscriptionMode === 'new'">
                ℹ️ Processing messages that arrived after {{ formatTimestamp(selectedGroup.subscriptionTimestamp) }}
              </span>
              <span v-else-if="selectedGroup.subscriptionMode === 'all'">
                ℹ️ Processing all messages from the beginning
              </span>
              <span v-else-if="selectedGroup.subscriptionMode === 'timestamp'">
                ℹ️ Processing messages from the specified timestamp onwards
              </span>
            </div>
          </div>

          <!-- Delete Options -->
          <div class="delete-options-card">
            <div class="flex items-center gap-2 mb-3">
              <svg class="w-5 h-5 text-red-600 dark:text-red-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
              </svg>
              <h3 class="text-sm font-semibold text-gray-900 dark:text-white">Danger Zone</h3>
            </div>
            <div class="flex items-center justify-between">
              <div>
                <p class="text-sm font-medium text-gray-900 dark:text-gray-100">Delete Consumer Group</p>
                <p class="text-xs text-gray-500 dark:text-gray-400 mt-1">
                  Permanently remove this consumer group and optionally its subscription metadata
                </p>
              </div>
              <label class="flex items-center gap-2 text-sm">
                <input
                  v-model="deleteMetadata"
                  type="checkbox"
                  class="rounded border-gray-300 dark:border-gray-600 text-purple-600 focus:ring-purple-500"
                />
                <span class="text-gray-700 dark:text-gray-300">Delete metadata too</span>
              </label>
            </div>
          </div>

          <!-- Loading Details -->
          <div v-if="detailsLoading" class="flex items-center justify-center py-12">
            <LoadingSpinner />
            <span class="ml-3 text-gray-500 dark:text-gray-400">Loading partition details...</span>
          </div>

          <!-- Per-Queue Details -->
          <div v-else-if="selectedGroupDetails" v-for="(queueData, queueName) in selectedGroupDetails" :key="queueName" class="queue-section">
            <h3 class="queue-title">
              <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 11H5m14 0a2 2 0 012 2v6a2 2 0 01-2 2H5a2 2 0 01-2-2v-6a2 2 0 012-2m14 0V9a2 2 0 00-2-2M5 11V9a2 2 0 012-2m0 0V5a2 2 0 012-2h6a2 2 0 012 2v2M7 7h10" />
              </svg>
              {{ queueName }}
              <span class="partition-count">{{ queueData.partitions.length }} partition{{ queueData.partitions.length !== 1 ? 's' : '' }}</span>
            </h3>

            <div class="partitions-table">
              <table class="detail-table">
                <thead>
                  <tr>
                    <th class="sortable" @click="sortPartitions('partition')">
                      <div class="sort-header">
                        Partition
                        <span v-if="sortColumn === 'partition'" class="sort-icon">
                          {{ sortDirection === 'asc' ? '↑' : '↓' }}
                        </span>
                      </div>
                    </th>
                    <th class="sortable" @click="sortPartitions('workerId')">
                      <div class="sort-header">
                        Worker ID
                        <span v-if="sortColumn === 'workerId'" class="sort-icon">
                          {{ sortDirection === 'asc' ? '↑' : '↓' }}
                        </span>
                      </div>
                    </th>
                    <th class="text-right sortable" @click="sortPartitions('totalConsumed')">
                      <div class="sort-header justify-end">
                        Consumed
                        <span v-if="sortColumn === 'totalConsumed'" class="sort-icon">
                          {{ sortDirection === 'asc' ? '↑' : '↓' }}
                        </span>
                      </div>
                    </th>
                    <th class="text-right sortable" @click="sortPartitions('offsetLag')">
                      <div class="sort-header justify-end">
                        Offset Lag
                        <span v-if="sortColumn === 'offsetLag'" class="sort-icon">
                          {{ sortDirection === 'asc' ? '↑' : '↓' }}
                        </span>
                      </div>
                    </th>
                    <th class="text-right sortable" @click="sortPartitions('timeLagSeconds')">
                      <div class="sort-header justify-end">
                        Time Lag
                        <span v-if="sortColumn === 'timeLagSeconds'" class="sort-icon">
                          {{ sortDirection === 'asc' ? '↑' : '↓' }}
                        </span>
                      </div>
                    </th>
                    <th class="text-center sortable" @click="sortPartitions('leaseActive')">
                      <div class="sort-header justify-center">
                        Lease
                        <span v-if="sortColumn === 'leaseActive'" class="sort-icon">
                          {{ sortDirection === 'asc' ? '↑' : '↓' }}
                        </span>
                      </div>
                    </th>
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="partition in getSortedPartitions(queueData.partitions)" :key="partition.partition">
                    <td class="font-medium">{{ partition.partition }}</td>
                    <td>
                      <code class="worker-id">{{ partition.workerId || 'N/A' }}</code>
                    </td>
                    <td class="text-right">{{ formatNumber(partition.totalConsumed) }}</td>
                    <td class="text-right font-medium" :class="partition.offsetLag > 0 ? 'text-orange-600 dark:text-orange-400' : 'text-green-600 dark:text-green-400'">
                      {{ formatNumber(partition.offsetLag) }}
                    </td>
                    <td class="text-right font-medium" :class="partition.timeLagSeconds > 300 ? 'text-red-600 dark:text-red-400' : 'text-gray-900 dark:text-gray-100'">
                      {{ partition.timeLagSeconds > 0 ? formatDuration(partition.timeLagSeconds * 1000) : '0ms' }}
                    </td>
                    <td class="text-center">
                      <span v-if="partition.leaseActive" class="badge badge-success text-xs">Active</span>
                      <span v-else class="badge badge-secondary text-xs">Inactive</span>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Edit Timestamp Modal -->
    <div v-if="showEditTimestamp" class="modal-overlay" @click="showEditTimestamp = false">
      <div class="modal-content-small" @click.stop>
        <div class="modal-header">
          <div>
            <h2 class="text-xl font-bold text-gray-900 dark:text-gray-100">
              Update Subscription Timestamp
            </h2>
            <p class="text-sm text-gray-500 dark:text-gray-400 mt-1">
              {{ selectedGroup.name }}
            </p>
          </div>
          <button @click="showEditTimestamp = false" class="close-btn">
            <svg class="w-6 h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>

        <div class="modal-body">
          <div class="mb-4">
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              New Subscription Timestamp
            </label>
            <DateTimePicker
              v-model="newTimestampISO"
              placeholder="Select subscription timestamp"
              :show-presets="true"
              :clearable="false"
            />
            <p class="text-xs text-gray-500 dark:text-gray-400 mt-2">
              ⚠️ Updating the timestamp will affect what messages are considered "new" for this consumer group.
              Messages with created_at > this timestamp will be consumed.
            </p>
          </div>

          <div class="flex gap-3 justify-end">
            <button
              @click="showEditTimestamp = false"
              class="btn btn-secondary"
              :disabled="actionLoading"
            >
              Cancel
            </button>
            <button
              @click="handleUpdateTimestamp"
              class="btn btn-primary"
              :disabled="actionLoading || !newTimestampISO"
            >
              <span v-if="actionLoading">Updating...</span>
              <span v-else>Update Timestamp</span>
            </button>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { consumersApi } from '../api/consumers';
import { formatNumber, formatDuration } from '../utils/formatters';

import CustomSelect from '../components/common/CustomSelect.vue';
import DateTimePicker from '../components/common/DateTimePicker.vue';
import StatusBadge from '../components/common/StatusBadge.vue';
import LoadingSpinner from '../components/common/LoadingSpinner.vue';

const loading = ref(false);
const error = ref(null);
const consumerGroups = ref([]);
const searchQuery = ref('');
const statusFilter = ref('');
const selectedGroup = ref(null);
const selectedGroupDetails = ref(null);
const detailsLoading = ref(false);
const showEditTimestamp = ref(false);
const newTimestamp = ref('');
const newTimestampISO = ref('');
const deleteMetadata = ref(true);
const actionLoading = ref(false);

const statusOptions = [
  { value: '', label: 'All States' },
  { value: 'Stable', label: 'Stable' },
  { value: 'Lagging', label: 'Lagging' },
  { value: 'Dead', label: 'Dead' }
];
const sortColumn = ref('partition');
const sortDirection = ref('asc');
const showLaggingFilter = ref(false);
const lagThreshold = ref(3600); // Default: 1 hour in seconds
const laggingPartitions = ref([]);
const laggingLoading = ref(false);
const groupSortColumn = ref('name');
const groupSortDirection = ref('asc');

const filteredGroups = computed(() => {
  let filtered = consumerGroups.value;
  
  if (searchQuery.value) {
    const search = searchQuery.value.toLowerCase();
    filtered = filtered.filter(g => 
      g.name.toLowerCase().includes(search) ||
      (g.queueName && g.queueName.toLowerCase().includes(search)) ||
      g.topics.some(t => t.toLowerCase().includes(search))
    );
  }
  
  if (statusFilter.value) {
    filtered = filtered.filter(g => g.state === statusFilter.value);
  }
  
  // Apply sorting
  filtered = [...filtered].sort((a, b) => {
    let aVal = a[groupSortColumn.value];
    let bVal = b[groupSortColumn.value];
    
    // Handle numeric columns
    if (['members', 'totalLag', 'maxTimeLag'].includes(groupSortColumn.value)) {
      aVal = Number(aVal) || 0;
      bVal = Number(bVal) || 0;
    }
    
    // Handle string columns (case-insensitive)
    if (['name', 'queueName', 'state'].includes(groupSortColumn.value)) {
      aVal = (aVal || '').toLowerCase();
      bVal = (bVal || '').toLowerCase();
    }
    
    // Compare values
    if (aVal < bVal) return groupSortDirection.value === 'asc' ? -1 : 1;
    if (aVal > bVal) return groupSortDirection.value === 'asc' ? 1 : -1;
    return 0;
  });
  
  return filtered;
});

const stats = computed(() => {
  const groups = consumerGroups.value;
  
  // Count unique consumer group names (since we now have one row per group+queue combo)
  const uniqueGroupNames = new Set(groups.map(g => g.name));
  const uniqueStableGroups = new Set(
    groups.filter(g => g.state === 'Stable').map(g => g.name)
  );
  
  return {
    activeGroups: uniqueStableGroups.size,
    totalConsumers: groups.reduce((sum, g) => sum + g.members, 0),
    totalTopics: new Set(groups.map(g => g.queueName)).size,
    avgLag: groups.length > 0 
      ? Math.round(groups.reduce((sum, g) => sum + g.totalLag, 0) / groups.length)
      : 0,
  };
});

function clearFilters() {
  searchQuery.value = '';
  statusFilter.value = '';
}

function sortGroups(column) {
  if (groupSortColumn.value === column) {
    groupSortDirection.value = groupSortDirection.value === 'asc' ? 'desc' : 'asc';
  } else {
    groupSortColumn.value = column;
    groupSortDirection.value = 'asc';
  }
}

async function selectGroup(group) {
  selectedGroup.value = group;
  selectedGroupDetails.value = null;
  detailsLoading.value = true;
  sortColumn.value = 'partition';
  sortDirection.value = 'asc';
  
  try {
    // Fetch detailed partition data
    const details = await consumersApi.getConsumerGroupDetails(group.name);
    
    // If a specific queue is selected (queueName exists), filter to show only that queue
    if (group.queueName && details) {
      const filteredDetails = {};
      if (details[group.queueName]) {
        filteredDetails[group.queueName] = details[group.queueName];
      }
      selectedGroupDetails.value = filteredDetails;
    } else {
      selectedGroupDetails.value = details;
    }
  } catch (err) {
    console.error('Error loading consumer group details:', err);
    alert(`Failed to load consumer group details: ${err.message}`);
    selectedGroup.value = null;
  } finally {
    detailsLoading.value = false;
  }
}

function closeDetail() {
  selectedGroup.value = null;
  selectedGroupDetails.value = null;
  sortColumn.value = 'partition';
  sortDirection.value = 'asc';
}

function sortPartitions(column) {
  if (sortColumn.value === column) {
    sortDirection.value = sortDirection.value === 'asc' ? 'desc' : 'asc';
  } else {
    sortColumn.value = column;
    sortDirection.value = 'asc';
  }
}

function getSortedPartitions(partitions) {
  if (!partitions || !Array.isArray(partitions)) return [];
  
  const sorted = [...partitions].sort((a, b) => {
    let aVal = a[sortColumn.value];
    let bVal = b[sortColumn.value];
    
    // Handle numeric columns
    if (['totalConsumed', 'offsetLag', 'timeLagSeconds'].includes(sortColumn.value)) {
      aVal = Number(aVal) || 0;
      bVal = Number(bVal) || 0;
    }
    
    // Handle boolean columns
    if (sortColumn.value === 'leaseActive') {
      aVal = aVal ? 1 : 0;
      bVal = bVal ? 1 : 0;
    }
    
    // Handle null/undefined values for string columns
    if (sortColumn.value === 'workerId') {
      aVal = aVal || '';
      bVal = bVal || '';
    }
    
    // Compare values
    if (aVal < bVal) return sortDirection.value === 'asc' ? -1 : 1;
    if (aVal > bVal) return sortDirection.value === 'asc' ? 1 : -1;
    return 0;
  });
  
  return sorted;
}

function formatTimestamp(timestamp) {
  if (!timestamp) return 'N/A';
  try {
    const date = new Date(timestamp);
    return date.toLocaleString('en-US', { 
      year: 'numeric', 
      month: 'short', 
      day: 'numeric',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit'
    });
  } catch (e) {
    return timestamp;
  }
}

function formatDurationSince(timestamp) {
  if (!timestamp) return 'N/A';
  try {
    const date = new Date(timestamp);
    const now = new Date();
    const milliseconds = now - date;
    return formatDuration(milliseconds);
  } catch (e) {
    return 'N/A';
  }
}

function confirmDelete() {
  const message = deleteMetadata.value
    ? `Delete consumer group "${selectedGroup.value.name}" and all its subscription metadata?\n\nThis will:\n- Remove all partition consumer state\n- Delete subscription metadata\n- Cannot be undone`
    : `Delete consumer group "${selectedGroup.value.name}" (keeping metadata)?\n\nThis will:\n- Remove all partition consumer state\n- Preserve subscription metadata for future use\n- Cannot be undone`;
  
  if (confirm(message)) {
    handleDelete();
  }
}

async function handleDelete() {
  if (!selectedGroup.value) return;
  
  actionLoading.value = true;
  try {
    await consumersApi.deleteConsumerGroup(selectedGroup.value.name, deleteMetadata.value);
    closeDetail();
    await loadData(); // Refresh the list
  } catch (err) {
    alert(`Failed to delete consumer group: ${err.message}`);
  } finally {
    actionLoading.value = false;
  }
}

async function handleUpdateTimestamp() {
  if (!selectedGroup.value || !newTimestampISO.value) return;
  
  actionLoading.value = true;
  try {
    // Convert ISO string to datetime-local format for the API
    const date = new Date(newTimestampISO.value);
    const year = date.getFullYear();
    const month = String(date.getMonth() + 1).padStart(2, '0');
    const day = String(date.getDate()).padStart(2, '0');
    const hours = String(date.getHours()).padStart(2, '0');
    const minutes = String(date.getMinutes()).padStart(2, '0');
    const seconds = String(date.getSeconds()).padStart(2, '0');
    const formattedTimestamp = `${year}-${month}-${day}T${hours}:${minutes}:${seconds}`;
    
    await consumersApi.updateSubscriptionTimestamp(selectedGroup.value.name, formattedTimestamp);
    showEditTimestamp.value = false;
    newTimestamp.value = '';
    newTimestampISO.value = '';
    await loadData(); // Refresh the list
  } catch (err) {
    alert(`Failed to update timestamp: ${err.message}`);
  } finally {
    actionLoading.value = false;
  }
}

function openEditTimestamp() {
  if (selectedGroup.value?.subscriptionTimestamp) {
    // Pre-fill with current timestamp in ISO format
    const timestamp = new Date(selectedGroup.value.subscriptionTimestamp).toISOString();
    newTimestamp.value = timestamp.slice(0, 19);
    newTimestampISO.value = timestamp;
  } else {
    // Default to now
    const now = new Date().toISOString();
    newTimestamp.value = now.slice(0, 19);
    newTimestampISO.value = now;
  }
  showEditTimestamp.value = true;
}

async function loadData() {
  loading.value = true;
  error.value = null;
  
  try {
    const groups = await consumersApi.getConsumerGroups();
    consumerGroups.value = groups;
  } catch (err) {
    error.value = err.message;
    console.error('Consumer groups error:', err);
  } finally {
    loading.value = false;
  }
}

async function loadLaggingPartitions() {
  laggingLoading.value = true;
  
  try {
    const result = await consumersApi.getLaggingPartitions(lagThreshold.value);
    laggingPartitions.value = result;
  } catch (err) {
    console.error('Error loading lagging partitions:', err);
    alert(`Failed to load lagging partitions: ${err.message}`);
  } finally {
    laggingLoading.value = false;
  }
}

function getLagLabel(seconds) {
  if (seconds < 3600) {
    const minutes = Math.round(seconds / 60);
    return `${minutes} minute${minutes !== 1 ? 's' : ''}`;
  } else if (seconds < 86400) {
    const hours = Math.round(seconds / 3600);
    return `${hours} hour${hours !== 1 ? 's' : ''}`;
  } else {
    const days = Math.round(seconds / 86400);
    return `${days} day${days !== 1 ? 's' : ''}`;
  }
}

function toggleLaggingFilter() {
  showLaggingFilter.value = !showLaggingFilter.value;
  if (showLaggingFilter.value && laggingPartitions.value.length === 0) {
    loadLaggingPartitions();
  }
}

onMounted(() => {
  loadData();
  
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/consumer-groups', loadData);
  }
});

onUnmounted(() => {
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/consumer-groups', null);
  }
});
</script>

<style scoped>
/* Styles inherited from professional.css */

.metrics-grid {
  @apply grid grid-cols-2 lg:grid-cols-4 gap-5;
}

/* Modal styles */
.modal-overlay {
  @apply fixed inset-0 bg-black/60 backdrop-blur-sm flex items-center justify-center z-50 p-4;
}

.modal-content {
  @apply bg-white dark:bg-[#161b22] border border-gray-200/40 dark:border-gray-800/40;
  @apply rounded-xl max-w-5xl w-full max-h-[90vh] flex flex-col;
  box-shadow: 0 20px 25px -5px rgba(0, 0, 0, 0.3), 0 10px 10px -5px rgba(0, 0, 0, 0.2);
}

.modal-content-small {
  @apply bg-white dark:bg-[#161b22] border border-gray-200/40 dark:border-gray-800/40;
  @apply rounded-xl max-w-lg w-full flex flex-col;
  box-shadow: 0 20px 25px -5px rgba(0, 0, 0, 0.3), 0 10px 10px -5px rgba(0, 0, 0, 0.2);
}

.modal-header {
  @apply flex justify-between items-start p-5 border-b border-gray-200/80 dark:border-gray-800/80;
}

.close-btn {
  @apply p-2 rounded-lg text-gray-500 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-800;
  transition: all 0.15s cubic-bezier(0.4, 0, 0.2, 1);
}

.modal-body {
  @apply p-5 overflow-y-auto flex-1;
}

.stat-card {
  @apply bg-orange-50/50 dark:bg-orange-900/10 border border-emerald-200/40 dark:border-orange-800/30;
  @apply rounded-lg p-4;
}

.stat-label {
  @apply text-xs font-semibold uppercase tracking-wider text-gray-500 dark:text-gray-400 mb-1;
  letter-spacing: 0.05em;
}

.stat-value {
  @apply text-xl font-bold text-gray-900 dark:text-gray-100 tracking-tight;
}

.subscription-info-card {
  @apply bg-purple-50/50 dark:bg-purple-900/10 border border-purple-200/40 dark:border-purple-800/30;
  @apply rounded-lg p-4 mb-6;
}

.delete-options-card {
  @apply bg-red-50/50 dark:bg-red-900/10 border border-red-200/40 dark:border-red-800/30;
  @apply rounded-lg p-4 mb-6;
}

.queue-section {
  @apply mb-4;
}

.queue-title {
  @apply flex items-center gap-2 text-base font-semibold text-gray-900 dark:text-white mb-3;
  @apply p-3 bg-gray-50/80 dark:bg-gray-800/30 rounded-lg border-l-2 border-orange-500;
}

.partition-count {
  @apply text-xs font-medium text-gray-500 dark:text-gray-400 ml-auto;
}

.partitions-table {
  @apply rounded-lg overflow-hidden border border-gray-200/40 dark:border-gray-800/40;
}

.detail-table {
  @apply w-full text-sm border-collapse;
}

.detail-table thead {
  @apply bg-gray-50 dark:bg-gray-800/30 border-b border-gray-200/80 dark:border-gray-800/80;
}

.detail-table th {
  @apply py-2.5 px-4 font-semibold text-left text-xs uppercase tracking-wider text-gray-500 dark:text-gray-400;
  letter-spacing: 0.05em;
}

.detail-table th.sortable {
  @apply cursor-pointer select-none;
  transition: background-color 0.15s ease;
}

.detail-table th.sortable:hover {
  @apply bg-gray-100/80 dark:bg-gray-700/50;
}

.sort-header {
  @apply flex items-center gap-1.5;
}

.sort-icon {
  @apply text-purple-600 dark:text-purple-400 font-bold text-sm;
}

.detail-table td {
  @apply py-2.5 px-4 border-b border-gray-100/60 dark:border-gray-800/40 text-gray-900 dark:text-gray-100;
}

.detail-table tbody tr:last-child td {
  @apply border-b-0;
}

.detail-table tbody tr:hover {
  @apply bg-gray-50/60 dark:bg-gray-800/20;
}

.worker-id {
  @apply bg-indigo-100 dark:bg-indigo-900/30 text-indigo-700 dark:text-indigo-300;
  @apply px-2 py-1 rounded text-xs font-mono;
}

.sortable-header {
  @apply cursor-pointer select-none;
  transition: background-color 0.15s ease;
}

.sortable-header:hover {
  @apply bg-gray-50 dark:bg-gray-800/30;
}

.sort-header-main {
  @apply flex items-center gap-1.5;
}

.lag-threshold-btn {
  @apply px-2.5 py-1 rounded text-xs font-medium;
  @apply bg-gray-100 dark:bg-gray-800 text-gray-700 dark:text-gray-300;
  @apply border border-gray-200 dark:border-gray-700;
  @apply hover:bg-gray-200 dark:hover:bg-gray-700;
  @apply transition-all duration-150;
}

.lag-threshold-btn.active {
  @apply bg-purple-600 dark:bg-purple-600 text-white;
  @apply border-purple-600 dark:border-purple-600;
  @apply hover:bg-purple-700 dark:hover:bg-purple-700;
}
</style>
