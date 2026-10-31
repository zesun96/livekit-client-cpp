'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { randomUUID } = require('node:crypto');
const { until, sdk: { Room } } = require('./helpers');

const { LIVEKIT_NODE_INTEGRATION, LIVEKIT_URL, LIVEKIT_TOKEN, LIVEKIT_SECOND_TOKEN } = process.env;
const enabled = LIVEKIT_NODE_INTEGRATION === '1' && LIVEKIT_URL && LIVEKIT_TOKEN && LIVEKIT_SECOND_TOKEN;

function receive(room, name, topic) {
  let listener;
  let timer;
  const promise = new Promise((resolve, reject) => {
    listener = event => { if (event.topic === topic) { clearTimeout(timer); room.off(name, listener); resolve(event); } };
    room.on(name, listener);
    timer = setTimeout(() => { room.off(name, listener); reject(new Error(`Timed out waiting for ${name}`)); }, 10000);
  });
  // Attach a rejection handler immediately even if a preceding send fails.
  promise.catch(() => {});
  return { promise, cancel() { clearTimeout(timer); room.off(name, listener); } };
}

test('two peers exchange binary data and text on a real server', { skip: !enabled, timeout: 60000 }, async () => {
  const first = new Room();
  const second = new Room();
  const topic = `node-binding-${randomUUID()}`;
  const data = receive(second, 'dataReceived', topic);
  const text = receive(second, 'textReceived', topic);
  try {
    // Disabling media subscription keeps this scenario independent of physical media devices.
    await first.connect(LIVEKIT_URL, LIVEKIT_TOKEN, { autoSubscribe: false });
    await second.connect(LIVEKIT_URL, LIVEKIT_SECOND_TOKEN, { autoSubscribe: false });
    await until('both rooms connected', async () => (await first.snapshot()).connected && (await second.snapshot()).connected);
    const bytes = Buffer.from([0, 1, 255, 0, 128]);
    await first.publishData(bytes, { topic, reliable: true });
    assert.deepEqual((await data.promise).data, bytes);
    await first.sendText('Node.js 你好', { topic });
    assert.equal((await text.promise).text, 'Node.js 你好');
    const snapshot = await first.snapshot();
    assert(snapshot.connected);
    const secondIdentity = (await second.snapshot()).localParticipant.identity;
    await until('second participant snapshot', async () => (await first.snapshot()).remoteParticipants.some(p => p.identity === secondIdentity));
  } finally {
    data.cancel(); text.cancel();
    await Promise.all([first.close(), second.close()]);
  }
});
