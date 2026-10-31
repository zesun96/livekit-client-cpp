# Node.js / TypeScript binding

Node.js bindings for the LiveKit C++ **RTC client**, using the stable C ABI and Node-API 8.
The package provides CommonJS exports, ESM named imports, and TypeScript declarations. It has
no runtime npm dependencies. It is separate from LiveKit's server administration SDK.

## Status and coverage

Windows x64 was validated locally with Node.js 22.14.0, MSVC, and native SDK 0.7.0.
The package declares Node.js 20+, but other Node.js versions and Electron have not been tested.
GNU/Linux, macOS, mobile, and browser targets are not validated or supported by this binding.
Node-API compatibility does not establish core SDK platform support.

| Capability | Status |
| --- | --- |
| Version, room connect/disconnect/close | Promise APIs |
| Auto-subscribe, adaptive stream, dynacast, retries, timeouts, ICE and reconnect policy | Available |
| Async token source, including recovery refresh | Available |
| Connection, participant, track, subscription, metrics, E2EE and room events | Available |
| Copied participant/publication/subscribed-track snapshots, permissions, attributes, RTC stats | Available |
| Local name, metadata and attributes | Available |
| Remote track subscription by **participant SID** and track SID | Available |
| Reliable/lossy data, destination identities and topics | Available |
| One-shot text, bytes and file sending; completed text/byte/file events | Available |
| Outgoing RPC and async incoming handlers | Available; errors retain code/message/data |
| Incremental text/byte writers, progress/completion and incoming stream events | Available |
| DataTrack schemas, publish/push, snapshots, subscriptions and bounded readers | Available |
| External PCM16 and general video frames, planes, rotation, metadata, encoding controls | Available |
| Copied remote audio/video frame events and bounded pull readers | Available |
| Media devices and screen source enumeration | Available |
| Microphone, system audio, camera, screen and speaker controls, audio processing | Available; physical devices not exercised |
| E2EE GCM/key provider, key import/export/ratchet and cryptor controls | Available |
| Remote recording, RTC statistics, logs, tracing and JSON trace files | Available |

The binding covers the C ABI's feature families through owned JavaScript objects and Promise APIs.
The declarations include generated C POD value types and constants under `CAPI`. Native pointer
ownership and the C ABI's duplicate synchronous/asynchronous entry points are handled internally.
Offline and local-server checks establish API, marshalling, lifecycle, data and synthetic-media
behavior. See [validation results and remaining coverage](TESTING.md). Physical capture devices
and audio quality remain unvalidated.

## Build on Windows x64

Build the core SDK shared library first, following the [core build guide](../../Readme.md).
Reuse a compatible existing SDK build. The addon needs the SDK import library, its runtime DLLs,
Node.js development headers, the Windows x64 `node.lib`, CMake, and Visual Studio 2022 C++ tools.
Download matching Node development files from the official Node.js release directory if absent,
and verify them against that release's `SHASUMS256.txt`.

From the `livekit-client-cpp` directory:

```powershell
./pkg/nodejs/scripts/build.ps1 `
  -NodeIncludeDir "$PWD/out/node-development/node-v22.14.0/include/node" `
  -NodeLibrary "$PWD/out/node-development/node.lib" `
  -SdkLibrary "$PWD/out/build/java-sdk-static-crt/Release/livekitclient.lib" `
  -SdkRuntimeDir "$PWD/out/build/java-sdk-static-crt/Release"
```

These paths illustrate the locally reused development files; the script does not download or
rebuild the SDK. The equivalent environment variables are `NODE_INCLUDE_DIR`, `NODE_LIBRARY`,
`LIVEKIT_CLIENT_CPP_LIBRARY`, and `LIVEKIT_CLIENT_CPP_RUNTIME_DIR`. `npm run build` uses them.
The addon and all DLLs from the supplied runtime directory are deployed to `pkg/nodejs/native`.
The tested SDK and addon use the static MSVC runtime (`/MT`). Use compatible architectures,
configurations, compiler runtimes and native dependencies. Generated binaries are gitignored.

The corresponding direct CMake inputs are `NODE_INCLUDE_DIR`, `NODE_LIBRARY`, and
`LIVEKIT_CLIENT_CPP_LIBRARY`; the build target is `livekitclient_node`.

## Usage

```javascript
const { Room } = require('./pkg/nodejs');

async function main() {
  const room = new Room();
  room.on('dataReceived', event => {
    console.log(event.participantIdentity, event.topic, event.data.toString());
  });
  room.on('workerError', error => console.error(error.message));
  try {
    await room.connect(process.env.LIVEKIT_URL, process.env.LIVEKIT_TOKEN);
    await room.publishData(Buffer.from('hello'), { topic: 'chat', reliable: true });
    console.log(await room.snapshot());
  } finally {
    await room.close();
  }
}
main().catch(error => { console.error(error.message); process.exitCode = 1; });
```

After installing the packed package, import `@livekit-cpp/client` instead. ESM applications can
use `import { Room } from '@livekit-cpp/client'`. TypeScript consumers need `@types/node`.
The default addon is `native/livekitclient_node.node`; override it with `Room({ addonPath })`
or `LIVEKIT_NODE_ADDON`. Dependent SDK DLLs must be resolvable by the system loader, normally
beside the addon. Importing the JS package itself does not initialize the SDK.

`connect()` resolves after signaling join; wait for `connected` or a connected `snapshot()`
before asserting RTC connection state. Name, metadata and attribute setters resolve after sending
the signaling update. When changing different fields consecutively, wait for each updated local
snapshot before sending the next change: the core SDK builds updates using its current snapshot.

`node pkg/nodejs/examples/room.js` runs the five-second room/data example when `LIVEKIT_URL`
and a short-lived `LIVEKIT_TOKEN` are set. Do not place server API secrets in client code.

### External media

```javascript
const audio = await room.createAudioTrack({ sampleRate: 48000, channels: 1 });
await audio.publish(); // connect the room first
await audio.captureFrame(Buffer.alloc(480 * 2)); // 10 ms of interleaved PCM16 LE
const video = await room.createVideoTrack();
await video.publish();
await video.captureFrame(Buffer.alloc(4 * 4 * 3 / 2), 4, 4, 123456789012345n);
await audio.unpublish();
```

Video `timestampUs` uses the native monotonic capture clock. Use `0n` to select the SDK clock;
put application or wall-clock timestamps in `metadata.userTimestampUs` instead.

Sources are created immediately; RTC tracks are materialized on their first `publish()`, which
requires a connected room. Sources can accept frames offline. Local tracks and sources remain
owned by the room until `track.close()` or `room.close()`. Unpublish before closing a published
track. A local track belongs to the
room that created it. All media APIs use Promise results and execute in that room's Worker.
Remote `audioFrame` events carry PCM16 LE `Buffer`s and sample layout; `videoFrame` events carry
packed I420 `Buffer`s, dimensions and a decimal-string `timestampUs`. Outgoing video timestamps
are signed 64-bit `bigint` values. This avoids losing timestamp precision in JavaScript numbers.

`video.captureVideoFrame()` also accepts formats from `CAPI.VideoBufferType`, plane layouts,
rotation, and frame metadata. `userTimestampUs`, RTC counters, stream sizes/timestamps and other
64-bit values use `bigint`. The original `videoFrame` event retains its decimal-string timestamp
for compatibility; `videoFrameWithMetadata` and pull-reader frames expose `bigint` timestamps.
Audio sources expose queue duration, queue clearing and bounded playout waits. Local video tracks
expose encoding and degradation controls. See [the feature example](examples/features.js), which
runs offline by default; `LIVEKIT_NODE_EXAMPLE_CONNECT=1` explicitly enables its server section.

### Token sources and RPC handlers

```javascript
await room.registerRpcMethod('echo', async invocation => invocation.payload);
await room.connectWithTokenSource(async ({ options, forceRefresh }) => {
  // Call your authenticated backend; return short-lived participant credentials.
  return fetchParticipantCredentials(options, forceRefresh);
}, { participantIdentity: 'node-user', participantAttributes: { role: 'agent' } }, {
  iceServers: [{ urls: ['stun:example.invalid:3478'] }],
  reconnectPolicy: context => context.retryCount < 5 ? 1000 : null,
});
```

A token source returns `{ serverUrl, participantToken }`. RPC handlers return strings and may
throw `new RpcError(code, message, data)`. Unregister an existing RPC method before registering
it again. Only one connection attempt may be pending per room.

The response channel belongs to the application thread, so asynchronous handlers run while the
Room Worker is inside the C ABI. It has independent capacity, byte and timeout limits. A timeout
discards late results, and `room.close()` cancels waiting native callbacks. Inside a token source,
reconnect policy or RPC handler, awaiting another native operation on the same room rejects with
`REENTRANT_CALLBACK`. Complete the response before scheduling that room's native work. Ordinary
event listeners can call room APIs. Calling `close()` from a response callback is supported.

### Streams, DataTrack and remote media

`streamText()` and `streamBytes()` return writers with `write`, `close`, `cancel`, `info` and
`dispose`. `close` completes the stream and retains its info; `dispose` releases the native
writer, cancelling an open writer. Incremental text writes preserve embedded NUL characters.
Subscribe to `streamProgress`/`streamComplete` using the writer's numeric `id`. Registered topics
deliver incremental `textStream` and `byteStream` events; completed messages continue through the
existing text/bytes/file events. Stream options accept attributes as a record or key/value array.

`storeDataTrackSchema`, `getDataTrackSchema`, `publishDataTrack`, `remoteDataTracks`,
`subscribeDataTrack` and `updateDataTrackSubscription` cover schema, publication and subscriptions.
Local data tracks expose `push`, `info`, `unpublish`, and `close`; push timestamps are optional
unsigned 64-bit `bigint` values. DataTrack failures retain native error domain 2 and its code.

`createAudioStream`, `createVideoStream`, and `subscribeDataTrack` return owned bounded readers.
Use `read(timeoutMs)`, `info`, `close`, or `for await ... of reader`. Each reader permits one
pending read. The Worker reads in slices of at most 100 ms so close commands can run between
slices. Null means timeout or terminal state; `info().closed` distinguishes terminal readers.
Breaking an async iterator closes its reader. `startTrackRecording` returns a recorder with
`stop`, `stats`, and `close`; stop before inspecting the output file.

### Capture, E2EE and diagnostics

`createMicrophoneTrack`, `createSystemAudioTrack`, `createCameraTrack` and `createScreenTrack`
create hardware-backed sources. Their track objects expose start/stop, device/source switching
and status. Microphone controls include mute, volume and processing options/statistics.
`room.output` controls speaker state and exposes playback statistics. These calls can open real
devices; run hardware probes outside the sandbox as described in the repository instructions.
No hardware probe was executed for this implementation.

Pass `e2eeOptions` at connect time to configure encryption. `room.e2ee` exposes state, shared and
participant keys, export/removal/ratchet, data-key indices and frame cryptors. Key operations and
cryptor enumeration require a configured E2EE manager. `room.diagnostics` controls log/trace
options, event subscriptions and trace files. Logging and tracing configuration is process-wide;
subscriptions fan out to their rooms, and closing one room retains other rooms' subscriptions.
A JSON trace file takes precedence over trace-event subscribers while active, is flushed by
`stopTrace()`, and is flushed when its owning room closes. Room snapshots and track `rtcStats()`
return copied data that can be retained after teardown.

### Threading, queues and shutdown

Each room owns one dedicated Worker. Commands execute in submission order, including commands
submitted during startup. Blocking C ABI calls do not run on the application thread. The native
runtime is reference counted independently for each room. Always await `close()` in a `finally`
block: the Worker keeps the Node process alive until closed. Closing waits for preceding commands,
room teardown, delivery of retained events and Worker exit; it is idempotent. It cancels pending
response callbacks. Other in-flight native operations finish according to their SDK timeout.
Worker termination also runs native finalizers as a fallback.

SDK callbacks copy borrowed values before returning and never call JavaScript or Node-API from
SDK threads for ordinary events. Response callbacks use the application-owned Node-API thread-safe
function channel described above. A mutex protects the native event queue. One event batch at a time is posted to
the application; the Worker waits for acknowledgement before posting another. Event listeners run
on the application's Node thread. An exception thrown by a listener follows normal EventEmitter
semantics. `event` receives each supported event as an object whose `type` is the event name.

`RoomOptions` defaults:

| Option | Default | Behavior |
| --- | --- | --- |
| `eventCapacity` | 256 | Native queue maximum events |
| `eventByteLimit` | 16 MiB | Native queue maximum estimated bytes, including object overhead |
| `pollIntervalMs` | 10 | Worker event polling interval |
| `maxPendingOperations` | 128 | Maximum outstanding application commands |
| `maxPendingBytes` | 32 MiB | Maximum serialized outstanding command bytes |
| `callbackCapacity` | 128 | Maximum pending response callbacks |
| `callbackTimeoutMs` | 10000 | Maximum native callback wait; RPC also respects its supplied timeout |
| `callbackByteLimit` | 1 MiB | Maximum estimated request or response callback bytes |

Native overflow drops oldest events; a single oversized event is dropped. `eventsDropped`
reports the count. The retained queue is bounded; callback copies and the single in-flight batch
add transient memory. High-resolution media consumers should tune limits and consume promptly.
Command overflow rejects with `LiveKitError.code === 'QUEUE_FULL'`. `close()` bypasses command
limits so teardown remains available. Buffers/options are copied before the first asynchronous
wait; caller buffers are never detached. Arguments are validated before native calls;
native failures reject with `LiveKitError`, retaining `code`, `domain`, message and RPC error `data`. Native error
domains follow the C ABI; this binding uses domain 3 for outgoing RPC application errors.

## Offline validation

From `pkg/nodejs`, run `npm test` after the normal build. Tests cover native lifecycle, multiple
room workers, errors, media source/frame validation, copied buffers, bounded commands, startup
failure, FIFO ordering, event delivery during shutdown and application event-loop responsiveness.
No server, credentials, physical microphone or speaker are required.

For real C callback copy and queue-overflow tests, build a **separate test addon** with the same
arguments plus `-Testing`. This writes to `out/build/node-binding-testing/Release`, never to the
package's `native` directory. Then, from the repository root:

```powershell
$env:LIVEKIT_NODE_TEST_ADDON = "$PWD/out/build/node-binding-testing/Release/livekitclient_node.node"
node --test pkg/nodejs/test/*.test.js
Remove-Item Env:LIVEKIT_NODE_TEST_ADDON
```

The private callback injection entry point is compiled only with `LIVEKIT_NODE_TESTING=ON`.
Production builds explicitly disable it. Tests needing callback injection or POD round-trips
skip without the test addon.
Local validation passed all 26 offline tests with the separate test addon, strict TypeScript
checking, production/test addon builds, CommonJS/ESM loading, and installation of the packed
Windows npm artifact into an isolated consumer directory.
Type declarations can be checked using TypeScript 5.9+ and recent `@types/node`:

```powershell
npx tsc --noEmit --strict --target ES2022 --module Node16 --moduleResolution Node16 test/types.ts
```

The generated POD converters, TypeScript value types, schemas and constants are checked in.
Regenerate after a C header change with `python tools/generate_structs.py`; `--check` verifies
the generated files. Regeneration uses the repository's `clang-format` configuration.

## Opt-in real-server validation

Integration scenarios are excluded from `npm test`. From the repository root, reuse the local
server and CLI paths documented in the workspace AGENTS.md:

```powershell
./pkg/nodejs/scripts/test-local.ps1 -Scenario Basic
./pkg/nodejs/scripts/test-local.ps1 -Scenario Full
# Run outside the Windows sandbox: audio subscription may open the playback device.
./pkg/nodejs/scripts/test-local.ps1 -Scenario Audio
```

The runner starts a loopback-only test server, creates temporary credentials, saves TAP/logs and
generated media under `out/node-validation`, then stops its processes and removes the signing
configuration in `finally`. `-Port` selects a base TCP port (also uses TCP +1 and UDP +2).
`-PackagePath` selects an installed package directory; `-AddonPath` overrides the addon.
Audio uses generated silence with muted SDK playback and never captures a microphone.

To use your own server, supply `LIVEKIT_URL`, `LIVEKIT_TOKEN`, and `LIVEKIT_SECOND_TOKEN` for
different identities in the same room, with publish-data permissions. Set
`LIVEKIT_NODE_INTEGRATION=1` and explicitly run:

```powershell
node --test pkg/nodejs/test/integration/two-peer.js
```

Basic validates binary data, text and snapshots. Full additionally validates metadata updates,
lossy/directed data, files, RPC success/errors, incremental streams/cancellation, DataTrack,
VP8 video/metadata/readers/statistics, live-track shutdown, token source and E2EE data/key ratchet.
Full also needs `LIVEKIT_NODE_TEST_OUTPUT_DIR`, `LIVEKIT_NODE_E2EE_TOKEN`,
`LIVEKIT_NODE_E2EE_SECOND_TOKEN` (separate E2EE room), and `LIVEKIT_NODE_E2EE_KEY` (base64 shared key).
Audio needs `LIVEKIT_NODE_AUDIO=1` and the output directory. Missing primary opt-in or credentials
causes a skip. All three scenarios passed locally; see [TESTING.md](TESTING.md).

## Packaging

Build the production addon, then run `npm pack` from `pkg/nodejs`. The tarball includes JavaScript,
declarations, documentation, license, and deployed native runtime files. It requires no install
script or compiler on the consuming machine. A tarball made before building has no native runtime
and cannot create rooms. No package has been published. The generated Windows binaries do not
provide cross-platform support.
