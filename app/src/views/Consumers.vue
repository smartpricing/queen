<template>
  <div class="space-y-4 sm:space-y-6 animate-fade-in">
    <!-- Stats -->
    <div class="grid grid-cols-2 md:grid-cols-4 gap-2 sm:gap-4">
      <div class="card p-3 sm:p-4">
        <p class="text-[10px] sm:text-xs text-light-500 uppercase tracking-wide">Total Groups</p>
        <p class="text-xl sm:text-2xl font-bold font-display text-light-900 dark:text-white mt-1">
          {{ formatNumber(consumers.length) }}
        </p>
      </div>
      <div class="card p-3 sm:p-4">
        <p class="text-[10px] sm:text-xs text-light-500 uppercase tracking-wide">Active Consumers</p>
        <p class="text-xl sm:text-2xl font-bold font-display text-cyber-600 dark:text-cyber-400 mt-1">
          {{ formatNumber(totalConsumers) }}
        </p>
      </div>
      <div class="card p-3 sm:p-4">
        <p class="text-[10px] sm:text-xs text-light-500 uppercase tracking-wide">Total Lag</p>
        <p class="text-xl sm:text-2xl font-bold font-display text-crown-600 dark:text-crown-400 mt-1">
          {{ formatNumber(totalLag) }}
        </p>
      </div>
      <div class="card p-3 sm:p-4">
        <p class="text-[10px] sm:text-xs text-light-500 uppercase tracking-wide">Lagging Groups</p>
        <p class="text-xl sm:text-2xl font-bold font-display mt-1" :class="laggingGroups > 0 ? 'text-rose-600 dark:text-rose-400' : 'text-emerald-600 dark:text-emerald-400'">
          {{ laggingGroups }}
        </p>
      </div>
    </div>

    <!-- Lagging Partitions Section -->
    <div class="card">
      <div class="card-header flex flex-col sm:flex-row sm:items-center sm:justify-between gap-2">
        <div class="flex items-center gap-2 sm:gap-3">
          <svg class="w-4 h-4 sm:w-5 sm:h-5 text-amber-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
          </svg>
          <h3 class="text-sm sm:text-base font-semibold text-light-900 dark:text-white">Lagging Partitions</h3>
          <span v-if="laggingPartitions.length > 0" class="badge badge-warning text-[10px] sm:text-xs">
            {{ laggingPartitions.length }}
          </span>
        </div>
        <button 
          @click="showLaggingSection = !showLaggingSection"
          class="btn btn-secondary text-xs sm:text-sm w-full sm:w-auto"
        >
          {{ showLaggingSection ? 'Hide' : 'Show' }}
        </button>
      </div>
      
      <div v-if="showLaggingSection" class="card-body">
        <!-- Lag Threshold Selector -->
        <div class="mb-6">
          <div class="mb-3">
            <label class="text-sm font-medium text-light-700 dark:text-light-300">
              Minimum Lag Threshold: 
              <span class="text-queen-600 dark:text-queen-400 font-semibold">{{ getLagLabel(lagThreshold) }}</span>
            </label>
          </div>
          
          <div class="flex flex-wrap items-center gap-3">
            <div class="flex flex-wrap gap-2">
              <button 
                v-for="preset in lagPresets" 
                :key="preset.value"
                @click="lagThreshold = preset.value; loadLaggingPartitions()"
                class="px-3 py-1.5 text-xs font-medium rounded-lg transition-colors"
                :class="lagThreshold === preset.value 
                  ? 'bg-queen-600 text-white' 
                  : 'bg-light-100 dark:bg-dark-300 text-light-700 dark:text-light-300 hover:bg-light-200 dark:hover:bg-dark-200'"
              >
                {{ preset.label }}
              </button>
            </div>
            
            <button 
              @click="loadLaggingPartitions"
              :disabled="laggingLoading"
              class="btn btn-cyber text-xs ml-auto"
            >
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
              </svg>
              {{ laggingLoading ? 'Loading...' : 'Refresh' }}
            </button>
          </div>
        </div>
        
        <!-- Loading state -->
        <div v-if="laggingLoading" class="flex items-center justify-center py-8">
          <div class="w-6 h-6 border-2 border-queen-500 border-t-transparent rounded-full animate-spin mr-3"></div>
          <span class="text-light-500">Loading lagging partitions...</span>
        </div>
        
        <!-- Results Table -->
        <div v-else-if="laggingPartitions.length > 0" class="overflow-x-auto">
          <table class="w-full text-sm">
            <thead>
              <tr class="border-b border-light-200 dark:border-dark-50">
                <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Consumer Group</th>
                <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Queue</th>
                <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Partition</th>
                <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Worker ID</th>
                <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Offset Lag</th>
                <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Time Lag</th>
                <th class="text-right px-3 py-2 text-xs font-semibold text-light-500 uppercase">Lag Hours</th>
                <th class="text-left px-3 py-2 text-xs font-semibold text-light-500 uppercase">Oldest Unconsumed</th>
              </tr>
            </thead>
            <tbody>
              <tr 
                v-for="partition in laggingPartitions" 
                :key="`${partition.consumer_group}-${partition.queue_name}-${partition.partition_name}`"
                class="border-b border-light-100 dark:border-dark-100 hover:bg-light-50 dark:hover:bg-dark-300"
              >
                <td class="px-3 py-2 font-medium text-light-900 dark:text-white">
                  {{ partition.consumer_group }}
                </td>
                <td class="px-3 py-2">
                  <span class="badge badge-queen text-xs">{{ partition.queue_name }}</span>
                </td>
                <td class="px-3 py-2 font-medium text-light-700 dark:text-light-300">
                  {{ partition.partition_name }}
                </td>
                <td class="px-3 py-2">
                  <code class="text-xs px-2 py-0.5 rounded bg-light-200 dark:bg-dark-200 text-light-700 dark:text-light-300">
                    {{ partition.worker_id || 'N/A' }}
                  </code>
                </td>
                <td class="px-3 py-2 text-right font-medium text-amber-600 dark:text-amber-400">
                  {{ formatNumber(partition.offset_lag) }}
                </td>
                <td class="px-3 py-2 text-right font-medium text-rose-600 dark:text-rose-400">
                  {{ formatDuration(partition.time_lag_seconds * 1000) }}
                </td>
                <td class="px-3 py-2 text-right font-semibold text-light-900 dark:text-white">
                  {{ partition.lag_hours }}h
                </td>
                <td class="px-3 py-2 text-xs text-light-600 dark:text-light-400">
                  {{ formatTimestamp(partition.oldest_unconsumed_at) }}
                </td>
              </tr>
            </tbody>
          </table>
        </div>
        
        <!-- Empty state -->
        <div v-else class="text-center py-8">
          <svg class="w-12 h-12 mx-auto text-emerald-500 mb-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z" />
          </svg>
          <p class="font-medium text-light-900 dark:text-white">No lagging partitions found</p>
          <p class="text-sm text-light-500 mt-1">All partitions are within the {{ getLagLabel(lagThreshold) }} threshold</p>
        </div>
      </div>
    </div>

    <!-- Filter -->
    <div class="card p-3 sm:p-4">
      <div class="flex flex-col sm:flex-row sm:flex-wrap sm:items-center gap-3 sm:gap-4">
        <div class="w-full sm:flex-1 sm:min-w-[200px] sm:max-w-md">
          <div class="relative">
            <input
              v-model="searchQuery"
              type="text"
              placeholder="Search consumer groups..."
              class="input pl-10"
            />
            <svg 
              class="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-light-500"
              fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5"
            >
              <path stroke-linecap="round" stroke-linejoin="round" d="M21 21l-5.197-5.197m0 0A7.5 7.5 0 105.196 5.196a7.5 7.5 0 0010.607 10.607z" />
            </svg>
          </div>
        </div>
        
        <div class="flex items-center justify-between sm:justify-start gap-3 sm:gap-4">
          <label class="flex items-center gap-2 text-xs sm:text-sm text-light-700 dark:text-light-300">
            <input
              v-model="showLaggingOnly"
              type="checkbox"
              class="w-4 h-4 rounded border-light-300 dark:border-dark-50 text-queen-500 focus:ring-queen-500"
            />
            <span class="hidden sm:inline">Show lagging only</span>
            <span class="sm:hidden">Lagging only</span>
          </label>
          
          <select v-model="sortBy" class="select w-32 sm:w-40 text-sm">
            <option value="name">Sort by Name</option>
            <option value="lag">Sort by Lag</option>
            <option value="members">Sort by Members</option>
          </select>
          
          <button
            @click="handleHardRefresh"
            :disabled="hardRefreshLoading"
            class="btn btn-secondary text-xs sm:text-sm flex items-center gap-1.5"
            title="Force refresh stats from database (fixes stale lag data)"
          >
            <svg 
              class="w-4 h-4" 
              :class="{ 'animate-spin': hardRefreshLoading }"
              fill="none" stroke="currentColor" viewBox="0 0 24 24"
            >
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
            </svg>
            <span class="hidden sm:inline">{{ hardRefreshLoading ? 'Refreshing...' : 'Hard Refresh' }}</span>
            <span class="sm:hidden">{{ hardRefreshLoading ? '...' : 'Refresh' }}</span>
          </button>
        </div>
      </div>
    </div>

    <!-- Consumer Groups Table -->
    <div class="card overflow-hidden">
      <div class="overflow-x-auto">
        <table class="w-full">
          <thead>
            <tr class="border-b border-light-200 dark:border-dark-50">
              <th class="text-left px-4 py-3 text-xs font-semibold text-light-500 uppercase tracking-wide">
                Group Name
              </th>
              <th class="text-left px-4 py-3 text-xs font-semibold text-light-500 uppercase tracking-wide">
                Status
              </th>
              <th class="text-left px-4 py-3 text-xs font-semibold text-light-500 uppercase tracking-wide">
                Queue
              </th>
              <th class="text-right px-4 py-3 text-xs font-semibold text-light-500 uppercase tracking-wide">
                Members
              </th>
              <th class="text-right px-4 py-3 text-xs font-semibold text-light-500 uppercase tracking-wide">
                Offset Lag
              </th>
              <th class="text-right px-4 py-3 text-xs font-semibold text-light-500 uppercase tracking-wide">
                Time Lag
              </th>
              <th class="text-center px-4 py-3 text-xs font-semibold text-light-500 uppercase tracking-wide">
                Actions
              </th>
            </tr>
          </thead>
          <tbody v-if="loading">
            <tr v-for="i in 5" :key="i" class="border-b border-light-100 dark:border-dark-100">
              <td class="px-4 py-3"><div class="skeleton h-4 w-40" /></td>
              <td class="px-4 py-3"><div class="skeleton h-5 w-16" /></td>
              <td class="px-4 py-3"><div class="skeleton h-4 w-24" /></td>
              <td class="px-4 py-3"><div class="skeleton h-4 w-8 ml-auto" /></td>
              <td class="px-4 py-3"><div class="skeleton h-4 w-12 ml-auto" /></td>
              <td class="px-4 py-3"><div class="skeleton h-4 w-16 ml-auto" /></td>
              <td class="px-4 py-3"><div class="skeleton h-4 w-32 mx-auto" /></td>
            </tr>
          </tbody>
          <tbody v-else-if="filteredConsumers.length > 0">
            <tr 
              v-for="consumer in filteredConsumers" 
              :key="`${consumer.name}-${consumer.queueName}`"
              class="border-b border-light-100 dark:border-dark-100 hover:bg-light-50 dark:hover:bg-dark-300 transition-colors"
            >
              <td class="px-4 py-3">
                <span class="font-medium text-light-900 dark:text-white">
                  {{ consumer.name }}
                </span>
              </td>
              <td class="px-4 py-3">
                <span 
                  class="inline-flex items-center gap-1.5 text-xs font-medium px-2 py-1 rounded-full"
                  :class="getStatusClass(consumer)"
                >
                  <span 
                    class="w-1.5 h-1.5 rounded-full"
                    :class="getStatusDotClass(consumer)"
                  />
                  {{ getStatusText(consumer) }}
                </span>
              </td>
              <td class="px-4 py-3">
                <span class="text-sm text-crown-600 dark:text-crown-400">
                  {{ consumer.queueName || '-' }}
                </span>
              </td>
              <td class="px-4 py-3 text-right">
                <span class="text-sm font-medium text-cyber-600 dark:text-cyber-400">
                  {{ consumer.members || 0 }}
                </span>
              </td>
              <td class="px-4 py-3 text-right">
                <span 
                  class="text-sm font-medium"
                  :class="(consumer.totalLag || 0) > 0 ? 'text-amber-600 dark:text-amber-400' : 'text-light-600 dark:text-light-400'"
                >
                  {{ formatNumber(consumer.totalLag || 0) }}
                </span>
              </td>
              <td class="px-4 py-3 text-right">
                <span 
                  class="text-sm font-medium"
                  :class="(consumer.maxTimeLag || 0) > 0 ? 'text-rose-600 dark:text-rose-400' : 'text-light-600 dark:text-light-400'"
                >
                  {{ (consumer.maxTimeLag || 0) > 0 ? formatDuration(consumer.maxTimeLag * 1000) : '-' }}
                </span>
              </td>
              <td class="px-4 py-3">
                <div class="flex items-center justify-center gap-1">
                  <button 
                    @click="viewConsumer(consumer)"
                    class="p-1.5 rounded hover:bg-light-200 dark:hover:bg-dark-200 text-light-600 dark:text-light-400 hover:text-queen-600 dark:hover:text-queen-400 transition-colors"
                    title="View details"
                  >
                    <svg class="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
                      <path stroke-linecap="round" stroke-linejoin="round" d="M2.036 12.322a1.012 1.012 0 010-.639C3.423 7.51 7.36 4.5 12 4.5c4.638 0 8.573 3.007 9.963 7.178.07.207.07.431 0 .639C20.577 16.49 16.64 19.5 12 19.5c-4.638 0-8.573-3.007-9.963-7.178z" />
                      <path stroke-linecap="round" stroke-linejoin="round" d="M15 12a3 3 0 11-6 0 3 3 0 016 0z" />
                    </svg>
                  </button>
                  <button 
                    v-if="consumer.queueName"
                    @click="handleMoveToNow(consumer)"
                    class="p-1.5 rounded hover:bg-light-200 dark:hover:bg-dark-200 text-light-600 dark:text-light-400 hover:text-cyber-600 dark:hover:text-cyber-400 transition-colors"
                    title="Move to Now (skip pending)"
                  >
                    <svg class="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
                      <path stroke-linecap="round" stroke-linejoin="round" d="M3 8.688c0-.864.933-1.405 1.683-.977l7.108 4.062a1.125 1.125 0 010 1.953l-7.108 4.062A1.125 1.125 0 013 16.81V8.688zM12.75 8.688c0-.864.933-1.405 1.683-.977l7.108 4.062a1.125 1.125 0 010 1.953l-7.108 4.062a1.125 1.125 0 01-1.683-.977V8.688z" />
                    </svg>
                  </button>
                  <button 
                    v-if="consumer.queueName"
                    @click="openSeekModal(consumer)"
                    class="p-1.5 rounded hover:bg-light-200 dark:hover:bg-dark-200 text-light-600 dark:text-light-400 hover:text-crown-600 dark:hover:text-crown-400 transition-colors"
                    title="Seek to timestamp"
                  >
                    <svg class="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
                      <path stroke-linecap="round" stroke-linejoin="round" d="M12 6v6h4.5m4.5 0a9 9 0 11-18 0 9 9 0 0118 0z" />
                    </svg>
                  </button>
                  <button 
                    @click="confirmDelete(consumer)"
                    class="p-1.5 rounded hover:bg-rose-100 dark:hover:bg-rose-900/30 text-light-600 dark:text-light-400 hover:text-rose-600 dark:hover:text-rose-400 transition-colors"
                    title="Delete consumer group for this queue"
                  >
                    <svg class="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
                      <path stroke-linecap="round" stroke-linejoin="round" d="M14.74 9l-.346 9m-4.788 0L9.26 9m9.968-3.21c.342.052.682.107 1.022.166m-1.022-.165L18.16 19.673a2.25 2.25 0 01-2.244 2.077H8.084a2.25 2.25 0 01-2.244-2.077L4.772 5.79m14.456 0a48.108 48.108 0 00-3.478-.397m-12 .562c.34-.059.68-.114 1.022-.165m0 0a48.11 48.11 0 013.478-.397m7.5 0v-.916c0-1.18-.91-2.164-2.09-2.201a51.964 51.964 0 00-3.32 0c-1.18.037-2.09 1.022-2.09 2.201v.916m7.5 0a48.667 48.667 0 00-7.5 0" />
                    </svg>
                  </button>
                </div>
              </td>
            </tr>
          </tbody>
          <tbody v-else>
            <tr>
              <td colspan="7" class="px-4 py-12 text-center">
                <svg class="w-12 h-12 mx-auto text-light-400 dark:text-light-600 mb-3" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M18 18.72a9.094 9.094 0 003.741-.479 3 3 0 00-4.682-2.72m.94 3.198l.001.031c0 .225-.012.447-.037.666A11.944 11.944 0 0112 21c-2.17 0-4.207-.576-5.963-1.584A6.062 6.062 0 016 18.719m12 0a5.971 5.971 0 00-.941-3.197m0 0A5.995 5.995 0 0012 12.75a5.995 5.995 0 00-5.058 2.772m0 0a3 3 0 00-4.681 2.72 8.986 8.986 0 003.74.477m.94-3.197a5.971 5.971 0 00-.94 3.197M15 6.75a3 3 0 11-6 0 3 3 0 016 0zm6 3a2.25 2.25 0 11-4.5 0 2.25 2.25 0 014.5 0zm-13.5 0a2.25 2.25 0 11-4.5 0 2.25 2.25 0 014.5 0z" />
                </svg>
                <h3 class="text-sm font-semibold text-light-900 dark:text-white mb-1">No consumer groups found</h3>
                <p class="text-sm text-light-600 dark:text-light-400">
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
      class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-dark-500/50 backdrop-blur-sm"
      @click="selectedConsumer = null"
    >
      <div 
        class="card w-full max-w-2xl max-h-[80vh] overflow-hidden animate-slide-up"
        @click.stop
      >
        <div class="card-header flex items-center justify-between">
          <h3 class="font-semibold text-light-900 dark:text-white">{{ selectedConsumer.name }}</h3>
          <button @click="selectedConsumer = null" class="btn btn-ghost btn-icon">
            <svg class="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
              <path stroke-linecap="round" stroke-linejoin="round" d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>
        <div class="card-body overflow-y-auto max-h-[60vh]">
          <div class="grid grid-cols-3 gap-4 mb-6">
            <div class="p-4 rounded-lg bg-light-100 dark:bg-dark-300 text-center">
              <p class="text-xs text-light-500 uppercase tracking-wide">Members</p>
              <p class="text-2xl font-bold text-cyber-600 dark:text-cyber-400 mt-1">
                {{ selectedConsumer.members || 0 }}
              </p>
            </div>
            <div class="p-4 rounded-lg bg-light-100 dark:bg-dark-300 text-center">
              <p class="text-xs text-light-500 uppercase tracking-wide">State</p>
              <p class="text-lg font-bold mt-1" :class="{
                'text-emerald-600 dark:text-emerald-400': getStatusText(selectedConsumer) === 'Stable',
                'text-amber-600 dark:text-amber-400': getStatusText(selectedConsumer) === 'Warning',
                'text-rose-600 dark:text-rose-400': getStatusText(selectedConsumer) === 'Lagging'
              }">
                {{ getStatusText(selectedConsumer) }}
              </p>
            </div>
            <div class="p-4 rounded-lg bg-light-100 dark:bg-dark-300 text-center">
              <p class="text-xs text-light-500 uppercase tracking-wide">Lag</p>
              <p class="text-2xl font-bold text-light-900 dark:text-white mt-1">
                {{ formatNumber(selectedConsumer.totalLag || 0) }}
              </p>
            </div>
          </div>
          
          <div class="mb-4">
            <h4 class="text-sm font-medium text-light-700 dark:text-light-300 mb-2">Queue</h4>
            <div class="p-3 rounded-lg bg-light-100 dark:bg-dark-300">
              <span class="font-medium text-light-900 dark:text-white">{{ selectedConsumer.queueName || '-' }}</span>
            </div>
          </div>
          
          <div v-if="selectedConsumer.topics?.length > 0">
            <h4 class="text-sm font-medium text-light-700 dark:text-light-300 mb-3">Topics</h4>
            <div class="space-y-2">
              <div 
                v-for="topic in selectedConsumer.topics" 
                :key="topic"
                class="flex items-center justify-between p-3 rounded-lg bg-light-100 dark:bg-dark-300"
              >
                <span class="font-medium text-light-900 dark:text-white">{{ topic }}</span>
                <button 
                  @click="seekQueue(selectedConsumer.name, topic)"
                  class="btn btn-ghost text-xs"
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
      class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-dark-500/50 backdrop-blur-sm"
      @click="consumerToDelete = null"
    >
      <div 
        class="card p-6 w-full max-w-md animate-slide-up"
        @click.stop
      >
        <h3 class="text-lg font-semibold text-light-900 dark:text-white mb-2">
          Delete Consumer Group
        </h3>
        <p class="text-light-600 dark:text-light-400 mb-4">
          Are you sure you want to delete <strong>{{ consumerToDelete.name }}</strong>
          <span v-if="consumerToDelete.queueName"> for queue <strong>{{ consumerToDelete.queueName }}</strong></span>?
        </p>
        <p class="text-sm text-light-500 mb-6">
          This will remove partition consumer state{{ consumerToDelete.queueName ? ' for this queue only' : '' }}.
        </p>
        
        <label class="flex items-center gap-2 text-sm text-light-700 dark:text-light-300 mb-6">
          <input
            v-model="deleteMetadata"
            type="checkbox"
            class="w-4 h-4 rounded border-light-300 dark:border-dark-50 text-queen-500 focus:ring-queen-500"
          />
          Also delete subscription metadata
        </label>
        
        <div class="flex items-center justify-end gap-3">
          <button @click="consumerToDelete = null" class="btn btn-secondary">
            Cancel
          </button>
          <button @click="deleteConsumer" :disabled="actionLoading" class="btn bg-rose-600 text-white hover:bg-rose-700">
            {{ actionLoading ? 'Deleting...' : 'Delete' }}
          </button>
        </div>
      </div>
    </div>

    <!-- Seek Modal -->
    <div 
      v-if="showSeekModal"
      class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-dark-500/50 backdrop-blur-sm"
      @click="showSeekModal = false"
    >
      <div 
        class="card p-6 w-full max-w-md animate-slide-up"
        @click.stop
      >
        <h3 class="text-lg font-semibold text-light-900 dark:text-white mb-2">
          Seek Cursor Position
        </h3>
        <p class="text-light-600 dark:text-light-400 mb-4">
          {{ seekConsumer?.name }} / {{ seekConsumer?.queueName }}
        </p>
        
        <div class="mb-4">
          <label class="block text-sm font-medium text-light-700 dark:text-light-300 mb-2">
            Target Timestamp
          </label>
          <input
            v-model="seekTimestamp"
            type="datetime-local"
            class="input"
          />
          <p class="text-xs text-light-500 mt-2">
            ⚠️ The cursor will move to the last message at or before this timestamp. Messages after this point will be re-consumed.
          </p>
        </div>
        
        <div class="flex items-center justify-end gap-3">
          <button @click="showSeekModal = false" class="btn btn-secondary">
            Cancel
          </button>
          <button 
            @click="handleSeekToTimestamp" 
            :disabled="actionLoading || !seekTimestamp" 
            class="btn btn-primary"
          >
            {{ actionLoading ? 'Seeking...' : 'Seek to Timestamp' }}
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
  // Fallback: check if maxTimeLag > 0 or totalLag > 0
  return (consumer.maxTimeLag || 0) > 0 || (consumer.totalLag || 0) > 0
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
      // Sort by maxTimeLag first (more meaningful), then totalLag
      const aLag = (a.maxTimeLag || 0) * 1000000 + (a.totalLag || 0)
      const bLag = (b.maxTimeLag || 0) * 1000000 + (b.totalLag || 0)
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

const totalLag = computed(() =>
  consumers.value.reduce((sum, c) => sum + (c.totalLag || 0), 0)
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
  if (status === 'Dead') return 'bg-light-200 dark:bg-dark-200 text-light-600 dark:text-light-400'
  return 'bg-emerald-100 dark:bg-emerald-900/30 text-emerald-700 dark:text-emerald-400'
}

const getStatusDotClass = (consumer) => {
  const status = consumer.state
  if (status === 'Lagging') return 'bg-rose-500 animate-pulse'
  if (status === 'Dead') return 'bg-light-400'
  return 'bg-emerald-500'
}

const fetchConsumers = async () => {
  loading.value = true
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
