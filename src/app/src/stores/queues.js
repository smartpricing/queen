import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { api } from '../utils/api'

export const useQueuesStore = defineStore('queues', () => {
  const queues = ref([])
  const loading = ref(false)
  const error = ref(null)
  const selectedQueue = ref(null)

  const globalStats = computed(() => {
    const stats = {
      pending: 0,
      processing: 0,
      completed: 0,
      failed: 0,
      deadLetter: 0,
      total: 0
    }
    
    queues.value.forEach(queue => {
      if (queue.stats) {
        stats.pending += queue.stats.pending || 0
        stats.processing += queue.stats.processing || 0
        stats.completed += queue.stats.completed || 0
        stats.failed += queue.stats.failed || 0
        stats.deadLetter += queue.stats.deadLetter || 0
        stats.total += queue.stats.total || 0
      }
    })
    
    return stats
  })

  const queuesByNamespace = computed(() => {
    const grouped = {}
    queues.value.forEach(queue => {
      const [ns, task, queueName] = queue.queue.split('/')
      if (!grouped[ns]) {
        grouped[ns] = {}
      }
      if (!grouped[ns][task]) {
        grouped[ns][task] = []
      }
      grouped[ns][task].push({
        ...queue,
        name: queueName,
        namespace: ns,
        task: task
      })
    })
    return grouped
  })

  async function fetchQueues() {
    loading.value = true
    error.value = null
    try {
      const response = await api.analytics.getQueues()
      queues.value = response.queues || []
    } catch (err) {
      error.value = err.message
      console.error('Failed to fetch queues:', err)
    } finally {
      loading.value = false
    }
  }

  async function fetchQueueDetails(queuePath) {
    try {
      const [ns, task, queue] = queuePath.split('/')
      const response = await api.analytics.getQueueStats(ns, task, queue)
      selectedQueue.value = response
      return response
    } catch (err) {
      error.value = err.message
      console.error('Failed to fetch queue details:', err)
      throw err
    }
  }

  async function configureQueue(config) {
    try {
      const response = await api.configure(config)
      await fetchQueues() // Refresh list
      return response
    } catch (err) {
      error.value = err.message
      throw err
    }
  }

  function updateQueueDepth(queuePath, depth) {
    const queue = queues.value.find(q => q.queue === queuePath)
    if (queue && queue.stats) {
      queue.stats.pending = depth
    }
  }

  return {
    queues,
    loading,
    error,
    selectedQueue,
    globalStats,
    queuesByNamespace,
    fetchQueues,
    fetchQueueDetails,
    configureQueue,
    updateQueueDepth
  }
})
