'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { Worker } = require('node:worker_threads');
const { once } = require('node:events');
const addonPath = process.env.LIVEKIT_NODE_TEST_ADDON;

test('native callbacks copy borrowed bytes and enforce count and byte limits', { skip: !addonPath }, () => {
  // Private injection exists only in a separately built LIVEKIT_NODE_TESTING addon.
  const native = require(addonPath);
  const call = (room, method, args = {}) => native.call(room, method, args);
  const room = native.create({ eventCapacity: 2, eventByteLimit: 4096 });
  try {
    call(room, '_testEvents', { count: 5, size: 3 });
    const batch = call(room, 'poll');
    assert.equal(batch.dropped, 3);
    assert.equal(batch.events.length, 2);
    assert.deepEqual(batch.events.map(e => [...e.data]), [[3, 3, 3], [4, 4, 4]]);
    assert.equal(batch.events[0].topic, 'test-topic');
    assert.equal(batch.events[0].participantIdentity, 'test-peer');
    call(room, '_testEvents', { count: 1, size: 8192 });
    const oversized = call(room, 'poll');
    assert.equal(oversized.dropped, 1);
    assert.equal(oversized.events.length, 0);
  } finally { call(room, 'close'); }
  const bytes = native.create({ eventCapacity: 100, eventByteLimit: 2048 });
  try {
    call(bytes, '_testEvents', { count: 8, size: 256 });
    const batch = call(bytes, 'poll');
    assert(batch.events.length > 0 && batch.events.length < 8);
    assert.equal(batch.dropped + batch.events.length, 8);
    assert.equal(call(bytes, 'poll').dropped, 0);
  } finally { call(bytes, 'close'); }
});

test('native finalizers release rooms and sources on forced Worker shutdown', { skip: !addonPath }, async () => {
  const worker = new Worker(`
    const { parentPort, workerData } = require('node:worker_threads');
    const native = require(workerData);
    const room = native.create({});
    native.call(room, 'createAudioTrack', { sampleRate: 48000, channels: 1 });
    native.call(room, 'createVideoTrack', {});
    global.retainedRoom = room;
    setInterval(() => {}, 1000);
    parentPort.postMessage('ready');
  `, { eval: true, workerData: addonPath });
  try {
    await once(worker, 'message');
    assert.equal(await worker.terminate(), 1);
  } finally { await worker.terminate(); }
  const native = require(addonPath);
  const room = native.create({});
  try { assert.match(native.version(), /^\d+\./); }
  finally { native.call(room, 'close', {}); }
});
