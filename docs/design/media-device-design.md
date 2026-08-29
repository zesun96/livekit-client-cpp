# Media Device Design

## Status and scope

This document describes the native media-device architecture in `livekit-client-cpp`. It covers
device discovery, microphone and system-audio capture, camera and screen capture, remote-audio
playback, and the WebRTC audio-processing path.

The implemented and validated platform is Windows. Audio I/O uses miniaudio with WASAPI, camera
capture uses CameraCapture, and monitor/window capture uses screen_capture_lite. The public API is
kept independent of those backends so another platform can provide the same contracts. Linux
implementation validation and CI are intentionally deferred until an Ubuntu environment is
available.

Application-provided PCM and I420 sources remain supported. Native device sources are additional
factories and do not change the behavior or ownership rules of the external-frame APIs.

## Goals

- Expose audio inputs, audio outputs, and camera inputs through stable, opaque device IDs.
- Create native microphone, system-audio, camera, monitor, and window sources.
- Play WebRTC's actual mixed remote audio through a selected output device.
- Feed the same mixed playout signal and estimated output delay into microphone AEC.
- Support deterministic stop, restart, and explicit device/source switching.
- Bound every cross-thread media queue and define its overload policy.
- Preserve the C++ API behavior in the size-versioned C ABI.
- Keep third-party device objects and callback-owned buffers behind internal adapters.

## Non-goals

- Persisting a device selection across operating-system reinstallations or backend changes.
- Automatically selecting a replacement after physical device removal.
- Applying microphone AEC, AGC, or noise suppression to system-audio loopback.
- Treating a successful loopback open as proof that audible samples exist. A silent Windows output
  endpoint may produce no WASAPI loopback callbacks.
- Claiming Linux support before Linux builds, package consumption, and hardware tests have run.

## Architecture

The implementation is split into four layers:

1. Public C++ and C APIs expose device snapshots, source factories, lifecycle controls, and
   playback controls.
2. Core source implementations convert native callbacks into LiveKit audio or video sources.
3. Capture adapters isolate the SDK from the `media-capture` package and third-party types.
4. Platform backends own operating-system devices and their callback threads.

```text
Application
  |  C++ API / size-versioned C ABI
  v
Room, MicrophoneAudioSource, SystemAudioSource,
CameraVideoSource, ScreenVideoSource
  |  internal capture/playback adapters
  v
media-capture
  |-- miniaudio -------- microphone, output playback, WASAPI loopback
  |-- CameraCapture ---- camera frames
  `-- screen_capture_lite -- monitor and window frames
```

Backend headers and handles do not appear in installed LiveKit public headers. This keeps backend
replacement and platform-specific implementation details out of the SDK ABI.

## Device discovery and identity

`EnumerateMediaDevices()` returns a snapshot of the devices visible to the current process:

- `MediaDeviceKind::AudioInput` for microphones;
- `MediaDeviceKind::AudioOutput` for speakers and loopback endpoints;
- `MediaDeviceKind::VideoInput` for cameras.

`EnumerateScreenCaptureSources()` is separate because monitor and window entries contain desktop
coordinates, dimensions, and a `ScreenCaptureSourceKind`.

Device and screen-source IDs are opaque backend IDs. Applications must not parse them or use the
display label as an identifier. Labels are intended only for user interfaces and may be duplicated
or changed by the operating system. An empty audio device ID selects the system default endpoint;
an empty camera ID selects the first available camera. Screen capture requires an ID returned by
the screen-source enumeration API.

Enumeration is read-only: it does not request permission, open a device, or change a room's active
output. The current API provides snapshots rather than hot-plug notifications. Applications that
need a live device menu should enumerate again after an OS device-change event.

## Public source model

There are two source categories:

- External sources accept frames supplied by the application through `CaptureFrame()`.
- Native sources own an OS capture object and deliver its frames automatically.

The native factories create and start their source. They return `nullptr` if validation or the
initial device open fails:

- `CreateMicrophoneAudioSource()` creates 48 kHz, signed 16-bit, mono audio.
- `CreateSystemAudioSource()` creates 48 kHz, signed 16-bit, stereo output-loopback audio.
- `CreateCameraVideoSource()` creates a camera source using the requested dimensions and frame rate.
- `CreateScreenVideoSource()` creates a monitor or window source at 1-60 frames per second.

Each device-backed interface exposes `Start()`, `Stop()`, `IsCapturing()`, its active ID, and an
explicit switch operation. `Stop()` is idempotent and prevents callbacks from outliving the source.
When a running backend supports transactional switching, it attempts to restore the previous
device if the new device cannot be opened. Callers must still handle a failed switch and inspect the
current ID/state instead of assuming that a hot-unplug can always be recovered.

The source must outlive every local track created from it. A safe shutdown order is:

1. unpublish and destroy the local track;
2. stop and destroy the native source;
3. disconnect or destroy the room.

## Audio capture and processing

### Microphone path

The microphone backend asks miniaudio for 48 kHz signed 16-bit mono output. Native callback sizes
are not assumed to be exactly 10 ms, so the SDK accumulates samples and submits 480-sample blocks
to WebRTC AudioProcessing.

```text
WASAPI microphone
  -> miniaudio format conversion/resampling (48 kHz, mono, s16)
  -> 10 ms frame assembly
  -> WebRTC APM capture stream (AEC, digital AGC, noise suppression)
  -> software mute/normalized gain
  -> bounded AudioSource queue
  -> local WebRTC audio track
```

AEC, digital AGC, and noise suppression default to enabled for a microphone source. They can be
changed while capture is running through `SetProcessingOptions()`. Reconfiguration constructs a
new WebRTC AudioProcessing instance and swaps it under synchronization, so a capture callback never
observes a partially configured processor.

Microphone volume is a normalized software gain in the inclusive range `[0, 1]` and is applied
after APM. Mute is equivalent to zero output gain without stopping the capture device. Processing
statistics report capture/render frames, processing failures, queue drops, whether AEC is
currently enabled, and WebRTC's optional ERL, ERLE, residual-echo-likelihood, and delay metrics.
Each optional metric has an explicit availability flag because AEC cannot estimate it before it
has observed enough correlated render and capture audio. The C ABI appends the same value/flag
pairs to its size-versioned statistics structure.

### System-audio path

On Windows, system audio uses WASAPI loopback on an audio output endpoint:

```text
Selected output endpoint
  -> miniaudio WASAPI loopback (48 kHz, stereo, s16)
  -> bounded AudioSource queue
  -> local WebRTC audio track with ScreenShareAudio source
```

The output endpoint ID comes from an `AudioOutput` entry returned by `EnumerateMediaDevices()`.
System audio deliberately bypasses microphone APM because echo cancellation and microphone gain
control would damage the signal being shared. It is also independent from `CreateAudioSource()`,
which continues to accept application-provided PCM.

Use `PublishScreenShareAudioTrack()` or set `TrackPublishOptions::source` to
`TrackSource::ScreenShareAudio` so the server and subscribers receive the correct LiveKit track
source metadata. The complete flow is demonstrated by
[`publish_system_audio`](../../examples/publish_system_audio/publish_system_audio.cpp).

## Remote-audio playback and AEC reference

Each connected room owns one WebRTC audio device and one native playback adapter. WebRTC calls
`AudioTransport::NeedMorePlayData()` every 10 ms to obtain its actual mixed remote audio. The room
volume and mute state are applied to both the device output and the microphone AEC reference:

```text
Subscribed remote tracks
  -> WebRTC decode, gain, and mix
  -> NeedMorePlayData (48 kHz, stereo, 10 ms)
       |-> bounded playback ring -> room gain/mute -> miniaudio/WASAPI output
       `-> same room gain/mute ---> latest mixed frame -> microphone APM reverse stream
                                                        + estimated playout delay
```

Using the mixed playout callback is important: track-observer callbacks are suitable for
application observation but are not guaranteed to represent the exact signal delivered to the
speaker. The AEC reference must also include software output volume and mute. Supplying the
pre-volume PCM overestimates echo at reduced speaker levels and can suppress unrelated near-end
speech during double-talk.

The playback ring is preallocated and bounded. Its default capacity is 200 ms. If a producer
overruns the queue, the oldest frames are discarded to keep latency bounded. If the device consumes
faster than WebRTC supplies data, the callback writes silence and increments the underrun counter.
No output callback waits for network or SDK work.

Room output controls are available after `Connect()` has created the peer transport factory:

- `SetAudioOutputDevice()` and `AudioOutputDevice()`;
- `SetSpeakerVolume()` and `SpeakerVolume()` using normalized `[0, 1]` values;
- `SetSpeakerMuted()` and `SpeakerMuted()`;
- `GetAudioPlaybackStats()`.

Playback statistics are cumulative for the playback object's lifetime. `estimated_delay_ms` is the
sum of currently buffered audio and measured device buffering. The value passed to WebRTC APM is
capped at 500 ms. `dropped_frames` identifies producer overflow; `underrun_frames` can include
expected silence around startup, shutdown, and periods with no subscribed audio.

## Video capture

### Camera

The camera backend produces top-to-bottom BGRA frames. Callback-owned pixels are copied into a
capacity-one latest-frame queue so a slow conversion cannot create unbounded latency. A worker uses
libyuv to convert BGRA to tightly packed I420 before submitting it to the LiveKit video source.

Frame metadata carries a monotonic timestamp and display rotation. Public rotations are restricted
to 0, 90, 180, and 270 degrees and map directly to WebRTC rotation metadata. The current Windows
camera backend normalizes captured frames to rotation 0 and does not mirror pixels.

### Monitor and window capture

Monitor and window sources use the same latest-frame policy and BGRA-to-I420 conversion. Source
selection is explicit and includes an `include_cursor` option. Switching recreates the underlying
capture manager because screen_capture_lite configuration is immutable after startup.

A minimized/closed window, display removal, DPI changes, and protected content are OS/backend
conditions. They must be tested on the target hardware; the SDK does not synthesize frames to hide
those states.

## Threading, queues, and memory

- OS callbacks may run on backend-owned threads.
- Callback frame views are borrowed and valid only for the duration of the callback.
- Camera and screen adapters copy pixels before returning from the backend callback.
- Video conversion runs on the latest-frame worker, not on the backend callback thread.
- Playback uses a preallocated ring and writes silence instead of blocking on underrun.
- The AEC render reference stores only the newest mixed frame; the microphone capture path pulls it.
- Device stop waits for the backend to stop callbacks before owned buffers and adapters are
  destroyed.
- No device implementation creates detached threads.

Audio and video overload policies intentionally differ. Audio keeps a short bounded history because
continuous timing matters, while video keeps only the newest pending frame because stale video is
less useful than low latency.

## Error and ABI contracts

C++ device factories return `nullptr` on initial validation/open failure. Runtime lifecycle and
switch methods return `bool`, and state/active-ID accessors let callers reconcile the result. An
empty enumeration result is valid when no device exists or the process cannot access devices.

The C API uses opaque handles and `lk_status_t`. Extensible option and statistics structures begin
with `struct_size`; callers initialize them with the matching `*_init` function. Implementations
copy only fields present in the caller-provided size so older callers remain compatible when fields
are appended.

Public callbacks and C ABI functions must not receive C++ exceptions. Backend callback exceptions,
allocation failures, and invalid external values are converted to failure results or counters at
the boundary.

## Validation strategy

Automated local tests cover:

- option defaults and invalid configuration;
- external PCM/I420 compatibility;
- C ABI argument and `struct_size` handling;
- microphone runtime gain and processing reconfiguration;
- 10 ms APM capture/reverse-stream validation;
- deterministic stationary-noise suppression and delayed-projection double-talk preservation
  using the repository's 48 kHz mono speech fixture;
- bounded frame-copy behavior and rotation metadata;
- invalid native device/source IDs;
- playback and system-audio public API surfaces.

Windows hardware acceptance additionally covers:

- microphone, camera, monitor, and window publication to a real LiveKit room;
- remote audio entering the actual playback mixer and AEC reverse stream;
- output playback with zero queue drops under nominal load;
- concurrent tone playback and WASAPI loopback with non-zero captured samples;
- repeated connect/publish runs to distinguish stable behavior from one successful attempt.
- an opt-in physical speaker-to-microphone AEC measurement using explicit device IDs when needed,
  deterministic wide-band excitation, median-frame AEC-disabled baseline and AEC-enabled residual
  RMS levels, clipped-sample counts, measured ERLE, and raw WebRTC ERL/ERLE, residual-echo,
  AEC-delay, playback-delay,
  processing-error, and queue-drop evidence.
- controlled physical double-talk using an unreferenced speech playback stream and P90 active-frame
  retention, plus stationary-noise suppression using an unreferenced noise stream and median-frame
  RMS ratios; both retain playback levels, clipping, processing-error, and queue-drop evidence.

Physical device removal/reinsertion, multiple cameras with orientation differences, display
hot-plug, and additional hardware/room layouts remain manual acceptance items. AEC, double-talk,
and noise-suppression measurements are automated but opt-in because their results are only
meaningful with a documented acoustic layout. Network integration tests remain opt-in and require
explicit LiveKit credentials.

Linux CI and Ubuntu hardware validation are paused as of 2026-08-22 because no Ubuntu environment
is currently available. They must be completed before GNU/Linux is marked supported.
