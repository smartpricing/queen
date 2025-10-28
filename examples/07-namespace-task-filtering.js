/**
 * Example 07: Namespace and Task Filtering
 * 
 * This example demonstrates:
 * - Using namespaces to organize queues
 * - Using tasks to categorize work
 * - Consuming from multiple queues via namespace/task filters
 */

import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

console.log('Creating queues with namespaces and tasks...\n')

// Media processing namespace
console.log('1. Media Processing Namespace')
await queen
  .queue('video-uploads')
  .namespace('media')
  .task('video-processing')
  .create()

await queen
  .queue('image-uploads')
  .namespace('media')
  .task('image-processing')
  .create()

await queen
  .queue('audio-uploads')
  .namespace('media')
  .task('audio-processing')
  .create()

console.log('   Created 3 queues in "media" namespace')

// Notifications namespace
console.log('\n2. Notifications Namespace')
await queen
  .queue('email-queue')
  .namespace('notifications')
  .task('email-delivery')
  .create()

await queen
  .queue('sms-queue')
  .namespace('notifications')
  .task('sms-delivery')
  .create()

console.log('   Created 2 queues in "notifications" namespace')

// Push messages to different queues
console.log('\n3. Pushing messages...')
await queen.queue('video-uploads').push([
  { data: { file: 'video1.mp4', duration: 120 } }
])
await queen.queue('image-uploads').push([
  { data: { file: 'photo1.jpg', size: 2048 } }
])
await queen.queue('email-queue').push([
  { data: { to: 'alice@example.com', subject: 'Hello' } }
])
await queen.queue('sms-queue').push([
  { data: { to: '+1234567890', message: 'Test SMS' } }
])
console.log('   Pushed messages to various queues')

// Consume from entire namespace
console.log('\n4. Consuming from "media" namespace (all media queues)...')
await queen
  .queue()
  .namespace('media')
  .limit(2)
  .consume(async (message) => {
    console.log('   Received media message:', message.data)
  })

// Consume by specific task
console.log('\n5. Consuming "email-delivery" task specifically...')
await queen
  .queue()
  .task('email-delivery')
  .limit(1)
  .consume(async (message) => {
    console.log('   Received email task:', message.data)
  })

// Consume from namespace + task combination
console.log('\n6. Creating urgent video processing...')
await queen
  .queue('urgent-videos')
  .namespace('media')
  .task('urgent-processing')
  .create()

await queen.queue('urgent-videos').push([
  { data: { file: 'urgent.mp4', priority: 'high' } }
])

await queen
  .queue()
  .namespace('media')
  .task('urgent-processing')
  .limit(1)
  .consume(async (message) => {
    console.log('   Received urgent media task:', message.data)
  })

// Cleanup
await queen.close()
console.log('\nDone!')
