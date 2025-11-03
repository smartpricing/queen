/**
 * Simple Streaming Test - Wait for actual window processing
 */

import { Queen } from '../client-js/client-v2/Queen.js';
import { v4 as uuid } from 'uuid';
const queen = new Queen({ url: 'http://localhost:6632' });

async function main() {
  console.log('\n========== Simple Streaming Test ==========\n');
  
  await queen.queue('test-chat-translations').delete()
  await queen.queue('test-chat-agent').delete()
  await queen.queue('test-chat-translations').namespace('test').task('stream').create();
  await queen.queue('test-chat-agent').namespace('test').task('stream').create();
  

  await queen.stream('test-chat-stream-partitioned', 'test')
    .sources(['test-chat-translations','test-chat-agent'])
    .partitioned()
    .tumblingTime(5)
    .gracePeriod(1)
    .define();
  
  const producer = async () => {
    while (true) {
      const chatId = uuid()
      const message = await queen.queue('test-chat-translations').partition(chatId).push({
        data: {
          kind: 'translation',
          value: Math.random() * 100,
          timestamp: new Date().toISOString(),
          chatId: chatId
        }
      });
      await new Promise(resolve => setTimeout(resolve, 100));
    }
  }
  producer();

  const firstConsumer = async () => {
    await queen
    .queue('test-chat-translations')
    .autoAck(false)
    .batch(1)
    .each()
    .consume(async (message) => {
        const newMessage = message.data 
        newMessage.kind = 'agent';
        const res = await queen
        .transaction()
        .ack(message)
        .queue('test-chat-agent')
        .partition(message.data.chatId)
        .push([{ data: newMessage }])
        .commit()
    })
  }
  firstConsumer();
  
  const consumer = queen.consumer('test-chat-stream-partitioned', 'test-chat-consumer-partitioned');
  let windowsProcessed = 0;
  
  
  await consumer.process(async (window) => {
    console.log(new Date().toISOString(), window.id, window.start,window.end,window.allMessages.length);
    console.log(window.allMessages);
    const byChatId = window.groupBy('data.chatId')
    console.log(byChatId);
  });
  
  
  await queen.close();
  process.exit(0);
}

main()

