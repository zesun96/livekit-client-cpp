import { Room, RoomState, LocalAudioTrack, VideoFrameEvent, CAPI, RpcError, TokenSource } from '..';
async function example() {
  const room = new Room({ eventCapacity: 32 });
  room.on('dataReceived', event => { const buffer: Buffer = event.data; console.log(buffer, event.topic); });
  room.on('videoFrame', (event: VideoFrameEvent) => { const timestamp: bigint = BigInt(event.timestampUs); console.log(timestamp); });
  room.on('workerError', error => console.error(error.message));
  try {
    const audio: LocalAudioTrack = await room.createAudioTrack({ sampleRate: 48000 });
    await audio.captureFrame(Buffer.alloc(960));
    const video = await room.createVideoTrack();
    await video.captureFrame(Buffer.alloc(24), 4, 4, 1n);
    const snapshot = await room.snapshot();
    const permissions: boolean = snapshot.localParticipant.permissions.canPublish;
    const fetchToken: TokenSource = async request => ({ serverUrl: 'ws://test', participantToken: request.forceRefresh ? 'refresh' : 'initial' });
    await room.connectWithTokenSource(fetchToken, { participantAttributes: { role: 'agent' } }, {
      iceServers: [{ urls: ['stun:example.invalid'] }], iceTransportType: CAPI.IceTransportType.Relay,
      e2eeOptions: { sharedKey: Buffer.alloc(32), enabled: true },
      reconnectPolicy: context => context.retryCount > 3 ? null : 1000,
    });
    await room.registerRpcMethod('echo', async invocation => {
      if (!invocation.payload) throw new RpcError(1500, 'Missing payload', 'details');
      return invocation.payload;
    });
    const writer = await room.streamText({ topic: 'text', attributes: { feature: 'test' }, totalSize: 10n });
    await writer.write('hello'); await writer.close(); await writer.dispose();
    const schema = { name: 'test', encoding: CAPI.DataTrackSchemaEncoding.JsonSchema };
    await room.storeDataTrackSchema(schema, Buffer.from('{}'));
    const track = await room.publishDataTrack({ name: 'data', frameEncoding: CAPI.DataTrackFrameEncoding.Json, schema });
    await track.push(Buffer.from('{}'), 18446744073709551615n);
    await room.e2ee.setSharedKey(Buffer.alloc(32));
    await video.captureVideoFrame({ data: Buffer.alloc(64), width: 4, height: 4, format: CAPI.VideoBufferType.Rgba, rotation: CAPI.VideoRotation.R0,
      timestampUs: 1n, metadata: { userTimestampUs: 1n, frameId: 23 } });
    const frames = await room.createVideoStream('peer', 'track', { capacity: 2 });
    for await (const frame of frames) { const ts: bigint = frame.timestampUs; console.log(ts); break; }
    room.on('dataTrackFrame', event => { const ts: bigint = event.frame.userTimestamp!; console.log(ts); });
    room.on('trace', event => { const ts: bigint = event.record.timestampUs; console.log(ts); });
    console.log(permissions);
    if (snapshot.state === RoomState.Connected) await room.sendText('hello', { topic: 'chat' });
    // @ts-expect-error Frames require a byte array.
    await audio.captureFrame('invalid');
    // @ts-expect-error Timestamp must preserve 64-bit precision.
    await video.captureFrame(Buffer.alloc(24), 4, 4, 1);
    // @ts-expect-error Data event payload is typed.
    room.on('dataReceived', event => event.missing);
    // @ts-expect-error Exact counters use bigint.
    await room.diagnostics.setTraceOptions({ categoryMask: 1 });
    // @ts-expect-error DataTrack timestamps use bigint.
    await track.push(Buffer.alloc(1), 1);
  } finally { await room.close(); }
}
void example;
