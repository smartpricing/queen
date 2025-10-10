import { SYSTEM_QUEUE } from '../managers/systemEventManager.js';

export async function syncSystemEvents(client, eventManager, consumerGroup) {
  console.log('Synchronizing system events...');
  
  let processed = 0;
  let hasMore = true;
  
  while (hasMore) {
    // Pop and process system events in batches using the server's consumer group
    // This ensures we only process events we haven't seen before
    const result = await client.pop({
      queue: SYSTEM_QUEUE,
      consumerGroup: consumerGroup,  // Use server's unique consumer group
      batch: 100,
      wait: false
    });
    
    if (!result.messages || result.messages.length === 0) {
      hasMore = false;
      break;
    }
    
    for (const message of result.messages) {
      await eventManager.processSystemEvent(message.payload);
      await client.ack(message.transactionId, 'completed', null, consumerGroup);
      processed++;
    }
  }
  
  if (processed > 0) {
    console.log(`Synchronized ${processed} system events`);
  }
  return processed;
}
