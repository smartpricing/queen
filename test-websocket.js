#!/usr/bin/env node

import WebSocket from 'ws';

console.log('🔌 Testing WebSocket connection to Queen V2...\n');

const ws = new WebSocket('ws://localhost:6632/ws/dashboard');

ws.on('open', () => {
  console.log('✅ Connected to WebSocket');
  
  // Subscribe to some queues
  ws.send(JSON.stringify({
    type: 'subscribe',
    queues: ['emails', 'test-queue']
  }));
  
  // Send ping
  ws.send('ping');
  
  console.log('\n📊 Listening for events...\n');
});

ws.on('message', (data) => {
  const message = data.toString();
  
  if (message === 'pong') {
    console.log('🏓 Received pong');
    return;
  }
  
  try {
    const event = JSON.parse(message);
    console.log(`📨 Event: ${event.event}`);
    console.log('   Data:', JSON.stringify(event.data, null, 2));
    console.log('   Time:', event.timestamp);
    console.log('');
  } catch (error) {
    console.log('📝 Raw message:', message);
  }
});

ws.on('error', (error) => {
  console.error('❌ WebSocket error:', error.message);
});

ws.on('close', () => {
  console.log('🔌 WebSocket connection closed');
  process.exit(0);
});

// Push a test message after 2 seconds
setTimeout(async () => {
  console.log('📤 Pushing test message to trigger events...\n');
  
  try {
    const response = await fetch('http://localhost:6632/api/v1/push', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        items: [{
          queue: 'test-queue',
          partition: 'websocket-test',
          payload: { test: 'WebSocket event test', timestamp: new Date().toISOString() }
        }]
      })
    });
    
    const result = await response.json();
    console.log('✅ Message pushed:', result.messages[0].transactionId, '\n');
  } catch (error) {
    console.error('❌ Failed to push test message:', error.message);
  }
}, 2000);

// Close after 15 seconds
setTimeout(() => {
  console.log('\n👋 Closing connection...');
  ws.close();
}, 15000);

console.log('Press Ctrl+C to exit earlier\n');
