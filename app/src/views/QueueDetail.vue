<template>
  <div class="view-container">

    <!-- ======================================================================
         Loading skeleton — shown only on first fetch; subsequent refreshes
         leave the previous data on screen so the eye doesn't lose its place.
         ====================================================================== -->
    <div v-if="loading" style="display:flex; flex-direction:column; gap:14px;">
      <div class="skeleton" style="height:34px; width:60%;" />
      <div class="skeleton" style="height:64px; width:100%;" />
      <div class="skeleton" style="height:360px; width:100%;" />
      <div class="grid-2">
        <div class="skeleton" style="height:240px;" />
        <div class="skeleton" style="height:240px;" />
      </div>
    </div>

    <!-- Error state -->
    <div v-else-if="error && !queueData" class="card" style="padding:24px; color:var(--ember-400);">
      <p style="font-weight:600; margin-bottom:8px;">Error loading queue</p>
      <p style="font-size:13px;">{{ error }}</p>
    </div>

    <template v-else-if="queueData || statusData">
      <!-- ====================================================================
           Detail bar — back · queue · meta · time-range · live · actions.
           Same idiom as before, plus a Dashboard-style range selector and
           a row of quick actions that turn this page into a launch pad.
           ==================================================================== -->
      <div class="qd-bar">
        <button @click="$router.push('/queues')" class="detail-back" title="Back to queues">
          <svg width="14" height="14" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.8">
            <path stroke-linecap="round" stroke-linejoin="round" d="M10 19l-7-7m0 0l7-7m-7 7h18" />
          </svg>
        </button>

        <span class="detail-name font-mono">{{ queueData?.name || queueName }}</span>

        <span class="detail-meta font-mono">
          <span v-if="queueData?.namespace">{{ queueData.namespace }}</span>
          <span v-if="queueData?.task">· {{ queueData.task }}</span>
          <span>· {{ partitions.length }} partition{{ partitions.length === 1 ? '' : 's' }}</span>
          <span v-if="queueData?.priority != null">· p{{ queueData.priority }}</span>
          <span v-if="queueData?.createdAt">· created {{ formatRelative(queueData.createdAt) }}</span>
        </span>

        <div class="qd-bar-spacer" />

        <div class="seg">
          <button
            v-for="r in timeRanges"
            :key="r.value"
            :class="{ on: selectedRange === r.value }"
            @click="selectedRange = r.value"
          >{{ r.label }}</button>
        </div>

        <div class="qd-live" :title="'last refresh ' + refreshAgo">
          <span class="pulse" />
          <span>live · {{ refreshAgo }}</span>
        </div>
      </div>

      <!-- Quick actions — second row, separated so the bar above stays
           uncluttered on narrow viewports. -->
      <div class="qd-actions">
        <button class="btn btn-ghost" @click="goMessages">
          <svg width="13" height="13" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.8">
            <path stroke-linecap="round" stroke-linejoin="round" d="M8 10h.01M12 10h.01M16 10h.01M21 12c0 4.418-4.03 8-9 8a9.86 9.86 0 01-4-.8L3 21l1.4-4.2A8.96 8.96 0 013 12c0-4.418 4.03-8 9-8s9 3.582 9 8z" />
          </svg>
          Browse messages
        </button>
        <button class="btn btn-ghost" @click="goDLQ" :class="{ 'btn-danger': totalMessages.deadLetter > 0 }">
          <svg width="13" height="13" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.8">
            <path stroke-linecap="round" stroke-linejoin="round" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
          </svg>
          DLQ
          <span v-if="totalMessages.deadLetter > 0" class="qd-badge qd-badge-bad">{{ formatNumber(totalMessages.deadLetter) }}</span>
        </button>
        <button class="btn btn-ghost" @click="goTraces">
          <svg width="13" height="13" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.8">
            <path stroke-linecap="round" stroke-linejoin="round" d="M9 5H7a2 2 0 00-2 2v12a2 2 0 002 2h10a2 2 0 002-2V7a2 2 0 00-2-2h-2M9 5a2 2 0 002 2h2a2 2 0 002-2M9 5a2 2 0 012-2h2a2 2 0 012 2m-6 9l2 2 4-4" />
          </svg>
          Traces
        </button>

        <span class="qd-actions-spacer" />

        <button @click="showDeleteModal = true" class="btn btn-danger">
          <svg width="13" height="13" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.8">
            <path stroke-linecap="round" stroke-linejoin="round" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
          </svg>
          Delete queue
        </button>
      </div>

      <!-- ====================================================================
           Banner row — appears only when there's something to flag, so the
           page is quiet on healthy queues. Worst-first ordering.
           ==================================================================== -->
      <div v-if="banners.length" class="qd-banners">
        <div v-for="(b, i) in banners" :key="i" class="qd-banner" :class="`qd-banner-${b.tone}`" @click="b.action && b.action()">
          <span class="qd-banner-dot" :class="`qd-banner-dot-${b.tone}`" />
          <span class="qd-banner-title">{{ b.title }}</span>
          <span class="qd-banner-detail">{{ b.detail }}</span>
          <span class="qd-banner-cta" v-if="b.cta">{{ b.cta }} →</span>
        </div>
      </div>

      <!-- ====================================================================
           Counts strip — point-in-time scope (left) + now-snapshots (right).
           Mirrors Dashboard's counts-strip but scoped to this queue.
           ==================================================================== -->
      <div class="counts-strip">
        <div class="counts-group">
          <span class="count-item count-static">
            <strong class="num" :class="pendingNumClass(totalMessages.pending)">{{ formatNumber(totalMessages.pending) }}</strong>
            <span>pending</span>
          </span>
          <span class="count-sep">·</span>
          <span class="count-item count-static">
            <strong>{{ formatNumber(totalMessages.processing) }}</strong>
            <span>processing</span>
          </span>
          <span class="count-sep">·</span>
          <span class="count-item count-static count-muted">
            <strong>{{ formatNumber(totalMessages.completed) }}</strong>
            <span>completed</span>
          </span>
          <span class="count-sep">·</span>
          <span class="count-item count-static">
            <strong class="num" :class="{ bad: totalMessages.failed > 0 }">{{ formatNumber(totalMessages.failed) }}</strong>
            <span>failed</span>
          </span>
          <span class="count-sep">·</span>
          <button class="count-item" @click="goDLQ">
            <strong class="num" :class="{ bad: totalMessages.deadLetter > 0 }">{{ formatNumber(totalMessages.deadLetter) }}</strong>
            <span>dlq</span>
          </button>
          <span class="count-sep">·</span>
          <span class="count-item count-static count-muted">
            <strong>{{ formatNumber(totalMessages.total) }}</strong>
            <span>total</span>
          </span>
          <span class="count-sep">·</span>
          <span class="count-item count-static">
            <strong>{{ formatNumber(partitions.length) }}</strong>
            <span>{{ partitions.length === 1 ? 'partition' : 'partitions' }}</span>
          </span>
          <span class="count-sep">·</span>
          <button class="count-item" @click="$router.push('/consumers')" :disabled="loadingConsumers">
            <strong>{{ formatNumber(queueConsumers.length) }}</strong>
            <span>consumer {{ queueConsumers.length === 1 ? 'group' : 'groups' }}</span>
          </button>
        </div>

        <div class="counts-group counts-group-right" :title="'Now-rates from queue-ops time series; parked = currently waiting long-poll consumers.'">
          <span class="count-item-label">now</span>
          <span class="count-item count-static count-tight">
            <strong>{{ nowSnapshot.push }}</strong><span class="count-suffix">push/s</span>
          </span>
          <span class="count-sep">·</span>
          <span class="count-item count-static count-tight">
            <strong>{{ nowSnapshot.pop }}</strong><span class="count-suffix">pop/s</span>
          </span>
          <span class="count-sep">·</span>
          <span class="count-item count-static count-tight">
            <strong>{{ nowSnapshot.ack }}</strong><span class="count-suffix">ack/s</span>
          </span>
          <span class="count-sep">·</span>
          <span class="count-item count-static count-tight">
            <strong>{{ nowSnapshot.parked }}</strong><span class="count-suffix">parked</span>
          </span>
        </div>
      </div>

      <!-- ====================================================================
           Metric table — Dashboard idiom, queue-scoped.
           Eight rows tell the story of this queue end-to-end:
             flow (Throughput, Pending Δ, Time lag, Fill ratio)
             health (Errors, Parked)
             admin (Partitions activity, Consumed)
           Each row reuses MetricRow for consistent typography + interactions.
           ==================================================================== -->
      <div class="metric-table">
        <div class="metric-head">
          <span></span>
          <span>Metric</span>
          <span class="h-value">Now</span>
          <span>Context</span>
          <span class="h-spark">{{ selectedRange }}</span>
          <span></span>
        </div>

        <MetricRow
          label="Throughput"
          :value="throughput.current"
          unit="/s"
          :context="throughputContext"
          :series="throughputSeries"
          :labels="chartLabels"
          :value-format="fmtRate"
          expand-unit="msgs / sec"
          :loading="loadingOps"
          :expanded="isExpanded('throughput')"
          @toggle-expand="toggleRow('throughput')"
        />
        <MetricRow
          label="Pending Δ"
          :value="pendingDeltaDisplay"
          unit="msgs"
          :context="pendingDeltaContext"
          :sparkline="pendingDeltaSeries"
          :labels="chartLabels"
          :value-format="fmtCount"
          expand-unit="msgs (cumulative)"
          :severity="pendingDeltaSeverity"
          :loading="loadingOps"
          tooltip="Cumulative (push − ack) across the selected window. Positive = queue filling, negative = draining."
          :expanded="isExpanded('pendingDelta')"
          @toggle-expand="toggleRow('pendingDelta')"
        />
        <MetricRow
          label="Time lag"
          :context="lagContext"
          :series="lagSeriesData"
          :labels="chartLabels"
          :value-format="fmtLagMs"
          expand-unit="ms"
          :severity="lagSeverityKey"
          :loading="loadingOps"
          tooltip="Per-bucket avg / max delivery delay, derived from queue lag metrics."
          :expanded="isExpanded('timeLag')"
          @toggle-expand="toggleRow('timeLag')"
        >
          <template #value>
            <template v-if="lagLatest.avg === null && lagLatest.max === null">
              <span class="num">—</span>
            </template>
            <template v-else>
              <span class="num" :class="lagNumClass(lagLatest.avg)">{{ fmtLagShort(lagLatest.avg) }}</span>
              <span class="mr-sep">/</span>
              <span class="num" :class="lagNumClass(lagLatest.max)">{{ fmtLagShort(lagLatest.max) }}</span>
            </template>
          </template>
        </MetricRow>
        <MetricRow
          label="Fill ratio"
          :context="fillContext"
          :series="fillSeriesData"
          :labels="chartLabels"
          :value-format="fmtFillPct"
          expand-unit="%"
          :severity="fillSeverityKey"
          :loading="loadingOps"
          tooltip="Long-polls returning a message ÷ all long-poll completions on this queue. <30% with traffic = consumers mostly empty; ~100% sustained = fully utilized — watch Time lag."
          :expanded="isExpanded('fillRatio')"
          @toggle-expand="toggleRow('fillRatio')"
        >
          <template #value>
            <template v-if="fillLatest === null">
              <span class="num">—</span>
            </template>
            <template v-else>
              <span class="num" :class="fillSeverityKey">{{ fillLatest.toFixed(1) }}</span><i class="mr-unit">%</i>
            </template>
          </template>
        </MetricRow>
        <MetricRow
          label="Errors"
          :value="formatNumber(errorsTotal)"
          :context="errorsContext"
          :sparkline="errorsSeries"
          :labels="chartLabels"
          :value-format="fmtCount"
          expand-unit="count"
          :severity="errorsSeverity"
          :loading="loadingOps"
          tooltip="ack failures over the selected window for this queue."
          :expanded="isExpanded('errors')"
          @toggle-expand="toggleRow('errors')"
        />
        <MetricRow
          label="Parked"
          :value="formatNumber(Math.round(parkedLatest))"
          unit="consumers"
          :context="parkedContext"
          :sparkline="parkedSeries"
          :labels="chartLabels"
          :value-format="fmtCount"
          expand-unit="long-polls"
          :loading="loadingOps"
          tooltip="Long-poll consumer connections currently waiting on this queue, averaged each minute."
          :expanded="isExpanded('parked')"
          @toggle-expand="toggleRow('parked')"
        />
        <MetricRow
          label="Partitions Δ"
          :context="'created / deleted in window · current ' + partitions.length"
          :series="partitionOpsSeries"
          :labels="chartLabels"
          :value-format="fmtCount"
          expand-unit="count"
          :loading="loadingOps"
          :expanded="isExpanded('partitionsOps')"
          @toggle-expand="toggleRow('partitionsOps')"
        >
          <template #value>
            <span class="num" style="color:var(--text-hi);">+{{ formatNumber(partitionCreatedTotal) }}</span>
            <span class="mr-sep">/</span>
            <span class="num mute">−{{ formatNumber(partitionDeletedTotal) }}</span>
          </template>
        </MetricRow>
        <MetricRow
          label="Consumed"
          :value="formatNumber(consumedTotal)"
          unit="msgs"
          :context="consumedContext"
          :sparkline="popSeries"
          :labels="chartLabels"
          :value-format="fmtCount"
          expand-unit="msgs (per bucket)"
          :loading="loadingOps"
          tooltip="Lifetime messages consumed across all consumer groups on this queue (from partition cursors); sparkline shows pop rate per bucket."
          :expanded="isExpanded('consumed')"
          @toggle-expand="toggleRow('consumed')"
        />
      </div>

      <!-- ====================================================================
           Two-column row — Consumer groups (left) + Lagging partitions (right).
           Same dot · name · right-metric · meta-line idiom as Dashboard.
           ==================================================================== -->
      <div class="grid-2">
        <!-- Consumer groups on this queue -->
        <div class="card">
          <div class="card-header">
            <h3>Consumer groups</h3>
            <span class="muted">
              {{ queueConsumers.length }} on this queue<template v-if="queueConsumersLagging > 0"> · <span class="num warn">{{ queueConsumersLagging }} lagging</span></template>
            </span>
          </div>

          <div v-if="loadingConsumers && !queueConsumers.length" class="card-body entity-list">
            <div v-for="i in 4" :key="i" class="skeleton" style="height:48px; border-radius:8px;" />
          </div>

          <div v-else-if="queueConsumers.length" class="card-body entity-list">
            <button
              v-for="g in queueConsumers"
              :key="g.name + '@' + g.queueName"
              class="entity-row"
              @click="$router.push('/consumers')"
            >
              <div class="entity-head">
                <span class="status-dot" :class="cgDotClass(g)" />
                <span class="entity-name">
                  <template v-if="g.name === '__QUEUE_MODE__'">
                    <span class="meta-tag">queue mode</span>
                  </template>
                  <template v-else>{{ g.name }}</template>
                </span>
                <span class="entity-right num" :class="lagNumClass((g.maxTimeLag || 0) * 1000)">
                  {{ (g.maxTimeLag || 0) > 0 ? formatDurationSec(g.maxTimeLag) : '—' }}
                </span>
              </div>
              <div class="entity-meta">
                <span class="meta-text">
                  <strong>{{ g.members || 0 }}</strong> {{ (g.members || 0) === 1 ? 'partition' : 'partitions' }}
                  <template v-if="(g.partitionsWithLag || 0) > 0">
                    <span class="meta-sep">·</span>
                    <span class="num warn">{{ g.partitionsWithLag }} lagging</span>
                  </template>
                  <template v-if="g.subscriptionMode">
                    <span class="meta-sep">·</span>
                    <span class="meta-tag">{{ g.subscriptionMode }}</span>
                  </template>
                  <template v-if="g.subscriptionTimestamp">
                    <span class="meta-sep">·</span>
                    since {{ formatRelative(g.subscriptionTimestamp) }}
                  </template>
                </span>
              </div>
            </button>
          </div>

          <div v-else class="card-body entity-empty">No consumer groups have subscribed yet</div>

          <div class="card-foot">
            <a class="card-foot-link" @click="$router.push('/consumers')">Open consumers →</a>
          </div>
        </div>

        <!-- Lagging partitions on this queue -->
        <div class="card">
          <div class="card-header" style="gap:8px;">
            <h3>Lagging partitions</h3>
            <span v-if="laggingFiltered.length > 0" class="chip chip-warn">{{ laggingFiltered.length }} above {{ getLagLabel(lagThreshold) }}</span>
            <span v-else-if="laggingLoaded" class="chip chip-ok">all caught up</span>
            <span style="margin-left:auto;" />
            <span class="label-xs" style="color:var(--text-low);">Threshold</span>
            <div class="seg">
              <button
                v-for="p in lagPresets"
                :key="p.value"
                :class="{ on: lagThreshold === p.value }"
                @click="lagThreshold = p.value; loadLaggingPartitions()"
              >{{ p.label }}</button>
            </div>
          </div>

          <div v-if="laggingLoading && !laggingFiltered.length" class="card-body entity-list">
            <div v-for="i in 3" :key="i" class="skeleton" style="height:48px; border-radius:8px;" />
          </div>

          <div v-else-if="laggingFiltered.length" class="card-body entity-list">
            <div
              v-for="p in laggingFiltered"
              :key="p.consumer_group + '@' + p.partition_name"
              class="entity-row entity-row-static"
            >
              <div class="entity-head">
                <span class="status-dot status-dot-warning" />
                <span class="entity-name font-mono">{{ p.partition_name }}</span>
                <span class="entity-right num" :class="lagSecClass(p.time_lag_seconds)">
                  {{ formatDurationSec(p.time_lag_seconds || 0) }}
                </span>
              </div>
              <div class="entity-meta" style="display:flex; align-items:center; justify-content:space-between;">
                <span class="meta-text">
                  <span v-if="p.consumer_group === '__QUEUE_MODE__'" class="meta-tag">queue mode</span>
                  <strong v-else>{{ p.consumer_group }}</strong>
                  <span class="meta-sep">·</span>
                  <span class="num warn">{{ formatNumber(p.offset_lag) }}</span> unconsumed
                  <template v-if="p.oldest_unconsumed_at">
                    <span class="meta-sep">·</span>
                    oldest {{ formatRelative(p.oldest_unconsumed_at) }}
                  </template>
                </span>
                <button
                  class="btn btn-ghost"
                  style="font-size:11px; padding:3px 8px;"
                  :disabled="skippingPartition === skipKey(p)"
                  @click.stop="handleSkipPartition(p)"
                >{{ skippingPartition === skipKey(p) ? 'Skipping…' : 'Skip to end' }}</button>
              </div>
            </div>
          </div>

          <div v-else class="card-body entity-empty">
            <span style="color:var(--ok-500);">●</span>
            <span style="margin-left:6px;">No partitions lag above {{ getLagLabel(lagThreshold) }}</span>
          </div>
        </div>
      </div>

      <!-- ====================================================================
           Partitions table — enriched with status dot, oldest message,
           last-activity, cursor depth, and per-partition severity tone.
           ==================================================================== -->
      <div class="card" style="margin-top:14px;">
        <div class="card-header" style="justify-content:space-between;">
          <div style="display:flex; align-items:center; gap:10px;">
            <h3>Partitions</h3>
            <span class="chip chip-ice">{{ sortedPartitions.length }} of {{ partitions.length }}</span>
          </div>
          <div style="width:200px;">
            <input
              v-model="partitionSearch"
              type="text"
              placeholder="Search partitions..."
              class="input"
              style="font-size:12px;"
            />
          </div>
        </div>

        <div style="overflow-x:auto;">
          <table class="t qd-parts" style="width:100%;">
            <thead>
              <tr>
                <th></th>
                <th @click="sortPartitions('name')" class="qd-sortable">
                  <div class="qd-th-inner">Partition <SortIcon :active="partitionSortColumn === 'name'" :asc="partitionSortDirection === 'asc'" /></div>
                </th>
                <th @click="sortPartitions('pending')" class="qd-sortable" style="text-align:right;">
                  <div class="qd-th-inner" style="justify-content:flex-end;">Pending <SortIcon :active="partitionSortColumn === 'pending'" :asc="partitionSortDirection === 'asc'" /></div>
                </th>
                <th @click="sortPartitions('processing')" class="qd-sortable" style="text-align:right;">
                  <div class="qd-th-inner" style="justify-content:flex-end;">Processing <SortIcon :active="partitionSortColumn === 'processing'" :asc="partitionSortDirection === 'asc'" /></div>
                </th>
                <th @click="sortPartitions('completed')" class="qd-sortable" style="text-align:right;">
                  <div class="qd-th-inner" style="justify-content:flex-end;">Completed <SortIcon :active="partitionSortColumn === 'completed'" :asc="partitionSortDirection === 'asc'" /></div>
                </th>
                <th @click="sortPartitions('deadLetter')" class="qd-sortable" style="text-align:right;">
                  <div class="qd-th-inner" style="justify-content:flex-end;">DLQ <SortIcon :active="partitionSortColumn === 'deadLetter'" :asc="partitionSortDirection === 'asc'" /></div>
                </th>
                <th @click="sortPartitions('consumed')" class="qd-sortable" style="text-align:right;">
                  <div class="qd-th-inner" style="justify-content:flex-end;">Consumed <SortIcon :active="partitionSortColumn === 'consumed'" :asc="partitionSortDirection === 'asc'" /></div>
                </th>
                <th @click="sortPartitions('oldest')" class="qd-sortable">
                  <div class="qd-th-inner">Oldest <SortIcon :active="partitionSortColumn === 'oldest'" :asc="partitionSortDirection === 'asc'" /></div>
                </th>
                <th @click="sortPartitions('lastActivity')" class="qd-sortable">
                  <div class="qd-th-inner">Last activity <SortIcon :active="partitionSortColumn === 'lastActivity'" :asc="partitionSortDirection === 'asc'" /></div>
                </th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="p in sortedPartitions" :key="p.id || p.name">
                <td class="qd-pdot">
                  <span class="status-dot" :class="partitionDotClass(p)" :title="partitionStatusText(p)" />
                </td>
                <td>
                  <div class="qd-pname font-mono">{{ p.name }}</div>
                </td>
                <td style="text-align:right;" class="font-mono tabular-nums num"
                    :class="{ warn: pendingWarn(p), bad: pendingBad(p) }">
                  {{ formatNumber(Math.max(0, p.messages?.pending || 0)) }}
                </td>
                <td style="text-align:right;" class="font-mono tabular-nums">{{ formatNumber(p.messages?.processing || 0) }}</td>
                <td style="text-align:right; color:var(--text-mid);" class="font-mono tabular-nums">
                  {{ formatNumber(p.messages?.completed || 0) }}
                </td>
                <td style="text-align:right;" class="font-mono tabular-nums num" :class="{ bad: (p.messages?.deadLetter || 0) > 0 }">
                  {{ formatNumber(p.messages?.deadLetter || 0) }}
                </td>
                <td style="text-align:right;">
                  <div class="font-mono tabular-nums" style="font-size:13px;">{{ formatNumber(p.cursor?.totalConsumed || 0) }}</div>
                  <div style="font-size:11px; color:var(--text-low);">
                    {{ formatNumber(p.cursor?.batchesConsumed || 0) }} batches
                  </div>
                </td>
                <td>
                  <span v-if="p.oldestMessage" class="font-mono" :class="oldestClass(p.oldestMessage)" style="font-size:12px;">
                    {{ formatRelative(p.oldestMessage) }}
                  </span>
                  <span v-else style="color:var(--text-low); font-size:12px;">—</span>
                </td>
                <td>
                  <span v-if="p.lastActivity" class="font-mono" style="font-size:12px; color:var(--text-mid);">
                    {{ formatRelative(p.lastActivity) }}
                  </span>
                  <span v-else style="color:var(--text-low); font-size:12px;">—</span>
                </td>
              </tr>
              <tr v-if="sortedPartitions.length === 0">
                <td colspan="9" style="text-align:center; padding:32px; color:var(--text-low); font-size:13px;">
                  {{ partitions.length === 0 ? 'No partitions yet' : 'No partitions match your search' }}
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>

      <!-- ====================================================================
           Configuration card — full queue config + identity chips.
           Always at the bottom: config rarely changes, but is the canonical
           record when something is misbehaving.
           ==================================================================== -->
      <div v-if="queueData?.config" class="card" style="margin-top:14px;">
        <div class="card-header">
          <h3>Configuration</h3>
          <span class="muted">{{ queueData.namespace || '—' }} · {{ queueData.task || '—' }}</span>
        </div>
        <div class="card-body">
          <div class="qd-config-grid">
            <div class="qd-config">
              <span class="label-xs">Lease time</span>
              <span class="qd-config-val font-mono">{{ formatDuration((queueData.config.leaseTime || 0) * 1000) }}</span>
              <span class="qd-config-hint">how long a lease is held</span>
            </div>
            <div class="qd-config">
              <span class="label-xs">TTL</span>
              <span class="qd-config-val font-mono">{{ formatDuration((queueData.config.ttl || 0) * 1000) || '—' }}</span>
              <span class="qd-config-hint">message lifetime</span>
            </div>
            <div class="qd-config">
              <span class="label-xs">Max queue size</span>
              <span class="qd-config-val font-mono">{{ queueData.config.maxQueueSize ? formatNumber(queueData.config.maxQueueSize) : '∞' }}</span>
              <span class="qd-config-hint">push back-pressure cap</span>
            </div>
            <div class="qd-config">
              <span class="label-xs">Retry limit</span>
              <span class="qd-config-val font-mono">{{ queueData.config.retryLimit || 0 }}</span>
              <span class="qd-config-hint">attempts before DLQ / drop</span>
            </div>
            <div class="qd-config">
              <span class="label-xs">Retry delay</span>
              <span class="qd-config-val font-mono">{{ queueData.config.retryDelay || 0 }} ms</span>
              <span class="qd-config-hint">backoff between attempts</span>
            </div>
            <div class="qd-config">
              <span class="label-xs">Dead-letter queue</span>
              <span class="qd-config-val">
                <span class="chip" :class="queueData.config.deadLetterQueue ? 'chip-ice' : 'chip-mute'">
                  {{ queueData.config.deadLetterQueue ? 'enabled' : 'disabled' }}
                </span>
              </span>
              <span class="qd-config-hint">where exhausted retries land</span>
            </div>
            <div class="qd-config">
              <span class="label-xs">Priority</span>
              <span class="qd-config-val font-mono">{{ queueData.priority ?? 0 }}</span>
              <span class="qd-config-hint">scheduler priority</span>
            </div>
            <div class="qd-config">
              <span class="label-xs">Created</span>
              <span class="qd-config-val font-mono">{{ formatDate(queueData.createdAt) }}</span>
              <span class="qd-config-hint">{{ formatRelative(queueData.createdAt) }}</span>
            </div>
          </div>
        </div>
      </div>
    </template>

    <!-- ======================================================================
         Modals
         ====================================================================== -->
    <div
      v-if="showDeleteModal"
      class="qd-modal-backdrop"
      @click="showDeleteModal = false"
    >
      <div class="card animate-slide-up qd-modal" @click.stop>
        <div class="card-header"><h3>Delete queue</h3></div>
        <div class="card-body">
          <p style="color:var(--text-mid);">
            Delete <strong>{{ queueName }}</strong>? This permanently removes the queue,
            all partitions ({{ partitions.length }}), and {{ formatNumber(totalMessages.total) }} messages.
            This action cannot be undone.
          </p>
        </div>
        <div class="qd-modal-foot">
          <button @click="showDeleteModal = false" class="btn btn-ghost">Cancel</button>
          <button @click="deleteQueue" class="btn btn-danger">Delete queue</button>
        </div>
      </div>
    </div>

  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, watch, h } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { analytics, queues as queuesApi, consumers as consumersApi, system as systemApi } from '@/api'
import { formatNumber, formatDuration, toNum, latestFinite, trimIncompleteBuckets } from '@/composables/useApi'
import { useRefresh } from '@/composables/useRefresh'
import MetricRow from '@/components/MetricRow.vue'

const route = useRoute()
const router = useRouter()
const queueName = computed(() => route.params.queueName)

// Inline sort caret — kept inside this file as a tiny render-only component
// so the partitions table doesn't need a new file just for an icon.
const SortIcon = {
  props: { active: Boolean, asc: Boolean },
  setup(p) {
    return () => h('svg', {
      width: 10, height: 10, viewBox: '0 0 16 16',
      fill: 'none', stroke: 'currentColor', 'stroke-width': 1.6,
      style: {
        opacity: p.active ? 1 : 0.3,
        transform: p.active && !p.asc ? 'rotate(180deg)' : undefined,
        transition: 'transform .15s var(--ease)'
      }
    }, [h('path', { d: 'M4 6l4 4 4-4', 'stroke-linecap': 'round', 'stroke-linejoin': 'round' })])
  }
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
const loading = ref(true)
const error = ref(null)

const queueData = ref(null)
const statusData = ref(null)
const opsData = ref(null)
const consumers = ref([])
const laggingPartitionsRaw = ref([])

const loadingOps = ref(true)
const loadingConsumers = ref(true)
const laggingLoading = ref(false)
const laggingLoaded = ref(false)

const partitionSearch = ref('')
const partitionSortColumn = ref('pending')
const partitionSortDirection = ref('desc')

const showDeleteModal = ref(false)

const skippingPartition = ref(null)

const selectedRange = ref('1h')
const timeRanges = [
  { label: '1h',  value: '1h',  minutes: 60 },
  { label: '6h',  value: '6h',  minutes: 360 },
  { label: '24h', value: '24h', minutes: 1440 },
]

const lagThreshold = ref(60)
const lagPresets = [
  { label: '1m',  value: 60 },
  { label: '5m',  value: 300 },
  { label: '1h',  value: 3600 },
  { label: '24h', value: 86400 },
]

const lastRefreshAt = ref(null)
const nowTick = ref(Date.now())
const refreshAgo = computed(() => {
  if (!lastRefreshAt.value) return '—'
  const sec = Math.max(0, Math.floor((nowTick.value - lastRefreshAt.value) / 1000))
  if (sec < 5) return 'just now'
  if (sec < 60) return `${sec}s ago`
  if (sec < 3600) return `${Math.floor(sec / 60)}m ago`
  return `${Math.floor(sec / 3600)}h ago`
})

// Per-row expand state — same Set-based pattern as Dashboard so the
// metric table feels familiar from page to page.
const expandedRows = ref(new Set())
const isExpanded = (key) => expandedRows.value.has(key)
const toggleRow = (key) => {
  const next = new Set(expandedRows.value)
  if (next.has(key)) next.delete(key); else next.add(key)
  expandedRows.value = next
}

// ---------------------------------------------------------------------------
// Time range helpers
// ---------------------------------------------------------------------------
const getTimeRangeParams = () => {
  const r = timeRanges.find(x => x.value === selectedRange.value) || timeRanges[0]
  const now = new Date()
  return {
    from: new Date(now.getTime() - r.minutes * 60 * 1000).toISOString(),
    to: now.toISOString(),
    queue: queueName.value,
  }
}

// History (oldest → newest). The queue-ops endpoint returns one row per
// (bucket, queueName); since we filter to a single queue, that's
// effectively one row per bucket. We sort by bucket ascending so charts
// read left→right, then drop the in-flight current bucket — workers
// flush on minute boundaries, so the latest bucket otherwise paints a
// phantom drop to zero on the right edge of every chart.
const history = computed(() => {
  const series = opsData.value?.series || []
  const sorted = [...series].sort((a, b) => a.bucket.localeCompare(b.bucket))
  return trimIncompleteBuckets(sorted, {
    bucketKey: 'bucket',
    bucketMinutes: opsData.value?.bucketMinutes || 1,
  })
})
const multiDay = computed(() => {
  const h = history.value
  if (h.length < 2) return false
  return new Date(h[0].bucket).toDateString() !== new Date(h[h.length - 1].bucket).toDateString()
})
const formatChartLabel = (date, multi) => multi
  ? date.toLocaleString('en-US', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' })
  : date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' })
const chartLabels = computed(() =>
  history.value.map(r => formatChartLabel(new Date(r.bucket), multiDay.value))
)

// ---------------------------------------------------------------------------
// Partitions (existing logic, retained but enriched)
// ---------------------------------------------------------------------------
const partitions = computed(() => {
  const statusPartitions = statusData.value?.partitions || []
  const queuePartitions = queueData.value?.partitions || []
  if (statusPartitions.length > 0) return statusPartitions
  return queuePartitions.map(p => ({ ...p, messages: p.stats || p.messages || {} }))
})

const totalMessages = computed(() => {
  const apiTotals = statusData.value?.totals
  if (apiTotals) {
    const msgs = apiTotals.messages || apiTotals
    return {
      total: msgs.total || 0,
      pending: Math.max(0, msgs.pending || 0),
      processing: msgs.processing || 0,
      completed: msgs.completed || 0,
      failed: msgs.failed || 0,
      deadLetter: msgs.deadLetter || 0
    }
  }
  const t = { total: 0, pending: 0, processing: 0, completed: 0, failed: 0, deadLetter: 0 }
  for (const p of partitions.value) {
    const m = p.messages || p.stats || {}
    t.total += m.total || 0
    t.pending += m.pending || 0
    t.processing += m.processing || 0
    t.completed += m.completed || 0
    t.failed += m.failed || 0
    t.deadLetter += m.deadLetter || 0
  }
  t.pending = Math.max(0, t.pending)
  return t
})

// ---------------------------------------------------------------------------
// Consumer groups for THIS queue (filtered from the global list)
// ---------------------------------------------------------------------------
const queueConsumers = computed(() =>
  consumers.value
    .filter(c => c.queueName === queueName.value)
    .sort((a, b) => (b.maxTimeLag || 0) - (a.maxTimeLag || 0))
)
const queueConsumersLagging = computed(() =>
  queueConsumers.value.filter(c => (c.maxTimeLag || 0) >= 60 || (c.partitionsWithLag || 0) > 0).length
)

// ---------------------------------------------------------------------------
// Lagging partitions for THIS queue (filtered from cluster-wide list)
// ---------------------------------------------------------------------------
const laggingFiltered = computed(() =>
  laggingPartitionsRaw.value.filter(p => p.queue_name === queueName.value)
)
const skipKey = (p) => `${p.consumer_group}-${p.queue_name}-${p.partition_name}`

// ---------------------------------------------------------------------------
// Now-snapshot (right side of counts strip) — latest *finite* bucket per
// field. Walking back from the tail means a still-aggregating most-recent
// bucket doesn't make every metric read 0 right after a minute boundary.
// ---------------------------------------------------------------------------
const nowSnapshot = computed(() => {
  const h = history.value
  return {
    push:   fmtRate(latestFinite(h.map(x => x.pushPerSecond)) ?? 0),
    pop:    fmtRate(latestFinite(h.map(x => x.popPerSecond))  ?? 0),
    ack:    fmtRate(latestFinite(h.map(x => x.ackPerSecond))  ?? 0),
    parked: formatNumber(Math.round(latestFinite(h.map(x => x.parkedCount)) ?? 0)),
  }
})

// ---------------------------------------------------------------------------
// Throughput row — push / pop / ack rates per bucket
// ---------------------------------------------------------------------------
const throughput = computed(() => {
  // Prefer pop, fall back to push — and use the latest *finite* values
  // so we don't read 0 from a still-flushing tail bucket.
  const h = history.value
  const pop = latestFinite(h.map(x => x.popPerSecond))
  const push = latestFinite(h.map(x => x.pushPerSecond))
  const v = pop ?? push ?? 0
  return { current: v.toFixed(1) }
})
const throughputSeries = computed(() => {
  const h = history.value
  if (!h.length) return null
  return [
    { label: 'Push', data: h.map(x => toNum(x.pushPerSecond)) },
    { label: 'Pop',  data: h.map(x => toNum(x.popPerSecond)) },
    { label: 'Ack',  data: h.map(x => toNum(x.ackPerSecond)), color: '#4ade80' },
  ]
})
const throughputPeak = computed(() => {
  const h = history.value
  if (!h.length) return 0
  // Per-bucket max across the three rates, ignoring nulls.
  const perBucket = h.map(x => {
    const vals = [x.pushPerSecond, x.popPerSecond, x.ackPerSecond]
      .map(toNum).filter(v => v !== null)
    return vals.length ? Math.max(...vals) : null
  }).filter(v => v !== null)
  return perBucket.length ? Math.max(0, ...perBucket) : 0
})
const throughputContext = computed(() => {
  if (throughputPeak.value === 0) return 'idle · no traffic in window'
  return `peak ${formatNumber(Math.round(throughputPeak.value))} /s`
})

// ---------------------------------------------------------------------------
// Pending Δ row — cumulative push − pop across the window
// ---------------------------------------------------------------------------
// Cumulative push − pop. When both source values are missing for a bucket
// we emit null (chart gap); otherwise we still accumulate so the line
// continues from the last known total once data resumes.
const pendingDeltaSeries = computed(() => {
  const h = history.value
  if (!h.length) return []
  let cum = 0
  return h.map(x => {
    const push = toNum(x.pushMessages)
    const pop  = toNum(x.popMessages)
    if (push === null && pop === null) return null
    cum += (push || 0) - (pop || 0)
    return cum
  })
})
const pendingDeltaLatest = computed(() => latestFinite(pendingDeltaSeries.value) ?? 0)
const pendingDeltaDisplay = computed(() => {
  const v = pendingDeltaLatest.value
  if (v === 0) return '0'
  return (v > 0 ? '+' : '−') + formatNumber(Math.abs(v))
})
const pendingDeltaContext = computed(() => {
  const v = pendingDeltaLatest.value
  if (v === 0) return 'flat · push = pop across window'
  if (v > 0) return 'queue filling · push > pop'
  return 'queue draining · pop > push'
})
const pendingDeltaSeverity = computed(() => {
  const v = pendingDeltaLatest.value
  if (v > 100000) return 'bad'
  if (v > 1000) return 'warn'
  if (v < -1000) return 'ok'
  return ''
})

// ---------------------------------------------------------------------------
// Time lag row — avg / max from queue-ops series
// ---------------------------------------------------------------------------
const lagSeriesData = computed(() => {
  const h = history.value
  if (!h.length) return null
  return [
    { label: 'Avg', data: h.map(x => toNum(x.avgLagMs)) },
    { label: 'Max', data: h.map(x => toNum(x.maxLagMs)) },
  ]
})
const lagLatest = computed(() => {
  const h = history.value
  if (!h.length) return { avg: null, max: null }
  // Read the latest finite value for each so a still-aggregating last
  // bucket doesn't make lag appear to drop to 0.
  return {
    avg: latestFinite(h.map(x => x.avgLagMs)),
    max: latestFinite(h.map(x => x.maxLagMs)),
  }
})
const lagNumClass = (ms) => {
  if (ms === null || ms === undefined) return ''
  if (!ms || ms === 0) return ''
  if (ms < 60_000) return ''
  if (ms < 300_000) return 'warn'
  return 'bad'
}
// MetricRow.severity expects a key like 'warn' / 'bad'; map from the raw ms.
const lagSeverityKey = computed(() => lagNumClass(lagLatest.value.max))
const lagContext = computed(() => {
  const h = history.value
  if (!h.length) return '—'
  const finite = h.map(x => toNum(x.maxLagMs)).filter(v => v !== null)
  if (!finite.length) return 'no measured lag in window'
  const peak = Math.max(...finite)
  if (peak === 0) return 'no measured lag in window'
  return `peak max ${fmtLagShort(peak)} · avg-bucket lag`
})

// ---------------------------------------------------------------------------
// Fill ratio row — popMessages / (popMessages + popEmpty)
// ---------------------------------------------------------------------------
const FILL_NOISE_FLOOR = 5
const fillSeriesData = computed(() => {
  const h = history.value
  if (!h.length) return null
  // null source data → emit null (real gap)
  // populated but quiet → carry forward last computed value (continuous line)
  // populated and busy  → compute from pop/(pop+empty)
  let last = null
  const data = h.map(x => {
    const pop = toNum(x.popMessages)
    const empty = toNum(x.popEmpty)
    if (pop === null && empty === null) return null
    const total = (pop || 0) + (empty || 0)
    if (total < FILL_NOISE_FLOOR) return last
    last = Math.round(((pop || 0) / total) * 1000) / 10
    return last
  })
  return [{ label: 'Fill', data }]
})
const fillLatest = computed(() => {
  const tail = history.value.slice(-5)
  let pop = 0, empty = 0
  for (const r of tail) {
    pop += toNum(r.popMessages) || 0
    empty += toNum(r.popEmpty) || 0
  }
  if (pop + empty < FILL_NOISE_FLOOR) return null
  return Math.round((pop / (pop + empty)) * 1000) / 10
})
const fillContext = computed(() => {
  const v = fillLatest.value
  if (v === null) return 'idle window · no long-poll activity'
  if (v >= 80) return 'high utilization · consumers busy serving'
  if (v < 30)  return 'low utilization · consumers mostly empty'
  return 'balanced · consumer pool sized OK'
})
const fillSeverityKey = computed(() => {
  const v = fillLatest.value
  if (v === null) return ''
  if (v < 30) return 'warn'
  return ''
})

// ---------------------------------------------------------------------------
// Errors row — ack failed in window for this queue
// ---------------------------------------------------------------------------
const errorsSeries = computed(() => {
  const h = history.value
  if (!h.length) return []
  return h.map(x => toNum(x.ackFailed))
})
const errorsTotal = computed(() =>
  history.value.reduce((s, x) => s + (toNum(x.ackFailed) || 0), 0)
)
const errorsContext = computed(() => {
  const t = errorsTotal.value
  if (!t) return 'no ack failures in window'
  return `${formatNumber(t)} ack failure${t === 1 ? '' : 's'}`
})
const errorsSeverity = computed(() => {
  const t = errorsTotal.value
  if (t > 100) return 'bad'
  if (t > 0) return 'warn'
  return ''
})

// ---------------------------------------------------------------------------
// Parked row — long-poll consumers waiting on this queue
// ---------------------------------------------------------------------------
const parkedSeries = computed(() => history.value.map(x => toNum(x.parkedCount)))
const parkedLatest = computed(() => latestFinite(history.value.map(x => x.parkedCount)) ?? 0)
const parkedAvg = computed(() => {
  const finite = history.value.map(x => toNum(x.parkedCount)).filter(v => v !== null)
  if (!finite.length) return 0
  return finite.reduce((s, v) => s + v, 0) / finite.length
})
const parkedContext = computed(() => {
  if (!history.value.length) return '—'
  return `avg ${parkedAvg.value.toFixed(1)} across window`
})

// ---------------------------------------------------------------------------
// Partitions Δ row — created / deleted in window
// ---------------------------------------------------------------------------
const partitionOpsSeries = computed(() => {
  const h = history.value
  if (!h.length) return null
  return [
    { label: 'Created', data: h.map(x => toNum(x.partitionsCreated)) },
    { label: 'Deleted', data: h.map(x => toNum(x.partitionsDeleted)) },
  ]
})
const partitionCreatedTotal = computed(() =>
  history.value.reduce((s, x) => s + (toNum(x.partitionsCreated) || 0), 0)
)
const partitionDeletedTotal = computed(() =>
  history.value.reduce((s, x) => s + (toNum(x.partitionsDeleted) || 0), 0)
)

// ---------------------------------------------------------------------------
// Consumed row — lifetime cursor consumed (total) + per-bucket pop sparkline
// ---------------------------------------------------------------------------
const consumedTotal = computed(() =>
  partitions.value.reduce((s, p) => s + (toNum(p.cursor?.totalConsumed) || 0), 0)
)
const popSeries = computed(() => history.value.map(x => toNum(x.popMessages)))
const consumedContext = computed(() => {
  const groups = queueConsumers.value.length
  return groups > 0
    ? `lifetime · ${groups} consumer ${groups === 1 ? 'group' : 'groups'}`
    : 'lifetime · no consumer groups'
})

// ---------------------------------------------------------------------------
// Banner derivation — only the worst signals show, ordered by severity.
// ---------------------------------------------------------------------------
const banners = computed(() => {
  const out = []
  if (totalMessages.value.deadLetter > 0) {
    out.push({
      tone: 'bad',
      title: 'Messages in DLQ',
      detail: `${formatNumber(totalMessages.value.deadLetter)} message${totalMessages.value.deadLetter === 1 ? '' : 's'} require investigation.`,
      cta: 'Open DLQ',
      action: goDLQ,
    })
  }
  if (laggingFiltered.value.length > 0) {
    out.push({
      tone: 'warn',
      title: `${laggingFiltered.value.length} lagging partition${laggingFiltered.value.length === 1 ? '' : 's'}`,
      detail: `Above ${getLagLabel(lagThreshold.value)} threshold. Inspect per-partition status below.`,
    })
  }
  if (errorsTotal.value > 0) {
    out.push({
      tone: errorsSeverity.value === 'bad' ? 'bad' : 'warn',
      title: 'Ack failures in window',
      detail: `${formatNumber(errorsTotal.value)} ack failure${errorsTotal.value === 1 ? '' : 's'} across the last ${selectedRange.value}.`,
    })
  }
  if (totalMessages.value.pending > 10000) {
    out.push({
      tone: totalMessages.value.pending > 100000 ? 'bad' : 'warn',
      title: 'High pending depth',
      detail: `${formatNumber(totalMessages.value.pending)} pending — consider scaling consumers or checking lag.`,
    })
  }
  return out
})

// ---------------------------------------------------------------------------
// Partition severity / sort
// ---------------------------------------------------------------------------
const pendingWarn = (p) => {
  const pending = Math.max(0, p.messages?.pending || 0)
  return pending >= 1000 && pending < 10000
}
const pendingBad = (p) => {
  const pending = Math.max(0, p.messages?.pending || 0)
  return pending >= 10000
}
const partitionStatus = (p) => {
  if ((p.messages?.deadLetter || 0) > 0 || pendingBad(p)) return 'bad'
  if (pendingWarn(p)) return 'warn'
  // Stale partitions (no recent activity but have pending) → watch
  if ((p.messages?.pending || 0) > 0 && p.lastActivity) {
    const ageMin = (Date.now() - new Date(p.lastActivity).getTime()) / 60000
    if (ageMin > 30) return 'warn'
  }
  return p.messages?.pending > 0 ? 'ok' : 'idle'
}
const partitionDotClass = (p) => {
  const s = partitionStatus(p)
  if (s === 'bad') return 'status-dot-danger'
  if (s === 'warn') return 'status-dot-warning'
  if (s === 'ok') return 'status-dot-success'
  return 'qd-dot-idle'
}
const partitionStatusText = (p) => {
  const s = partitionStatus(p)
  if (s === 'bad') return 'Saturated or has DLQ'
  if (s === 'warn') return 'Pending depth elevated'
  if (s === 'ok') return 'Active'
  return 'Idle'
}

const oldestClass = (ts) => {
  if (!ts) return ''
  const ageMin = (Date.now() - new Date(ts).getTime()) / 60000
  if (ageMin > 60) return 'num bad'
  if (ageMin > 5) return 'num warn'
  return 'num'
}

const sortedPartitions = computed(() => {
  let result = [...partitions.value]
  if (partitionSearch.value) {
    const q = partitionSearch.value.toLowerCase()
    result = result.filter(p => p.name.toLowerCase().includes(q))
  }
  const dir = partitionSortDirection.value === 'asc' ? 1 : -1
  result.sort((a, b) => {
    const k = partitionSortColumn.value
    const A = a, B = b
    let av, bv
    switch (k) {
      case 'name':
        av = a.name?.toLowerCase() || ''; bv = b.name?.toLowerCase() || ''
        return av.localeCompare(bv) * dir
      case 'pending': av = a.messages?.pending || 0; bv = b.messages?.pending || 0; break
      case 'processing': av = a.messages?.processing || 0; bv = b.messages?.processing || 0; break
      case 'completed': av = a.messages?.completed || 0; bv = b.messages?.completed || 0; break
      case 'deadLetter': av = a.messages?.deadLetter || 0; bv = b.messages?.deadLetter || 0; break
      case 'consumed': av = a.cursor?.totalConsumed || 0; bv = b.cursor?.totalConsumed || 0; break
      case 'oldest':
        av = a.oldestMessage ? new Date(a.oldestMessage).getTime() : (dir === 1 ? Infinity : -Infinity)
        bv = b.oldestMessage ? new Date(b.oldestMessage).getTime() : (dir === 1 ? Infinity : -Infinity)
        break
      case 'lastActivity':
        av = a.lastActivity ? new Date(a.lastActivity).getTime() : (dir === 1 ? Infinity : -Infinity)
        bv = b.lastActivity ? new Date(b.lastActivity).getTime() : (dir === 1 ? Infinity : -Infinity)
        break
      default: return 0
    }
    return (av - bv) * dir
  })
  return result
})
const sortPartitions = (column) => {
  if (partitionSortColumn.value === column) {
    partitionSortDirection.value = partitionSortDirection.value === 'asc' ? 'desc' : 'asc'
  } else {
    partitionSortColumn.value = column
    partitionSortDirection.value = column === 'name' ? 'asc' : 'desc'
  }
}

// ---------------------------------------------------------------------------
// Consumer-group dot helper (mirrors Dashboard rules)
// ---------------------------------------------------------------------------
const cgDotClass = (g) => {
  const lag = g.maxTimeLag || 0
  if (lag >= 300) return 'status-dot-danger'
  if (lag >= 60 || (g.partitionsWithLag || 0) > 0) return 'status-dot-warning'
  return 'status-dot-success'
}

// ---------------------------------------------------------------------------
// Numeric tone for top-strip pending
// ---------------------------------------------------------------------------
const pendingNumClass = (n) => {
  if (!n || n < 1000) return ''
  if (n < 10000) return 'warn'
  return 'bad'
}

// ---------------------------------------------------------------------------
// Lag-secs tone (used by lagging-partitions list and consumer-group right metric)
// ---------------------------------------------------------------------------
const lagSecClass = (s) => {
  if (!s) return ''
  if (s < 60) return ''
  if (s < 600) return 'warn'
  return 'bad'
}

// ---------------------------------------------------------------------------
// Formatters
// ---------------------------------------------------------------------------
const fmtRate = (n) => {
  const v = Number(n) || 0
  if (Math.abs(v) >= 1000) return (v / 1000).toFixed(1) + 'k'
  if (Math.abs(v) >= 100) return Math.round(v).toString()
  if (Math.abs(v) >= 10) return v.toFixed(1)
  return v.toFixed(2)
}
const fmtCount = (n) => formatNumber(Math.round(Number(n) || 0))
const fmtLagMs = (n) => {
  const v = Number(n) || 0
  if (v < 1) return '0'
  if (v < 1000) return Math.round(v) + ' ms'
  if (v < 60000) return (v / 1000).toFixed(1) + ' s'
  return (v / 60000).toFixed(1) + ' m'
}
function fmtLagShort(ms) {
  if (ms === null || ms === undefined) return '—'
  if (ms === 0) return '0'
  if (ms < 1000) return Math.round(ms) + 'ms'
  if (ms < 60000) return (ms / 1000).toFixed(1) + 's'
  if (ms < 3600000) return (ms / 60000).toFixed(1) + 'm'
  return (ms / 3600000).toFixed(1) + 'h'
}
const fmtFillPct = (n) => {
  if (n === null || n === undefined) return '—'
  const v = Number(n)
  if (!Number.isFinite(v)) return '—'
  return v.toFixed(1) + '%'
}
const formatDate = (ts) => {
  if (!ts) return '—'
  return new Date(ts).toLocaleString('en-US', {
    month: 'short', day: 'numeric', year: 'numeric',
    hour: '2-digit', minute: '2-digit'
  })
}
const formatRelative = (ts) => {
  if (!ts) return '—'
  const diffMs = Date.now() - new Date(ts).getTime()
  if (diffMs < 0) return 'just now'
  if (diffMs < 60_000) return `${Math.floor(diffMs / 1000)}s ago`
  if (diffMs < 3600_000) return `${Math.floor(diffMs / 60_000)}m ago`
  if (diffMs < 86400_000) return `${Math.floor(diffMs / 3600_000)}h ago`
  if (diffMs < 30 * 86400_000) return `${Math.floor(diffMs / 86400_000)}d ago`
  return formatDate(ts)
}
const formatDurationSec = (sec) => formatDuration(Math.round((sec || 0) * 1000))
const getLagLabel = (seconds) => {
  if (seconds < 60) return `${seconds}s`
  if (seconds < 3600) return `${Math.round(seconds / 60)}m`
  if (seconds < 86400) return `${Math.round(seconds / 3600)}h`
  return `${Math.round(seconds / 86400)}d`
}

// ---------------------------------------------------------------------------
// Navigation helpers
// ---------------------------------------------------------------------------
function goMessages() {
  router.push({ path: '/messages', query: { queue: queueName.value } })
}
function goDLQ() {
  router.push({ path: '/dlq', query: { queue: queueName.value } })
}
function goTraces() {
  router.push({ path: '/traces', query: { queue: queueName.value } })
}

// ---------------------------------------------------------------------------
// Fetchers
// ---------------------------------------------------------------------------
const fetchQueueDetail = async () => {
  try {
    const [queueResponse, statusResponse] = await Promise.all([
      queuesApi.get(queueName.value).catch(() => null),
      analytics.getQueueDetail(queueName.value).catch(() => null),
    ])
    const rawQueue = queueResponse?.data || null
    const rawStatus = statusResponse?.data || null

    if (rawStatus?.queue) {
      queueData.value = { ...rawStatus.queue, config: rawStatus.queue.config }
    } else if (rawQueue) {
      queueData.value = rawQueue
    } else {
      queueData.value = null
    }
    statusData.value = rawStatus
    if (!queueData.value && !statusData.value) error.value = 'Queue not found'
  } catch (err) {
    error.value = err.response?.data?.error || err.message
  }
}

const fetchOps = async () => {
  if (!opsData.value) loadingOps.value = true
  try {
    const r = await systemApi.getQueueOps(getTimeRangeParams())
    opsData.value = r.data
  } catch {
    opsData.value = null
  } finally {
    loadingOps.value = false
  }
}

const fetchConsumers = async () => {
  if (!consumers.value.length) loadingConsumers.value = true
  try {
    const r = await consumersApi.list()
    consumers.value = Array.isArray(r.data) ? r.data : (r.data?.consumer_groups || [])
  } catch {
    consumers.value = []
  } finally {
    loadingConsumers.value = false
  }
}

const loadLaggingPartitions = async () => {
  laggingLoading.value = true
  try {
    const r = await consumersApi.getLagging(lagThreshold.value)
    laggingPartitionsRaw.value = Array.isArray(r.data) ? r.data : (r.data || [])
  } catch {
    laggingPartitionsRaw.value = []
  } finally {
    laggingLoading.value = false
    laggingLoaded.value = true
  }
}

const fetchAll = async () => {
  await Promise.all([fetchQueueDetail(), fetchOps(), fetchConsumers(), loadLaggingPartitions()])
  lastRefreshAt.value = Date.now()
  loading.value = false
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
const deleteQueue = async () => {
  try {
    await queuesApi.delete(queueName.value)
    showDeleteModal.value = false
    router.push('/queues')
  } catch (err) {
    console.error('Failed to delete queue:', err)
  }
}
const handleSkipPartition = async (p) => {
  const key = skipKey(p)
  const groupLabel = p.consumer_group === '__QUEUE_MODE__' ? '(queue mode)' : p.consumer_group
  if (!confirm(`Skip "${p.partition_name}" to end for "${groupLabel}"?\n\nThis advances the cursor past all pending messages on this partition.`)) return
  skippingPartition.value = key
  try {
    await consumersApi.seekPartition(p.consumer_group, p.queue_name, p.partition_name)
    await loadLaggingPartitions()
  } catch (err) {
    console.error('Failed to skip partition:', err)
    alert('Failed to skip partition: ' + (err.response?.data?.error || err.message))
  } finally {
    skippingPartition.value = null
  }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
useRefresh(fetchAll)
watch(selectedRange, fetchOps)
watch(queueName, () => {
  loading.value = true
  queueData.value = null
  statusData.value = null
  opsData.value = null
  fetchAll()
})

let interval = null
let tickInterval = null
onMounted(() => {
  fetchAll()
  interval = setInterval(fetchAll, 30000)
  tickInterval = setInterval(() => { nowTick.value = Date.now() }, 1000)
})
onUnmounted(() => {
  if (interval) clearInterval(interval)
  if (tickInterval) clearInterval(tickInterval)
})
</script>

<style scoped>
/* ---------------------------------------------------------------------------
   Detail bar — top row: back · name · meta · spacer · range · live
   --------------------------------------------------------------------------- */
.qd-bar {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 6px 0 12px;
  border-bottom: 1px solid var(--bd);
  margin-bottom: 12px;
  flex-wrap: wrap;
}
.qd-bar-spacer { flex: 1 1 auto; }
.qd-live {
  display: flex; align-items: center; gap: 8px;
  font-size: 11px; font-family: 'JetBrains Mono', monospace;
  color: var(--text-mid);
}

/* Quick-action row */
.qd-actions {
  display: flex; align-items: center; gap: 8px;
  margin-bottom: 14px;
  flex-wrap: wrap;
}
.qd-actions-spacer { flex: 1 1 auto; }
.qd-badge {
  display: inline-flex; align-items: center;
  padding: 0 5px; border-radius: 99px;
  font-size: 10px; font-family: 'JetBrains Mono', monospace;
  margin-left: 4px;
}
.qd-badge-bad {
  background: var(--ember-glow); color: var(--ember-400);
  border: 1px solid rgba(244,63,94,.25);
}

/* ---------------------------------------------------------------------------
   Banner row — only renders when something needs flagging
   --------------------------------------------------------------------------- */
.qd-banners {
  display: flex; flex-direction: column; gap: 6px;
  margin-bottom: 12px;
}
.qd-banner {
  display: flex; align-items: center; gap: 10px;
  padding: 9px 14px;
  border: 1px solid var(--bd);
  border-radius: 6px;
  font-size: 12.5px;
  background: var(--ink-2);
  cursor: default;
  transition: border-color .12s var(--ease);
}
.qd-banner:has(> .qd-banner-cta) { cursor: pointer; }
.qd-banner:has(> .qd-banner-cta):hover { border-color: var(--bd-hi); }
.qd-banner-warn {
  background: rgba(230,180,80,.06);
  border-color: rgba(230,180,80,.22);
}
.qd-banner-bad {
  background: rgba(244,63,94,.06);
  border-color: rgba(244,63,94,.25);
}
.qd-banner-dot {
  width: 7px; height: 7px; border-radius: 99px;
  flex-shrink: 0;
}
.qd-banner-dot-warn { background: var(--warn-400); }
.qd-banner-dot-bad  { background: var(--ember-400); }
.qd-banner-title { font-weight: 600; color: var(--text-hi); }
.qd-banner-detail { color: var(--text-mid); }
.qd-banner-cta {
  margin-left: auto;
  font-family: 'JetBrains Mono', monospace;
  font-size: 11px; color: var(--text-mid);
}
.qd-banner-warn .qd-banner-title { color: var(--warn-400); }
.qd-banner-bad  .qd-banner-title { color: var(--ember-400); }

/* ---------------------------------------------------------------------------
   Counts strip — same look as Dashboard's, scoped to this queue
   --------------------------------------------------------------------------- */
.counts-strip {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 14px 24px;
  padding: 12px 16px;
  margin-bottom: 14px;
  border: 1px solid var(--bd);
  background: var(--ink-2);
  border-radius: 8px;
}
.counts-group {
  display: flex; align-items: baseline;
  flex-wrap: wrap; gap: 10px;
}
.counts-group-right { color: var(--text-low); }
.count-item-label {
  font-family: 'JetBrains Mono', monospace;
  font-size: 10.5px; letter-spacing: .08em;
  text-transform: uppercase; color: var(--text-low);
  margin-right: 2px;
}
.count-item {
  display: inline-flex; align-items: baseline; gap: 6px;
  background: transparent; border: none; padding: 0;
  color: var(--text-mid);
  font-size: 12.5px;
  cursor: pointer;
  transition: color .12s var(--ease);
}
.count-item.count-static { cursor: default; }
.count-item.count-tight { gap: 4px; }
.count-item:not(:disabled):not(.count-static):hover { color: var(--text-hi); }
.count-item:not(:disabled):not(.count-static):hover strong { color: var(--accent); }
.count-item:disabled { opacity: .5; cursor: default; }
.count-item strong {
  font-family: 'JetBrains Mono', monospace;
  font-variant-numeric: tabular-nums;
  font-size: 14px; font-weight: 600;
  color: var(--text-hi);
  letter-spacing: -.005em;
}
.counts-group-right .count-item strong {
  color: var(--text-mid);
  font-size: 13px;
}
.count-suffix {
  font-size: 10.5px; font-family: 'JetBrains Mono', monospace;
  color: var(--text-low); letter-spacing: .04em;
  text-transform: uppercase;
}
.count-muted strong { color: var(--text-mid); }
.count-sep {
  color: var(--bd-hi); font-size: 11px;
  user-select: none;
}

/* ---------------------------------------------------------------------------
   Metric table — replicated from Dashboard so the visual language is
   consistent: a row of dot · label · value · context · sparkline · expand.
   --------------------------------------------------------------------------- */
.metric-table {
  border: 1px solid var(--bd);
  background: var(--ink-2);
  border-radius: 8px;
  overflow: hidden;
  margin-bottom: 14px;
}
.metric-head {
  display: grid;
  grid-template-columns: 14px 150px 160px 1fr 260px 24px;
  gap: 16px;
  padding: 9px 16px;
  font-size: 10.5px;
  font-weight: 600;
  letter-spacing: .08em;
  text-transform: uppercase;
  color: var(--text-low);
  border-bottom: 1px solid var(--bd);
  background: rgba(255, 255, 255, .015);
}
.metric-head .h-value { text-align: right; }
.metric-head .h-spark {
  text-align: left;
  font-family: 'JetBrains Mono', monospace;
  letter-spacing: .04em; text-transform: lowercase;
}
@media (max-width: 1100px) {
  .metric-head {
    grid-template-columns: 14px 140px 140px 1fr 180px 24px;
    gap: 12px;
  }
}
@media (max-width: 900px) {
  .metric-head {
    grid-template-columns: 14px 1fr auto 110px 24px;
  }
  .metric-head > :nth-child(4) { display: none; }
}
:deep(.mr-sep) { color: var(--text-low); margin: 0 4px; font-weight: 400; }
:deep(.mr-unit) {
  font-style: normal; color: var(--text-low);
  margin-left: 3px; font-size: 11px; font-weight: 400;
}

/* ---------------------------------------------------------------------------
   Two-column entity panels (Dashboard idiom, reused verbatim)
   --------------------------------------------------------------------------- */
.entity-list {
  display: flex; flex-direction: column;
  gap: 6px; padding: 10px 10px 6px;
}
.entity-row {
  display: block; width: 100%;
  text-align: left;
  border: 1px solid var(--bd); border-radius: 8px;
  background: rgba(255, 255, 255, .012);
  padding: 9px 12px 10px;
  cursor: pointer;
  transition: border-color .12s var(--ease), background .12s var(--ease);
}
.entity-row-static { cursor: default; }
.entity-row:not(.entity-row-static):hover { border-color: var(--bd-hi); background: rgba(255, 255, 255, .03); }
.entity-head {
  display: flex; align-items: center; gap: 8px;
}
.entity-name {
  flex: 1;
  font-size: 13.5px; font-weight: 500;
  color: var(--text-hi); letter-spacing: -.005em;
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
}
.entity-right {
  font-family: 'JetBrains Mono', monospace;
  font-variant-numeric: tabular-nums;
  font-size: 12px; color: var(--text-mid);
  white-space: nowrap; flex-shrink: 0;
}
.entity-meta {
  display: flex; align-items: center;
  gap: 10px; margin-top: 6px;
  padding-left: 14px;
}
.meta-text {
  font-family: 'JetBrains Mono', monospace;
  font-size: 11px; color: var(--text-low);
  white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
}
.meta-text strong { color: var(--text-mid); font-weight: 600; }
.meta-sep { color: var(--bd-hi); margin: 0 4px; }
.meta-tag {
  display: inline-block;
  padding: 1px 6px; border-radius: 99px;
  border: 1px solid var(--bd);
  background: var(--ink-3); color: var(--text-mid);
  font-size: 10px; font-weight: 500;
  letter-spacing: .02em; margin-right: 2px;
}
.entity-empty {
  padding: 32px 16px; text-align: center;
  color: var(--text-low); font-size: 13px;
}
.card-foot {
  border-top: 1px solid var(--bd);
  padding: 8px 14px;
  display: flex; justify-content: flex-end;
}
.card-foot-link {
  font-size: 11.5px; color: var(--text-mid);
  cursor: pointer; transition: color .12s var(--ease);
}
.card-foot-link:hover { color: var(--text-hi); }

/* ---------------------------------------------------------------------------
   Partitions table — a sortable, dense list of every partition.
   --------------------------------------------------------------------------- */
.qd-parts th { white-space: nowrap; }
.qd-sortable { cursor: pointer; user-select: none; }
.qd-sortable:hover { color: var(--text-hi); }
.qd-th-inner { display: flex; align-items: center; gap: 4px; }
.qd-pdot { width: 16px; padding-right: 0; }
.qd-pname {
  font-weight: 500;
  color: var(--text-hi);
  font-size: 12.5px;
}
.qd-dot-idle { background: var(--text-faint); opacity: .55; }

/* ---------------------------------------------------------------------------
   Configuration grid
   --------------------------------------------------------------------------- */
.qd-config-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 14px 24px;
}
@media (max-width: 1100px) {
  .qd-config-grid { grid-template-columns: repeat(3, 1fr); }
}
@media (max-width: 900px) {
  .qd-config-grid { grid-template-columns: repeat(2, 1fr); }
}
@media (max-width: 500px) {
  .qd-config-grid { grid-template-columns: 1fr; }
}
.qd-config {
  display: flex; flex-direction: column; gap: 4px;
  padding: 10px 0;
}
.qd-config-val {
  font-size: 14px; font-weight: 600;
  color: var(--text-hi); letter-spacing: -.005em;
}
.qd-config-hint {
  font-size: 10.5px; color: var(--text-low);
  letter-spacing: .04em;
}

/* ---------------------------------------------------------------------------
   Modals (kept local so the page is self-contained)
   --------------------------------------------------------------------------- */
.qd-modal-backdrop {
  position: fixed; inset: 0; z-index: 50;
  display: flex; align-items: center; justify-content: center;
  padding: 16px;
  background: rgba(0,0,0,.5);
  backdrop-filter: blur(4px);
}
.qd-modal {
  width: 100%; max-width: 460px;
}
.qd-modal-foot {
  padding: 14px 16px;
  border-top: 1px solid var(--bd);
  display: flex; align-items: center;
  justify-content: flex-end; gap: 12px;
}
</style>
