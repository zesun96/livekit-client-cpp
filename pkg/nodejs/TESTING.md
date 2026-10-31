# Local validation

Validated on 2026-10-31: Windows x64, Node.js 22.14.0, Node-API 8, MSVC 2022,
LiveKit C++ SDK 0.7.0. Reused the workspace LiveKit server executable and CLI 2.18.2.

| Check | Result |
| --- | --- |
| Production and separate testing addon builds | Passed |
| Offline suite with real native callback/POD test hooks | 26 passed, zero skipped |
| Basic loopback-server scenario | 1 passed |
| Full loopback-server scenario | 10 passed, zero skipped; repeated successfully |
| Silent synthetic audio and WAV recording, outside sandbox | 1 passed |
| Generated declarations/converters/schema consistency | Passed |
| Strict TypeScript declarations | Passed |
| Packed npm artifact, isolated offline install, CommonJS/ESM loading | Passed |
| Full suite against the installed npm artifact (JS and native runtime) | 10 passed |

Full checks participant names, metadata, attributes and permissions; directed reliable/lossy
binary data; Unicode text; compressed byte transfer; files; asynchronous incoming/outgoing RPC,
application error details and unregister; incremental text/byte streams (including embedded NUL),
writer state, completion and cancellation; DataTrack schemas, subscriptions, binary frames and
exact 64-bit timestamps; synthetic RGBA-to-VP8-to-I420 video, custom frame metadata, frame events,
pull readers, mute, encoding/degradation controls and RTC statistics; initial asynchronous token
source connection; E2EE data before/after shared-key ratchet; disconnect and room close while a
local video track remains published.

Audio uses only generated silence, with SDK playback muted. It verifies publication/subscription,
PCM layout, decoded frame callbacks and readers, source queue controls, recording statistics and
the resulting RIFF/WAVE file. It does not capture the physical microphone or measure audio quality.

The live-track shutdown regression found and fixed an addon ownership-order defect: retained RTC
tracks must be released after disconnect and before destroying the room's transport threads.
Otherwise room destruction can hang. The regression deliberately omits track unpublish/disposal.

Tests also account for native asynchronous signaling semantics: join precedes the connected state,
and participant updates need server acknowledgement before mixing consecutive field changes.
Video capture uses the native monotonic clock (`timestampUs: 0n`) and keeps application timestamps
in frame metadata.

For reproducible commands and runner options, see [README.md](README.md#opt-in-real-server-validation).
TAP output, credential-free logs, transferred files and WAV artifacts are retained in
`out/node-validation`. Temporary server configuration is removed after each run.

Unvalidated areas: other OS/Node versions/Electron; real microphone/camera/screen/system-audio
capture; audible playback and acoustic quality; E2EE media/cryptor interoperability; automatic
reconnect and refreshed tokens after a real interruption; external TURN/ICE deployments;
large-scale load and long-duration runs. Callback marshalling and policy behavior have offline
coverage, which does not establish these deployment behaviors. No host fault-injection rules
were installed, and no npm package was published.
