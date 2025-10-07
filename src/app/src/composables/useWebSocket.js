import { ref, onUnmounted } from 'vue'
import { useToast } from 'primevue/usetoast'
import { useQueuesStore } from '../stores/queues'
import { useMessagesStore } from '../stores/messages'

const WS_URL = import.meta.env.VITE_WS_URL || 'ws://localhost:6632/ws/dashboard'

let ws = null
const isConnected = ref(false)
const reconnecting = ref(false)
let reconnectTimer = null
let pingInterval = null

export function useWebSocket() {
  const toast = useToast()
  const queuesStore = useQueuesStore()
  const messagesStore = useMessagesStore()

  function connect() {
    if (ws && ws.readyState === WebSocket.OPEN) {
      return
    }

    try {
      ws = new WebSocket(WS_URL)

      ws.onopen = () => {
        console.log('WebSocket connected')
        isConnected.value = true
        reconnecting.value = false
        
        // Clear reconnect timer
        if (reconnectTimer) {
          clearTimeout(reconnectTimer)
          reconnectTimer = null
        }
        
        // Start ping interval
        pingInterval = setInterval(() => {
          if (ws.readyState === WebSocket.OPEN) {
            ws.send('ping')
          }
        }, 30000)
        
        toast.add({
          severity: 'success',
          summary: 'Connected',
          detail: 'Real-time updates enabled',
          life: 3000
        })
      }

      ws.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data)
          handleMessage(data)
        } catch (err) {
          console.error('Failed to parse WebSocket message:', err)
        }
      }

      ws.onerror = (error) => {
        console.error('WebSocket error:', error)
        isConnected.value = false
      }

      ws.onclose = () => {
        console.log('WebSocket disconnected')
        isConnected.value = false
        
        // Clear ping interval
        if (pingInterval) {
          clearInterval(pingInterval)
          pingInterval = null
        }
        
        // Attempt reconnection
        if (!reconnecting.value) {
          reconnecting.value = true
          reconnectTimer = setTimeout(() => {
            console.log('Attempting to reconnect...')
            connect()
          }, 5000)
        }
      }
    } catch (error) {
      console.error('Failed to connect WebSocket:', error)
      isConnected.value = false
    }
  }

  function disconnect() {
    if (ws) {
      ws.close()
      ws = null
    }
    
    if (reconnectTimer) {
      clearTimeout(reconnectTimer)
      reconnectTimer = null
    }
    
    if (pingInterval) {
      clearInterval(pingInterval)
      pingInterval = null
    }
    
    isConnected.value = false
    reconnecting.value = false
  }

  function handleMessage(data) {
    const { event, data: eventData, timestamp } = data
    
    // Update stores based on event type
    switch (event) {
      case 'message.pushed':
        messagesStore.handleWebSocketMessage(data)
        break
        
      case 'message.processing':
        messagesStore.handleWebSocketMessage(data)
        break
        
      case 'message.completed':
        messagesStore.handleWebSocketMessage(data)
        break
        
      case 'message.failed':
        messagesStore.handleWebSocketMessage(data)
        toast.add({
          severity: 'error',
          summary: 'Message Failed',
          detail: eventData.error || 'Message processing failed',
          life: 5000
        })
        break
        
      case 'queue.depth':
        queuesStore.updateQueueDepth(eventData.queue, eventData.depth)
        break
        
      case 'queue.created':
        queuesStore.fetchQueues()
        break
        
      case 'worker.connected':
      case 'worker.disconnected':
        // Handle worker events if needed
        break
        
      default:
        console.log('Unknown WebSocket event:', event, eventData)
    }
  }

  onUnmounted(() => {
    disconnect()
  })

  return {
    isConnected,
    reconnecting,
    connect,
    disconnect
  }
}
