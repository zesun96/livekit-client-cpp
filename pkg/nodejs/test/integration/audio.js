'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { enabled, until, peers, delay, watch, sdk: { CAPI } } = require('./helpers');

// Audio subscription can open the system playback device even with muted output.
// On Windows run this opt-in suite outside the sandbox, as required by AGENTS.md.
test('synthetic silent audio, PCM reader, callback, queue and WAV recording', {
  skip: !enabled || process.env.LIVEKIT_NODE_AUDIO !== '1', timeout: 60000,
}, async t => {
  const { first, second, a } = await peers(t);
  await first.output.setMuted(true); await second.output.setMuted(true);
  assert.equal((await second.output.status()).muted, true);
  const audio = await first.createAudioTrack({ name: 'node.integration.silence', sampleRate: 48000, channels: 1, queueSizeMs: 100 });
  let reader, recorder;
  try {
    await audio.publish({ source: CAPI.TrackSource.Microphone, dtx: false, red: false });
    const publication = await until('audio publication', async () => (await second.snapshot()).remoteParticipants
      .find(p => p.identity === a.identity)?.publications.find(p => p.kind === CAPI.TrackKind.Audio));
    await second.setTrackSubscribed(a.sid, publication.sid, true);
    await until('audio subscription', async () => (await second.snapshot()).remoteParticipants
      .find(p => p.identity === a.identity)?.publications.find(p => p.sid === publication.sid)?.track);
    reader = await second.createAudioStream(a.identity, publication.sid, { capacity: 4 });
    const outputPath = path.join(process.env.LIVEKIT_NODE_TEST_OUTPUT_DIR, 'received-silence.wav');
    recorder = await second.startTrackRecording(a.identity, publication.sid, { outputPath, queueCapacity: 128 });
    const callback = watch(second, 'audioFrame', e => e.track?.sid === publication.sid); t.after(callback.cancel);
    let captured;
    for (let i = 0; i < 100; ++i) {
      await audio.captureFrame(Buffer.alloc(480 * 2));
      if (!captured) captured = await reader.read(10);
      await delay(10);
    }
    assert(captured, 'receiver must decode PCM frames');
    assert.equal(captured.sampleRate, 48000); assert.equal(captured.numChannels, 1);
    assert(captured.samplesPerChannel > 0);
    assert.equal(captured.data.length, captured.samplesPerChannel * 2);
    assert.equal((await callback.promise).sampleRate, 48000);
    assert((await audio.queuedDurationMs()) >= 0);
    await audio.waitForPlayout(3000); await audio.clearQueue();
    await recorder.stop();
    const { stats, error } = await recorder.stats();
    assert.equal(error, '');
    assert(stats.framesWritten > 0n); assert(stats.bytesWritten > 0n);
    const wav = fs.readFileSync(outputPath);
    assert.equal(wav.toString('ascii', 0, 4), 'RIFF');
    assert.equal(wav.toString('ascii', 8, 12), 'WAVE'); assert(wav.length > 44);
    await recorder.close(); recorder = null;
    await reader.close(); reader = null;
  } finally {
    if (recorder) await recorder.close();
    if (reader) await reader.close();
    await audio.unpublish(); await audio.close();
  }
});
