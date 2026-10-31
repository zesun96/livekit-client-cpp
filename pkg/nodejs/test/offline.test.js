'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const { Room, RoomState, LiveKitError } = require('..');

test('room lifecycle, independent runtime references and copied snapshots', async () => {
  const first = new Room();
  const second = new Room();
  try {
    const snapshot = await first.snapshot();
    assert.equal(snapshot.state, RoomState.Disconnected);
    assert.equal(snapshot.connected, false);
    assert.deepEqual(snapshot.remoteParticipants, []);
    assert.match(await first.version(), /^\d+\.\d+\.\d+/);
    await first.close();
    assert.strictEqual(first.close(), first.close());
    assert.equal((await second.snapshot()).connected, false);
    assert.equal(snapshot.localParticipant.identity, '');
    await assert.rejects(first.snapshot(), e => e instanceof LiveKitError && e.code === 2);
  } finally { await Promise.all([first.close(), second.close()]); }
});

test('close during startup waits for earlier commands and cannot be crowded out', async () => {
  const room = new Room({ maxPendingOperations: 1, maxPendingBytes: 64 });
  const before = room.version();
  const rejected = assert.rejects(room.snapshot(), e => e.code === 'QUEUE_FULL');
  const closed = room.close();
  assert.match(await before, /^\d+\./);
  await rejected;
  await closed;
  await assert.rejects(room.disconnect(), /closed/);
});

test('startup loader failure rejects queued operations and closes cleanly', async () => {
  const room = new Room({ addonPath: path.join(__dirname, 'missing.node') });
  const failed = assert.rejects(room.version(), /Cannot find module/);
  const closing = assert.rejects(room.close(), /Cannot find module/);
  await Promise.all([failed, closing]);
});

test('validates arguments without connecting or accessing devices', async () => {
  assert.throws(() => new Room({ eventCapacity: 0 }), /integer/);
  assert.throws(() => new Room({ unknown: true }), /Unknown option/);
  const room = new Room();
  try {
    assert.throws(() => room.connect('ws://test\0bad', 'token'), /NUL/);
    assert.throws(() => room.connect('ws://test', 'token', { autoSubscribe: 1 }), /boolean/);
    assert.throws(() => room.publishData('text'), /Uint8Array/);
    assert.throws(() => room.sendText('text', { destinationIdentities: [1] }), /string/);
    assert.throws(() => room.setAttributes({ key: 1 }), /string/);
    assert.throws(() => room.setTrackSubscribed('peer', 'track', 1), /boolean/);
    await assert.rejects(room.publishData(Buffer.from([0, 255, 0])), error => error instanceof LiveKitError && typeof error.code === 'number');
    assert.equal((await room.snapshot()).connected, false);
  } finally { await room.close(); }
});

test('bounded operation bytes include commands submitted before worker readiness', async () => {
  const room = new Room({ maxPendingBytes: 128 });
  try {
    await assert.rejects(room.publishData(Buffer.alloc(1024)), e => e.code === 'QUEUE_FULL');
    assert.match(await room.version(), /^\d+\./);
  } finally { await room.close(); }
});

test('external media owns sources and validates frame shapes without hardware', async () => {
  const room = new Room();
  try {
    const audio = await room.createAudioTrack({ name: 'offline-pcm', sampleRate: 48000, channels: 1, queueSizeMs: 100 });
    const video = await room.createVideoTrack({ name: 'offline-i420' });
    await audio.captureFrame(Buffer.alloc(480 * 2));
    await assert.rejects(audio.captureFrame(Buffer.alloc(3)), /PCM16/);
    await video.captureFrame(Buffer.alloc(4 * 4 * 3 / 2), 4, 4, 9007199254740993n);
    await assert.rejects(video.captureFrame(Buffer.alloc(1), 4, 4), error => error instanceof LiveKitError);
    assert.throws(() => video.captureFrame(Buffer.alloc(24), 4, 4, 1), /bigint/);
    await room.close();
    await assert.rejects(audio.captureFrame(Buffer.alloc(960)), /closed/);
  } finally { await room.close(); }
});

test('many concurrent calls complete and room workers release runtime references', async () => {
  const rooms = Array.from({ length: 3 }, () => new Room());
  try {
    await Promise.all(rooms.map(async room => {
      const snapshots = await Promise.all(Array.from({ length: 24 }, () => room.snapshot()));
      assert(snapshots.every(snapshot => snapshot.state === RoomState.Disconnected));
    }));
  } finally { await Promise.all(rooms.map(room => room.close())); }
});
