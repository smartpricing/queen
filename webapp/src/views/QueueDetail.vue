<template>
  <div class="p-4 sm:p-6">
    <div class="space-y-4 sm:space-y-6 max-w-7xl mx-auto">
      <LoadingSpinner v-if="loading && !queueData" />

      <div v-else-if="error" class="card bg-red-50 dark:bg-red-900/20 text-red-600 text-sm">
        <p><strong>Error loading queue:</strong> {{ error }}</p>
      </div>

      <template v-else-if="queueData">
        <!-- Header -->
        <QueueDetailHeader
          :queue="statusData?.queue"
          @push-message="showPushModal = true"
          @clear-queue="confirmClear"
        />

        <!-- Metrics -->
        <div class="grid grid-cols-2 lg:grid-cols-4 gap-3 sm:gap-4">
          <MetricCard
            title="Pending"
            :value="calculatedPending"
          />
          <MetricCard
            title="Processing"
            :value="statusData?.totals?.messages?.processing || 0"
          />
          <MetricCard
            title="Completed"
            :value="statusData?.totals?.messages?.completed || 0"
          />
          <MetricCard
            title="Failed"
            :value="statusData?.totals?.messages?.failed || 0"
          />
        </div>

        <!-- Queue Config -->
        <QueueConfig :config="statusData?.queue?.config" />

        <!-- Partitions -->
        <PartitionList :partitions="statusData?.partitions" />

        <!-- Recent Messages -->
        <RecentMessages
          :messages="recentMessages?.messages"
          @message-click="viewMessage"
        />
      </template>

      <!-- Push Message Modal -->
      <PushMessageModal
        :is-open="showPushModal"
        :queue-name="queueName"
        @close="showPushModal = false"
        @pushed="onMessagePushed"
      />

      <!-- Clear Queue Confirmation -->
      <ConfirmDialog
        :is-open="showClearConfirm"
        title="Clear Queue"
        :message="`Are you sure you want to clear all messages from '${queueName}'? This action cannot be undone.`"
        confirm-text="Clear Queue"
        confirm-class="btn-danger"
        @confirm="clearQueue"
        @cancel="showClearConfirm = false"
      />
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { queuesApi } from '../api/queues';
import { analyticsApi } from '../api/analytics';

import LoadingSpinner from '../components/common/LoadingSpinner.vue';
import MetricCard from '../components/common/MetricCard.vue';
import ConfirmDialog from '../components/common/ConfirmDialog.vue';
import QueueDetailHeader from '../components/queue-detail/QueueDetailHeader.vue';
import QueueConfig from '../components/queue-detail/QueueConfig.vue';
import PartitionList from '../components/queue-detail/PartitionList.vue';
import RecentMessages from '../components/queue-detail/RecentMessages.vue';
import PushMessageModal from '../components/queue-detail/PushMessageModal.vue';

const route = useRoute();
const router = useRouter();
const queueName = computed(() => route.params.queueName);

const loading = ref(false);
const error = ref(null);
const queueData = ref(null);
const statusData = ref(null);
const recentMessages = ref(null);

const showPushModal = ref(false);
const showClearConfirm = ref(false);

// Calculate pending
const calculatedPending = computed(() => {
  if (!statusData.value?.totals?.messages) return 0;
  
  const total = queueData.value?.totals?.total || 0;
  const completed = statusData.value.totals.messages.completed || 0;
  const failed = statusData.value.totals.messages.failed || 0;
  const processing = statusData.value.totals.messages.processing || 0;
  
  return Math.max(0, total - completed - failed - processing);
});

async function loadData() {
  loading.value = true;
  error.value = null;
  
  try {
    const [queueRes, statusRes, messagesRes] = await Promise.all([
      queuesApi.getQueue(queueName.value),
      analyticsApi.getQueueDetail(queueName.value),
      analyticsApi.getQueueMessages(queueName.value, { limit: 10 }),
    ]);
    
    queueData.value = queueRes.data;
    statusData.value = statusRes.data;
    recentMessages.value = messagesRes.data;
  } catch (err) {
    error.value = err.message;
    console.error('Queue detail error:', err);
  } finally {
    loading.value = false;
  }
}

function confirmClear() {
  showClearConfirm.value = true;
}

async function clearQueue() {
  try {
    await queuesApi.clearQueue(queueName.value);
    showClearConfirm.value = false;
    await loadData();
  } catch (err) {
    error.value = err.message;
  }
}

async function onMessagePushed() {
  await loadData();
}

function viewMessage(message) {
  // Navigate to messages page with this message selected
  router.push(`/messages?transactionId=${message.transactionId}`);
}

onMounted(() => {
  loadData();
});
</script>
