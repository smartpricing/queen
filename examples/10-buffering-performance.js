import { Queen } from 'queen-mq'

const client = new Queen({ 
    baseUrls: ['http://localhost:6632']
});

const queue = 'performance-test'

// Configure queue
await client.queue(queue, {});

console.log('⚡ Performance comparison: Buffered vs Unbuffered\n');

// Test 1: Unbuffered (individual pushes)
console.log('🐌 Test 1: Unbuffered individual pushes');
const unbufferedStart = Date.now();

for (let i = 1; i <= 100; i++) {
    await client.push(`${queue}/unbuffered`, { 
        id: i, 
        data: `Unbuffered message ${i}`,
        timestamp: Date.now()
    });
}

const unbufferedTime = Date.now() - unbufferedStart;
console.log(`   ✅ 100 individual pushes took: ${unbufferedTime}ms\n`);

// Test 2: Buffered pushes
console.log('🚀 Test 2: Buffered pushes (size: 10)');
const bufferedStart = Date.now();

let flushCount = 0;
const promises = [];

for (let i = 1; i <= 100; i++) {
    const promise = client.push(`${queue}/buffered`, { 
        id: i, 
        data: `Buffered message ${i}`,
        timestamp: Date.now()
    }, {
        buffer: {
            size: 10,       // Flush every 10 messages
            time: 1000,     // Or after 1 second
            onFlush: (address, count) => {
                flushCount++;
                console.log(`   📦 Flush ${flushCount}: ${count} messages`);
            }
        }
    });
    promises.push(promise);
}

// Wait for all buffered pushes to complete (they return immediately)
await Promise.all(promises);

// Wait a bit for any remaining flushes
await new Promise(resolve => setTimeout(resolve, 1500));

const bufferedTime = Date.now() - bufferedStart;
console.log(`   ✅ 100 buffered pushes took: ${bufferedTime}ms`);
console.log(`   📊 Total flushes performed: ${flushCount}`);

// Performance comparison
const improvement = ((unbufferedTime - bufferedTime) / unbufferedTime * 100).toFixed(1);
console.log(`\n📈 Performance improvement: ${improvement}% faster with buffering`);
console.log(`   Unbuffered: ${unbufferedTime}ms`);
console.log(`   Buffered:   ${bufferedTime}ms`);

// Final buffer stats
console.log('\n📊 Final buffer statistics:', client.getBufferStats());

// Clean shutdown
await client.close();

console.log('\n✨ Performance test completed!');
