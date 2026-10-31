'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { Room, RpcError, CAPI } = require('..');

test('full external source controls preserve dimensions and release resources', async () => {
  const room = new Room();
  try {
    const audio = await room.createAudioTrack({ sampleRate: 48000, channels: 1, echoCancellation: false });
    await audio.captureFrame(Buffer.alloc(960));
    assert((await audio.queuedDurationMs()) >= 0);
    await audio.clearQueue();
    await audio.waitForPlayout(0);
    await assert.rejects(audio.startCapture(), /external source/);
    const video = await room.createVideoTrack({ isScreencast: true });
    await video.captureVideoFrame({
      data: Buffer.alloc(64), width: 4, height: 4, format: CAPI.VideoBufferType.Rgba,
      timestampUs: 9007199254740993n, rotation: CAPI.VideoRotation.R0,
      metadata: { userTimestampUs: 18446744073709551615n, frameId: 23, userData: Buffer.from([0, 255]) },
    });
    assert.deepEqual(await video.dimensions(), { width: 4, height: 4 });
    await assert.rejects(video.rtcStats(), error => typeof error.code === 'number');
    await video.close();
    await assert.rejects(video.dimensions(), /closed/);
    const closing = audio.close();
    assert.strictEqual(audio.close(), closing);
    await closing;
    await assert.rejects(audio.clearQueue(), /closed/);
  } finally { await room.close(); }
});

test('snapshots include copied local permissions and attributes before connecting', async () => {
  const room = new Room();
  try {
    const snapshot = await room.snapshot();
    assert.deepEqual(snapshot.localParticipant.attributes, {});
    assert.equal(snapshot.localParticipant.permissions.canPublish, false);
    assert.deepEqual(snapshot.localParticipant.permissions.canPublishSources, []);
    await room.close();
    assert.equal(snapshot.localParticipant.permissions.canPublish, false);
  } finally { await room.close(); }
});

test('E2EE and DataTrack state and failures retain their native error domains', async () => {
  const room = new Room();
  try {
    assert.deepEqual(await room.e2ee.status(), { configured: false, enabled: false, dataKeyIndex: 0 });
    await assert.rejects(room.e2ee.cryptors(), error => error.domain === 1 && error.code === 2);
    await assert.rejects(room.e2ee.exportSharedKey(), error => error.domain === 1 && error.code === 1);
    assert.deepEqual(await room.remoteDataTracks(), []);
    await assert.rejects(room.storeDataTrackSchema({ name: 'schema', encoding: CAPI.DataTrackSchemaEncoding.JsonSchema }, Buffer.from('{}')),
      error => error.domain === CAPI.ErrorDomain.DataTrack && error.code === CAPI.DataTrackErrorCode.Disconnected);
    await assert.rejects(room.publishDataTrack({ name: 'test', frameEncoding: CAPI.DataTrackFrameEncoding.Json }), error => error.domain === 2);
    await assert.rejects(room.subscribeDataTrack('peer', 'DT_test', { bufferCapacity: 2 }), error => error.domain === 2);
    assert.throws(() => room.e2ee.setSharedKey('key'), /Uint8Array/);
    assert.throws(() => room.e2ee.setCryptorEnabled('track', 0, 'true'), /boolean/);
  } finally { await room.close(); }
});

test('async token source executes on application thread without a server', async () => {
  const room = new Room();
  let calls = 0;
  try {
    await assert.rejects(room.connectWithTokenSource(async request => {
      ++calls;
      assert.equal(request.forceRefresh, false);
      assert.equal(request.options.participantIdentity, 'offline-node');
      assert.deepEqual(request.options.participantAttributes, [{ key: 'role', value: 'test' }]);
      await new Promise(resolve => setImmediate(resolve));
      throw new Error('Intentional offline token refusal');
    }, { participantIdentity: 'offline-node', participantAttributes: { role: 'test' } }), error => error.code === 3);
    assert.equal(calls, 1);
    assert.match(await room.version(), /^\d+\./);
  } finally { await room.close(); }
});

test('callback reentry is rejected promptly and close cancels outstanding token requests', async () => {
  const room = new Room();
  try {
    let reentry;
    await assert.rejects(room.connectWithTokenSource(async () => {
      try { await room.snapshot(); } catch (error) { reentry = error.code; }
      throw new Error('Offline refusal');
    }));
    assert.equal(reentry, 'REENTRANT_CALLBACK');
    let started;
    const notified = new Promise(resolve => { started = resolve; });
    const failed = assert.rejects(room.connectWithTokenSource(() => {
      started(); return new Promise(() => {});
    }));
    await notified;
    await room.close();
    await failed;
  } finally { await room.close(); }
});

test('RPC and stream handlers register and unregister offline', async () => {
  const room = new Room();
  try {
    await room.registerRpcMethod('echo', async invocation => invocation.payload);
    await assert.rejects(room.registerRpcMethod('echo', () => 'replacement'), /already registered/);
    await room.unregisterRpcMethod('echo');
    await room.registerRpcMethod('echo', () => { throw new RpcError(1500, 'Application failure', 'details'); });
    await room.unregisterRpcMethod('echo');
    await room.registerTextStreamHandler('text');
    await room.registerByteStreamHandler('bytes');
    await room.unregisterTextStreamHandler('text');
    await room.unregisterByteStreamHandler('bytes');
    await assert.rejects(room.streamText({ topic: 'offline', attributes: { key: 'value' } }), /failed to open text stream/);
    await assert.rejects(room.createAudioStream('peer', 'TR_audio', { capacity: 2 }), /unavailable/);
    await assert.rejects(room.createVideoStream('peer', 'TR_video', { capacity: 2 }), /unavailable/);
  } finally { await room.close(); }
});

test('nested options validate before any physical device or network is opened', async () => {
  const room = new Room();
  try {
    assert.throws(() => room.connect('ws://unused', 'unused', { e2eeOptions: { sharedKey: 'bad' } }), /Uint8Array/);
    assert.throws(() => room.connect('ws://unused', 'unused', { iceServers: [{ urls: [4] }] }), /string/);
    assert.throws(() => room.connect('ws://unused', 'unused', { reconnectPolicy: 1 }), /function/);
    await assert.rejects(room.createCameraTrack({ framesPerSecond: 'bad' }), /finite number/);
    await assert.rejects(room.createMicrophoneTrack({ echoCancellation: 1 }), /boolean/);
    assert.throws(() => room.diagnostics.setTraceOptions({ categoryMask: 1 }), /bigint/);
    assert.throws(() => room.output.setVolume(2), /volume/);
    await assert.rejects(room.publishDataTrack({ schema: { wrong: 'value' } }), /Unknown/);
  } finally { await room.close(); }
});

test('process diagnostics retain other room subscriptions and round-trip exact masks', async () => {
  const first = new Room();
  const second = new Room();
  let logOptions;
  let traceOptions;
  try {
    logOptions = await first.diagnostics.getLogOptions();
    traceOptions = await first.diagnostics.getTraceOptions();
    await first.diagnostics.subscribe({ logs: true, traces: true });
    await second.diagnostics.subscribe({ logs: true, traces: true });
    await first.diagnostics.setTraceOptions({ enabled: true, categoryMask: 127n });
    assert.equal((await second.diagnostics.getTraceOptions()).categoryMask, 127n);
    await first.close();
    assert.equal((await second.diagnostics.getTraceOptions()).enabled, true);
    await second.diagnostics.setLogOptions(logOptions);
    await second.diagnostics.setTraceOptions(traceOptions);
    await second.diagnostics.subscribe();
  } finally { await Promise.all([first.close(), second.close()]); }
});
