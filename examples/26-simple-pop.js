import axios from 'axios';

const SERVER_URL = process.env.SERVER_URL || 'http://localhost:6632';
const QUEUE_NAME = 'test-queue-simple';
const PARTITION = '0';

async function run() {
  // 1. Setup queue
  /*console.log('Setting up queue...');
  try {
    await axios.delete(`${SERVER_URL}/api/v1/resources/queues/${QUEUE_NAME}`);
  } catch (e) { }
  
  await axios.post(`${SERVER_URL}/api/v1/configure`, {
    queue: QUEUE_NAME,
    options: { leaseTime: 300, retryLimit: 3 }
  });
  console.log('✅ Queue created\n');*/

  // 2. Push 2 messages
  console.log('Pushing 2 messages...');
  const pushStart = Date.now();
  await axios.post(`${SERVER_URL}/api/v1/push`, {
    items: [
      { queue: QUEUE_NAME, partition: PARTITION, payload: { message: 'Message 1', index: 1 } },
      { queue: QUEUE_NAME, partition: PARTITION, payload: { message: 'Message 2', index: 2 } }
    ]
  });
  const pushTime = Date.now() - pushStart;
  console.log(`✅ 2 messages pushed`);
  console.log(`   ⏱️  PUSH latency: ${pushTime}ms\n`);

  // Timing arrays
  const popTimes = [];
  const ackTimes = [];
  const interleaveTimes = [];
  let lastOpEnd = Date.now();

  // 3. Two cycles of POP + ACK
  for (let i = 1; i <= 2; i++) {
    console.log(`--- Cycle ${i} ---`);
    
    // Interleave time (time since last operation ended)
    const interleaveStart = lastOpEnd;
    
    // POP
    console.log(`Popping message ${i} (wait=true)...`);
    const popStart = Date.now();
    const interleaveBeforePop = popStart - interleaveStart;
    
    const popResponse = await axios.get(
      `${SERVER_URL}/api/v1/pop/queue/${QUEUE_NAME}/partition/${PARTITION}?batch=1&wait=true`
    );
    const popTime = Date.now() - popStart;
    popTimes.push(popTime);
    
    const message = popResponse.data.messages[0];
    console.log(`✅ Message popped (index: ${message.payload?.index})`);
    console.log(`   ⏱️  POP latency: ${popTime}ms`);
     console.log(message)
    // ACK
    console.log(`Acknowledging message ${i}...`);
    const ackStart = Date.now();
    await axios.post(`${SERVER_URL}/api/v1/ack/batch`, {
      acknowledgments: [{
        transactionId: message.transactionId,
        partitionId: message.partitionId
      }]
    });
    const ackTime = Date.now() - ackStart;
    ackTimes.push(ackTime);
    lastOpEnd = Date.now();
    
    console.log(`✅ Message acknowledged`);
    console.log(`   ⏱️  ACK latency: ${ackTime}ms`);
    
    if (i > 1) {
      interleaveTimes.push(interleaveBeforePop);
      console.log(`   ⏱️  Interleave (gap since prev ACK): ${interleaveBeforePop}ms`);
    }
    console.log('');
  }

  // Summary
  console.log('=== Summary ===');
  console.log(`PUSH latency:     ${pushTime}ms`);
  console.log('');
  console.log('POP latencies:');
  popTimes.forEach((t, i) => console.log(`  Cycle ${i + 1}: ${t}ms`));
  console.log('');
  console.log('ACK latencies:');
  ackTimes.forEach((t, i) => console.log(`  Cycle ${i + 1}: ${t}ms`));
  console.log('');
  console.log('Interleave (gap between cycles):');
  interleaveTimes.forEach((t, i) => console.log(`  After cycle ${i + 1}: ${t}ms`));
  console.log('');
  
  const totalPop = popTimes.reduce((a, b) => a + b, 0);
  const totalAck = ackTimes.reduce((a, b) => a + b, 0);
  const totalInterleave = interleaveTimes.reduce((a, b) => a + b, 0);
  
  console.log('Totals:');
  console.log(`  PUSH:       ${pushTime}ms`);
  console.log(`  POP total:  ${totalPop}ms`);
  console.log(`  ACK total:  ${totalAck}ms`);
  console.log(`  Interleave: ${totalInterleave}ms`);
  console.log(`  Grand total: ${pushTime + totalPop + totalAck}ms`);
}

run().catch(err => {
  console.error('Error:', err.response?.data || err.message);
  process.exit(1);
});
