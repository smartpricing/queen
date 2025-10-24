import { Queen } from '../client-js/client/index.js'

const client = new Queen({ 
    baseUrls: ['http://localhost:6632']
});

const queue = 'buffered-queue'

// Configure queue
await client.queue(queue, {});

console.log('🚀 Starting client-side buffering demo...\n');

// Example 1: Basic buffering - flush when 5 messages OR after 3 seconds
console.log('📦 Example 1: Basic buffering (size: 5, time: 3000ms)');

for (let i = 1; i <= 3; i++) {
    const result = await client.push(`${queue}/batch-1`, 
        { message: `Buffered message ${i}`, timestamp: Date.now() }, 
        {
            buffer: {
                size: 5,        // Flush when 5 messages
                time: 3000,     // Or flush after 3 seconds
                onFlush: (address, count) => {
                    console.log(`✅ Buffer flushed: ${count} messages to ${address}`);
                },
                onError: (address, error) => {
                    console.error(`❌ Buffer flush error for ${address}:`, error.message);
                }
            }
        }
    );
    console.log(`   Added message ${i} to buffer:`, result);
}

// Check buffer stats
console.log('📊 Buffer stats after 3 messages:', client.getBufferStats());

// Wait to see time-based flush
console.log('⏰ Waiting for time-based flush...');
await new Promise(resolve => setTimeout(resolve, 3500));

console.log('📊 Buffer stats after time flush:', client.getBufferStats());

// Example 2: Size-based flush
console.log('\n📦 Example 2: Size-based flush (size: 3, time: 10000ms)');

for (let i = 1; i <= 4; i++) {
    const result = await client.push(`${queue}/batch-2`, 
        { message: `Size-based message ${i}`, timestamp: Date.now() }, 
        {
            buffer: {
                size: 3,        // Flush when 3 messages
                time: 10000,    // Long time - should flush by size first
                onFlush: (address, count) => {
                    console.log(`✅ Size-based flush: ${count} messages to ${address}`);
                }
            }
        }
    );
    console.log(`   Added message ${i} to buffer:`, result);
    
    // Check stats after each message
    const stats = client.getBufferStats();
    if (stats.totalBufferedMessages > 0) {
        console.log(`   📊 ${stats.totalBufferedMessages} messages in ${stats.activeBuffers} buffers`);
    }
}

// Example 3: Manual flush
console.log('\n📦 Example 3: Manual flush control');

// Add some messages without auto-flush
for (let i = 1; i <= 2; i++) {
    await client.push(`${queue}/manual`, 
        { message: `Manual message ${i}`, timestamp: Date.now() }, 
        {
            buffer: {
                size: 10,       // High threshold
                time: 60000,    // Long time
                onFlush: (address, count) => {
                    console.log(`✅ Manual flush: ${count} messages to ${address}`);
                }
            }
        }
    );
}

console.log('📊 Buffer stats before manual flush:', client.getBufferStats());

// Manually flush specific buffer
console.log('🔧 Manually flushing buffer...');
await client.flushBuffer(`${queue}/manual`);

console.log('📊 Buffer stats after manual flush:', client.getBufferStats());

// Example 4: Multiple addresses with independent buffers
console.log('\n📦 Example 4: Multiple independent buffers');

// Add messages to different addresses
await client.push(`${queue}/addr-1`, { msg: 'Address 1 - Message 1' }, {
    buffer: { size: 2, time: 5000, onFlush: (addr, count) => console.log(`✅ ${addr}: ${count} messages`) }
});

await client.push(`${queue}/addr-2`, { msg: 'Address 2 - Message 1' }, {
    buffer: { size: 2, time: 5000, onFlush: (addr, count) => console.log(`✅ ${addr}: ${count} messages`) }
});

await client.push(`${queue}/addr-1`, { msg: 'Address 1 - Message 2' }, {
    buffer: { size: 2, time: 5000, onFlush: (addr, count) => console.log(`✅ ${addr}: ${count} messages`) }
});

console.log('📊 Final buffer stats:', client.getBufferStats());

// Clean shutdown - will auto-flush remaining buffers
console.log('\n🔄 Closing client (auto-flush remaining buffers)...');
await client.close();

console.log('✨ Demo completed!');
