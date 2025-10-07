import { defineStore } from 'pinia'
import { ref } from 'vue'
import { api } from '../utils/api'

export const useMessagesStore = defineStore('messages', () => {
  const messages = ref([])
  const recentActivity = ref([])
  const loading = ref(false)
  const error = ref(null)

  async function pushMessage(items) {
    try {
      const response = await api.push({ items })
      addActivity({
        type: 'push',
        count: response.messages.length,
        timestamp: new Date()
      })
      return response
    } catch (err) {
      error.value = err.message
      throw err
    }
  }

  async function popMessages(scope, options = {}) {
    try {
      const response = await api.pop(scope, options)
      if (response.messages?.length > 0) {
        addActivity({
          type: 'pop',
          count: response.messages.length,
          queue: `${scope.ns}/${scope.task}/${scope.queue}`,
          timestamp: new Date()
        })
      }
      return response
    } catch (err) {
      error.value = err.message
      throw err
    }
  }

  async function acknowledgeMessage(transactionId, status, errorMsg) {
    try {
      const response = await api.ack(transactionId, status, errorMsg)
      addActivity({
        type: 'ack',
        status,
        transactionId,
        timestamp: new Date()
      })
      return response
    } catch (err) {
      error.value = err.message
      throw err
    }
  }

  function addActivity(event) {
    recentActivity.value.unshift({
      ...event,
      id: Date.now() + Math.random()
    })
    // Keep only last 100 activities
    if (recentActivity.value.length > 100) {
      recentActivity.value = recentActivity.value.slice(0, 100)
    }
  }

  function handleWebSocketMessage(event) {
    addActivity({
      type: event.event,
      data: event.data,
      timestamp: new Date(event.timestamp)
    })
  }

  async function fetchRecentMessages(filters = {}) {
    loading.value = true
    try {
      const response = await api.messages.list(filters)
      messages.value = response.messages || []
      return messages.value
    } catch (err) {
      error.value = err.message
      console.error('Failed to fetch messages:', err)
      return []
    } finally {
      loading.value = false
    }
  }

  return {
    messages,
    recentActivity,
    loading,
    error,
    pushMessage,
    popMessages,
    acknowledgeMessage,
    addActivity,
    handleWebSocketMessage,
    fetchRecentMessages
  }
})
