import { createQueenClient } from '../src/client/index.js';

const client = createQueenClient({
    baseUrl: process.env.QUEEN_URL || 'http://localhost:6632',
    timeout: 10000  // 10 second timeout for push operations
});

const namespace = 'smartchat';
const task =  'agent';
const queue = 'id-1';  

async function consumer () {
  await client.consume({
    ns: namespace,
    task: task,
    queue: queue,
    handler: async (message) => {
      console.log(`Consumed message: ${message.data.data}`);
    },
    options: {
      wait: true,
      timeout: 10000
    }
  });
}

let count = 0;
async function producer () {
  const messages = [{
    ns: namespace,
    task: task,
    queue: queue,
    payload: {
      id: count,
      data: `Hello, world! ${count++}`,
      timestamp: new Date().toISOString()
    }
  }];

  const result = await client.push({
    items: messages
  })
  console.log(`Pushed ${result.messages.length} messages`);
}

consumer()
//while (true) {
  //await producer();

//}