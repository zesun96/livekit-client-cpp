'use strict';
const { test } = require('node:test');
const { once } = require('node:events');
const assert = require('node:assert/strict');
const path = require('node:path');
const { Room } = require('..');
const addonPath = path.join(__dirname, 'fixtures/fake-addon.cjs');

test('blocking worker operations leave the caller event loop responsive and preserve FIFO', async () => {
  const room = new Room({ addonPath });
  try {
    await room.snapshot();
    let completed = false;
    const version = room.version().then(value => { completed = true; return value; });
    await new Promise(resolve => setTimeout(resolve, 20));
    assert.equal(completed, false);
    const first = room.setName('first');
    const second = room.setName('second');
    assert.equal(await version, '1.2.3');
    await Promise.all([first, second]);
    assert.deepEqual((await room.snapshot()).sequence, ['first', 'second']);
  } finally { await room.close(); }
});

test('copies a Buffer slice before startup await and restores binary events', async () => {
  const room = new Room({ addonPath });
  try {
    const buffer = Buffer.from([99, 0, 255, 0, 88]);
    const received = once(room, 'dataReceived');
    const publishing = room.publishData(buffer.subarray(1, 4), { topic: 'binary' });
    buffer.fill(1);
    await publishing;
    const [event] = await received;
    assert(Buffer.isBuffer(event.data));
    assert.deepEqual([...event.data], [0, 255, 0]);
    assert.equal(event.backingByteLength, 3);
    assert.equal(event.topic, 'binary');
  } finally { await room.close(); }
});

test('close drains event batches awaiting acknowledgements before Worker exit', async () => {
  const room = new Room({ addonPath });
  const received = [];
  room.on('dataReceived', event => received.push(event.data[0]));
  const publishing = Array.from({ length: 80 }, (_, i) => room.publishData(Buffer.from([i])));
  const closing = room.close();
  await Promise.all(publishing);
  await closing;
  assert.deepEqual(received, Array.from({ length: 80 }, (_, i) => i));
});
