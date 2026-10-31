'use strict';
const { Room } = require('..');

async function main() {
  const url = process.env.LIVEKIT_URL;
  const token = process.env.LIVEKIT_TOKEN;
  if (!url || !token) throw new Error('Set LIVEKIT_URL and a short-lived LIVEKIT_TOKEN');
  const room = new Room();
  room.on('dataReceived', event => console.log(event.participantIdentity, event.topic, event.data.toString()));
  room.on('eventsDropped', count => console.error(`Dropped ${count} events; increase queue limits or consume faster`));
  try {
    await room.connect(url, token);
    const snapshot = await room.snapshot();
    console.log('Connected:', snapshot.name, snapshot.localParticipant.identity);
    await room.publishData(Buffer.from('Hello from Node.js'), { topic: 'hello', reliable: true });
    await new Promise(resolve => setTimeout(resolve, 5000));
  } finally {
    await room.close();
  }
}
main().catch(error => { console.error(error.message); process.exitCode = 1; });
