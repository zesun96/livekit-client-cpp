'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { Worker } = require('node:worker_threads');
const { once } = require('node:events');
const addonPath = process.env.LIVEKIT_NODE_TEST_ADDON;

test('generated POD marshalling preserves nested options, defaults, bytes and 64-bit values', { skip: !addonPath }, () => {
  const native = require(addonPath);
  const room = native.create({});
  const roundTrip = (type, value) => native.call(room, '_testOptions', { type, value });
  try {
    const options = roundTrip('CRoomConnectOptions', { iceServers: [{ urls: ['stun:example.invalid:3478'], username: 'user', password: 'test' }],
      e2eeOptions: { enabled: true, sharedKey: Buffer.from([0, 255, 0]), ratchetWindowSize: 8 } });
    assert.deepEqual(options.iceServers[0].urls, ['stun:example.invalid:3478']);
    assert.equal(options.iceServers[0].password, 'test');
    assert.deepEqual(options.e2eeOptions.sharedKey, Buffer.from([0, 255, 0]));
    assert.equal(options.e2eeOptions.enabled, true);
    const frame = roundTrip('CVideoFrameInput', { timestampUs: 9007199254740993n, metadata: {
      hasUserTimestampUs: true, userTimestampUs: 18446744073709551615n, hasUserData: true, userData: Buffer.from([0, 255]),
    }, planes: [{ offset: 0, size: 24, stride: 4 }] });
    assert.equal(frame.timestampUs, 9007199254740993n);
    assert.equal(frame.metadata.userTimestampUs, 18446744073709551615n);
    assert.deepEqual([...frame.metadata.userData], [0, 255]);
    assert.equal(frame.planes[0].size, 24);
    const publishing = roundTrip('CTrackPublishOptions', { videoEncoding: { maxBitrate: 9007199254740993n, maxFramerate: 30 }, frameMetadataFeatures: { frameId: true, userData: true } });
    assert.equal(publishing.videoEncoding.maxBitrate, 9007199254740993n);
    assert.equal(publishing.frameMetadataFeatures.frameId, true);
    assert.equal(publishing.frameMetadataFeatures.userData, true);
    assert.throws(() => roundTrip('CVideoFrameInput', { timestampUs: 9007199254740992 }), /safe range/);
    assert.throws(() => roundTrip('CVideoFrameInput', { timestampUs: 1n << 65n }), /overflow/);
  } finally { native.call(room, 'close', {}); }
});

test('extended events copy borrowed nested data from SDK threads', { skip: !addonPath }, () => {
  const native = require(addonPath);
  const room = native.create({});
  try {
    native.call(room, '_testAdvancedEvents', {});
    const { events, dropped } = native.call(room, 'poll', {});
    assert.equal(dropped, 0);
    const video = events.find(event => event.type === 'videoFrameWithMetadata');
    assert.equal(video.frame.timestampUs, 9007199254740993n);
    assert.equal(video.metadata.userTimestampUs, 18446744073709551615n);
    assert.deepEqual([...video.metadata.userData], [7, 7, 7]);
    assert(video.frame.data.every(byte => byte === 7));
    const data = events.find(event => event.type === 'dataTrackFrame');
    assert.equal(data.frame.userTimestamp, 18446744073709551615n);
    assert.deepEqual([...data.frame.data], [7, 7, 7]);
    const text = events.find(event => event.type === 'textStream');
    assert.equal(text.event.content, 'a\0b');
    assert.equal(text.event.chunkIndex, 9007199254740993n);
    const progress = events.find(event => event.type === 'streamProgress');
    assert.equal(progress.totalSize, 18446744073709551615n);
  } finally { native.call(room, 'close', {}); }
});

async function callbackWorker(native, channel, config = {}) {
  const worker = new Worker(`
    const { parentPort, workerData } = require('node:worker_threads');
    const native = require(workerData.addonPath);
    const room = native.create({channelId: workerData.channel});
    try { parentPort.postMessage({ result: native.call(room, workerData.method || '_testCallback', { kind: 'test' }) }); }
    catch(error) { parentPort.postMessage({ error: error.message }); }
    finally { native.call(room, 'close', {}); }
  `, { eval: true, workerData: { addonPath, channel, ...config } });
  const exit = once(worker, 'exit');
  return { worker, message: once(worker, 'message').then(([message]) => message), exit };
}

test('application-owned callback channel supports asynchronous results while the Worker blocks', { skip: !addonPath }, async () => {
  const native = require(addonPath);
  let channel;
  channel = native.createChannel(async request => {
    assert.equal(request.payload.value, 'copied request');
    await new Promise(resolve => setImmediate(resolve));
    native.completeCallback(channel, request.id, { ok: true, payload: 'async response' });
  }, {});
  try {
    const { worker, message, exit } = await callbackWorker(native, channel);
    try { assert.deepEqual((await message).result, { ok: true, payload: 'async response' }); await exit; }
    finally { await worker.terminate(); }
  } finally { native.releaseChannel(channel); }
});

test('RPC callback preserves error data and reconnect policy preserves exact context', { skip: !addonPath }, async () => {
  const native = require(addonPath);
  let channel;
  channel = native.createChannel(request => {
    if (request.kind === 'rpc') {
      assert.equal(request.payload.method, 'testRpc');
      assert.equal(request.payload.invocation.callerIdentity, 'caller');
      native.completeCallback(channel, request.id, { ok: false, code: 1510, message: 'refused', data: 'details' });
    } else {
      assert.equal(request.kind, 'reconnect');
      assert.equal(request.payload.elapsedMs, 9007199254740993n);
      native.completeCallback(channel, request.id, { ok: true, retry: true, delayMs: 37 });
    }
  }, {});
  try {
    const { worker, message, exit } = await callbackWorker(native, channel, { method: '_testRpcCallback' });
    try {
      const reply = (await message).result;
      assert.equal(reply.errorCode, 1510);
      assert.equal(reply.errorMessage, 'refused');
      assert.equal(reply.errorData, 'details');
      await exit;
    } finally { await worker.terminate(); }
  } finally { native.releaseChannel(channel); }
  let reconnect;
  reconnect = native.createChannel(request => {
    assert.equal(request.payload.elapsedMs, 9007199254740993n);
    native.completeCallback(reconnect, request.id, { ok: true, retry: true, delayMs: 37 });
  }, {});
  try {
    const { worker, message, exit } = await callbackWorker(native, reconnect, { method: '_testReconnectCallback' });
    try { assert.deepEqual((await message).result, { retry: true, delayMs: 37 }); await exit; }
    finally { await worker.terminate(); }
  } finally { native.releaseChannel(reconnect); }
});

test('callback timeouts reject late results and cancellation releases native waiters', { skip: !addonPath }, async () => {
  const native = require(addonPath);
  let requestId;
  const timeout = native.createChannel(request => { requestId = request.id; }, { callbackTimeoutMs: 30 });
  try {
    const { worker, message, exit } = await callbackWorker(native, timeout);
    try {
      const result = (await message).result;
      assert.equal(result.ok, false);
      assert.match(result.message, /timed out/);
      await exit;
      assert.equal(native.completeCallback(timeout, requestId, { ok: true }), false);
    } finally { await worker.terminate(); }
  } finally { native.releaseChannel(timeout); }
  let resolveStarted;
  const started = new Promise(resolve => { resolveStarted = resolve; });
  const cancelled = native.createChannel(() => { resolveStarted(); }, {});
  try {
    const { worker, message, exit } = await callbackWorker(native, cancelled);
    try {
      await started;
      native.cancelChannel(cancelled);
      assert.match((await message).result.message, /cancelled/);
      await exit;
    } finally { await worker.terminate(); }
  } finally { native.releaseChannel(cancelled); }
});
