'use strict';
const { Room, CAPI } = require('..');

async function main() {
  const room = new Room();
  room.on('eventsDropped', count => console.error('Dropped events:', count));
  try {
    console.log('Version:', await room.version());
    console.log('E2EE:', await room.e2ee.status());
    const audio = await room.createAudioTrack({ sampleRate: 48000, channels: 1 });
    await audio.captureFrame(Buffer.alloc(960));
    await audio.clearQueue();
    const video = await room.createVideoTrack();
    await video.captureVideoFrame({ data: Buffer.alloc(64), width: 4, height: 4,
      format: CAPI.VideoBufferType.Rgba, timestampUs: 9007199254740993n,
      metadata: { userTimestampUs: 18446744073709551615n, frameId: 1, userData: Buffer.from([0, 255]) } });
    console.log('Video dimensions:', await video.dimensions());
    console.log('Local permissions:', (await room.snapshot()).localParticipant.permissions);
    // This example contacts a server only with explicit opt-in and supplied credentials.
    if (process.env.LIVEKIT_NODE_EXAMPLE_CONNECT === '1') {
      if (!process.env.LIVEKIT_URL || !process.env.LIVEKIT_TOKEN) throw new Error('Set LIVEKIT_URL and LIVEKIT_TOKEN');
      await room.registerRpcMethod('echo', async invocation => invocation.payload);
      await room.connect(process.env.LIVEKIT_URL, process.env.LIVEKIT_TOKEN, { autoSubscribe: false });
      const writer = await room.streamText({ topic: 'node-example', attributes: { language: 'nodejs' } });
      try { await writer.write('Incremental text from Node.js'); await writer.close(); }
      finally { await writer.dispose(); }
      await audio.publish(); await video.publish();
      await audio.captureFrame(Buffer.alloc(960));
      await video.captureFrame(Buffer.alloc(24), 4, 4, 1n);
      console.log('RTC statistics:', await video.rtcStats());
    }
  } finally { await room.close(); }
}
main().catch(error => { console.error(error.message); process.exitCode = 1; });
