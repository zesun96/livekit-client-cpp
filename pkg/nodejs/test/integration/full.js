'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { enabled, until, watch, peers, delay, sdk: { RpcError, CAPI } } = require('./helpers');

test('Node.js complete control/data/video integration', { skip: !enabled, timeout: 120000 }, async t => {
  const { first, second, a, b } = await peers(t);
  await t.test('connection state, participant snapshots and metadata updates', async () => {
    assert.equal((await first.snapshot()).state, CAPI.RoomState.Connected);
    await first.setName('Node SDK integration');
    await until('name acknowledgement', async () => (await first.snapshot()).localParticipant.name === 'Node SDK integration');
    await first.setMetadata('node-metadata');
    await until('metadata acknowledgement', async () => (await first.snapshot()).localParticipant.metadata === 'node-metadata');
    await first.setAttributes({ test: 'node-sdk', unicode: '中文' });
    const remote = await until('participant metadata propagation', async () => {
      const participant = (await second.snapshot()).remoteParticipants.find(p => p.identity === a.identity);
      return participant?.name === 'Node SDK integration' && participant.metadata === 'node-metadata' && participant.attributes.test === 'node-sdk' && participant;
    });
    assert.equal(remote.attributes.unicode, '中文');
    assert.equal(remote.permissions.canPublishData, true);
  });
  await t.test('reliable/lossy binary data, text, bytes and file transfer', async st => {
    for (const reliable of [true, false]) {
      const topic = `binary-${reliable}`;
      const received = watch(second, 'dataReceived', event => event.topic === topic);
      st.after(received.cancel);
      const bytes = Buffer.from([0, 255, 128, 0, 17]);
      await first.publishData(bytes, { topic, reliable, destinationIdentities: [b.identity] });
      const event = await received.promise;
      assert.deepEqual(event.data, bytes);
      assert.equal(event.reliable, reliable);
      assert.equal(event.participantIdentity, a.identity);
    }
    const text = watch(second, 'textReceived', e => e.topic === 'one-shot-text'); st.after(text.cancel);
    await first.sendText('Node.js 测试 ✓', { topic: 'one-shot-text', attributes: { kind: 'text' }, compress: true });
    assert.equal((await text.promise).text, 'Node.js 测试 ✓');
    const bytes = Buffer.alloc(8193); for (let i = 0; i < bytes.length; ++i) bytes[i] = i % 256;
    const binary = watch(second, 'bytesReceived', e => e.topic === 'one-shot-bytes'); st.after(binary.cancel);
    await first.sendBytes(bytes, { topic: 'one-shot-bytes', name: 'binary.bin', mimeType: 'application/octet-stream', chunkSize: 1024, compress: true });
    assert.deepEqual((await binary.promise).data, bytes);
    const directory = process.env.LIVEKIT_NODE_TEST_OUTPUT_DIR;
    assert(directory);
    const file = path.join(directory, 'outgoing.bin'); fs.writeFileSync(file, bytes);
    const receivedFile = watch(second, 'fileReceived', e => e.topic === 'one-shot-file'); st.after(receivedFile.cancel);
    await first.sendFile(file, { topic: 'one-shot-file', mimeType: 'application/octet-stream', chunkSize: 1024 });
    assert.deepEqual((await receivedFile.promise).data, bytes);
  });
  await t.test('asynchronous RPC success, application errors and unregister', async () => {
    await second.registerRpcMethod('node.echo', async invocation => {
      assert.equal(invocation.callerIdentity, a.identity);
      await delay(5); return 'echo:' + invocation.payload;
    });
    assert.equal(await first.performRpc(b.identity, 'node.echo', 'hello', { responseTimeoutMs: 5000 }), 'echo:hello');
    await second.registerRpcMethod('node.error', async () => { throw new RpcError(1555, 'intentional test error', 'error-details'); });
    await assert.rejects(first.performRpc(b.identity, 'node.error', ''), e => e.code === 1555 && e.domain === 3 && e.data === 'error-details');
    await second.unregisterRpcMethod('node.echo');
    await assert.rejects(first.performRpc(b.identity, 'node.echo', 'after-unregister'), e => e.code === CAPI.RpcErrorCode.UnsupportedMethod);
    await second.unregisterRpcMethod('node.error');
  });
  await t.test('incremental text/byte streams, info, completion and cancellation', async st => {
    await second.registerTextStreamHandler('incremental-text');
    await second.registerByteStreamHandler('incremental-bytes');
    const chunks = [], byteChunks = [];
    const onText = e => { if (e.event.topic === 'incremental-text' && e.event.type === CAPI.DataStreamEventType.Chunk) chunks.push(e.event.content); };
    const onBytes = e => { if (e.event.topic === 'incremental-bytes' && e.event.type === CAPI.DataStreamEventType.Chunk) byteChunks.push(e.event.content); };
    second.on('textStream', onText); second.on('byteStream', onBytes);
    st.after(() => { second.off('textStream', onText); second.off('byteStream', onBytes); });
    const textEnd = watch(second, 'textStream', e => e.event.topic === 'incremental-text' && e.event.type === CAPI.DataStreamEventType.Closed); st.after(textEnd.cancel);
    const text = await first.streamText({ topic: 'incremental-text', attributes: { source: 'nodejs' } });
    try {
      const complete = watch(first, 'streamComplete', e => e.id === text.id); st.after(complete.cancel);
      await text.write('first-'); await text.write('第二段\0end'); await text.close();
      assert.equal(await text.isClosed(), true);
      const info = await text.info(); assert.equal(info.attributes.source, 'nodejs'); assert(info.streamId);
      assert.equal((await complete.promise).completion.status, CAPI.DataStreamCompletionStatus.Completed);
      await textEnd.promise; assert.equal(chunks.join(''), 'first-第二段\0end');
    } finally { await text.dispose(); }
    const byteEnd = watch(second, 'byteStream', e => e.event.topic === 'incremental-bytes' && e.event.type === CAPI.DataStreamEventType.Closed); st.after(byteEnd.cancel);
    const writer = await first.streamBytes({ topic: 'incremental-bytes', name: 'stream.bin', mimeType: 'application/octet-stream', totalSize: 5n });
    try {
      await writer.write(Buffer.from([0, 255])); await writer.write(Buffer.from([128, 0, 7])); await writer.close();
      assert.equal((await writer.info()).totalSize, 5n);
      await byteEnd.promise; assert.deepEqual(Buffer.concat(byteChunks), Buffer.from([0, 255, 128, 0, 7]));
    } finally { await writer.dispose(); }
    const cancelled = await first.streamText({ topic: 'cancelled-stream' });
    try {
      const complete = watch(first, 'streamComplete', e => e.id === cancelled.id); st.after(complete.cancel);
      await cancelled.cancel('integration cancellation');
      assert.equal((await complete.promise).completion.status, CAPI.DataStreamCompletionStatus.Cancelled);
    } finally { await cancelled.dispose(); }
    await second.unregisterTextStreamHandler('incremental-text'); await second.unregisterByteStreamHandler('incremental-bytes');
  });
  await t.test('DataTrack schema, frame timestamps, readers and unpublish', async () => {
    const schema = { name: 'node.integration.schema', encoding: CAPI.DataTrackSchemaEncoding.JsonSchema };
    const definition = Buffer.from('{"type":"object"}');
    await first.storeDataTrackSchema(schema, definition);
    const track = await first.publishDataTrack({ name: 'node.integration.data', frameEncoding: CAPI.DataTrackFrameEncoding.Json, schema });
    let reader;
    try {
      const info = await track.info(); assert.equal(info.published, true);
      await until('remote DataTrack', async () => (await second.remoteDataTracks()).some(value => value.sid === info.sid));
      reader = await second.subscribeDataTrack(a.identity, info.sid, { bufferCapacity: 4 });
      const payload = Buffer.from('{"test":"nodejs"}');
      let frame;
      await until('DataTrack frame', async () => { await track.push(payload, 9007199254740993n); frame = await reader.read(100); return frame; });
      assert.deepEqual(frame.data, payload); assert.equal(frame.userTimestamp, 9007199254740993n); assert.equal(frame.hasUserTimestamp, true);
      const received = await second.getDataTrackSchema(a.identity, schema); assert.deepEqual(received.definition, definition);
      await second.updateDataTrackSubscription(a.identity, info.sid, { bufferCapacity: 4, targetFps: 15 });
      assert.equal((await reader.info()).closed, false);
      await reader.close(); reader = null;
      await track.unpublish(); assert.equal((await track.info()).published, false);
    } finally { if (reader) await reader.close(); await track.close(); }
  });
  await t.test('synthetic video publication, subscription, metadata, reader and statistics', async st => {
    const video = await first.createVideoTrack({ name: 'node.integration.video' });
    let reader;
    let published = false;
    try {
      await video.captureFrame(Buffer.alloc(64 * 64 * 3 / 2, 96), 64, 64, 1n);
      await video.publish({ source: CAPI.TrackSource.Camera, videoCodec: CAPI.VideoCodec.Vp8, simulcast: false,
        frameMetadataFeatures: { userTimestamp: true, frameId: true, userData: true } });
      published = true;
      const publication = await until('video publication', async () => (await second.snapshot()).remoteParticipants.find(p => p.identity === a.identity)?.publications.find(p => p.kind === CAPI.TrackKind.Video));
      await second.setTrackSubscribed(a.sid, publication.sid, true);
      await until('video subscription', async () => (await second.snapshot()).remoteParticipants.find(p => p.identity === a.identity)?.publications.find(p => p.sid === publication.sid)?.track);
      await second.updateRemoteTrackSettings(a.sid, publication.sid, { enabled: true, videoQuality: CAPI.VideoQuality.High });
      reader = await second.createVideoStream(a.identity, publication.sid, { capacity: 4 });
      const frameEvent = watch(second, 'videoFrameWithMetadata', e => e.track?.sid === publication.sid && e.metadata?.hasUserTimestampUs); st.after(frameEvent.cancel);
      const rgba = Buffer.alloc(64 * 64 * 4); for (let i = 0; i < rgba.length; i += 4) { rgba[i] = 220; rgba[i + 3] = 255; }
      let frame;
      const timestamp = 9007199254740993n;
      await until('decoded video frame', async () => {
        await video.captureVideoFrame({ data: rgba, width: 64, height: 64, format: CAPI.VideoBufferType.Rgba,
          timestampUs: 0n, metadata: { userTimestampUs: timestamp, frameId: 23, userData: Buffer.from([0, 255, 0, 128]) } });
        frame = await reader.read(100); return frame?.metadata.hasUserTimestampUs;
      });
      assert.equal(frame.width, 64); assert.equal(frame.height, 64); assert.equal(frame.data.length, 64 * 64 * 3 / 2);
      assert.equal(frame.metadata.hasUserTimestampUs, true); assert.equal(frame.metadata.userTimestampUs, timestamp);
      assert.equal(frame.metadata.frameId, 23); assert.deepEqual(Buffer.from(frame.metadata.userData), Buffer.from([0, 255, 0, 128]));
      assert.equal((await frameEvent.promise).frame.width, 64);
      await until('RTC sender statistics', async () => (await video.rtcStats()).length > 0);
      JSON.parse(await video.rtcStatsJson());
      const muted = watch(second, 'trackMuted', e => e.track?.sid === publication.sid); st.after(muted.cancel);
      await video.setMuted(true); await muted.promise; await video.setMuted(false);
      await video.updateEncoding({ maxBitrate: 200000n, maxFramerate: 15 });
      await video.updateDegradationPreference(CAPI.VideoDegradationPreference.MaintainResolution);
      await first.setTrackSubscriptionPermissions(true);
      await reader.close(); reader = null;
      await video.unpublish();
      published = false;
    } catch (error) { st.diagnostic(error.stack); throw error; }
    finally { if (reader) await reader.close(); if (published) await video.unpublish(); await video.close(); }
  });
  await t.test('token source connects and disconnect closes the peer session', async () => {
    await second.disconnect();
    await first.disconnect();
    let fetches = 0;
    await first.connectWithTokenSource(async request => {
      ++fetches; assert.equal(request.forceRefresh, false);
      await delay(5); return { serverUrl: process.env.LIVEKIT_URL, participantToken: process.env.LIVEKIT_TOKEN };
    }, { participantIdentity: a.identity }, { autoSubscribe: false });
    await until('token-source connection', async () => (await first.snapshot()).connected);
    assert.equal(fetches, 1); await first.disconnect();
    assert.equal((await first.snapshot()).connected, false);
  });
  t.diagnostic('control/data/video body complete; closing rooms');
});

test('room close releases a still-published video track without explicit disposal', { skip: !enabled, timeout: 30000 }, async t => {
  const { first, second } = await peers(t);
  const video = await first.createVideoTrack({ name: 'close-with-live-track' });
  await video.captureFrame(Buffer.alloc(64 * 64 * 3 / 2), 64, 64, 0n);
  await video.publish({ videoCodec: CAPI.VideoCodec.Vp8, simulcast: false });
  await first.close(); await first.close();
  assert.equal((await second.snapshot()).connected, true);
});

test('E2EE encrypted data and shared-key ratchet on real peers', { skip: !enabled, timeout: 45000 }, async t => {
  const key = Buffer.from(process.env.LIVEKIT_NODE_E2EE_KEY, 'base64');
  t.diagnostic('starting encrypted peers');
  const { first, second, b } = await peers(t, { e2eeOptions: { enabled: true, sharedKey: key } }, true);
  assert.equal((await first.e2ee.status()).configured, true);
  assert.equal((await second.e2ee.status()).enabled, true);
  for (const ratchet of [false, true]) {
    if (ratchet) { await first.e2ee.ratchetSharedKey(); await second.e2ee.ratchetSharedKey(); }
    const topic = 'encrypted-' + ratchet;
    const received = watch(second, 'dataReceived', e => e.topic === topic); t.after(received.cancel);
    await first.publishData(Buffer.from([0, 255, 0, 42]), { topic, destinationIdentities: [b.identity] });
    assert.deepEqual((await received.promise).data, Buffer.from([0, 255, 0, 42]));
    assert((await first.e2ee.exportSharedKey()).length > 0);
  }
});
