import { Queen } from '../client-js/client-v2/Queen.js';

const queen = new Queen({ url: 'http://localhost:6632' });

async function main() {
  await queen.queue('test-push-forever').create();
  while (true) {
    await queen.queue('test-push-forever').push({ data: { message: 'Hello, world!' } });
    await new Promise(resolve => setTimeout(resolve, 1000));
  }
}

main();