# LiveKit C++ SDK implementation roadmap

This is the only maintained implementation roadmap and priority source for `livekit-client-cpp`.
Its baseline is SDK `0.5.0` on September 1, 2026. See [FEATURES.md](FEATURES.md) for implemented
capabilities, [BUILDING.md](BUILDING.md) and
[WINDOWS_SDK_PACKAGING.md](WINDOWS_SDK_PACKAGING.md) for build and release procedures, and
[integration.md](integration.md) for recorded validation results.

## Fixed architecture decisions

The following decisions are established constraints rather than recurring design choices:

1. Maintain an independent C/C++ core that links libwebrtc directly. Do not introduce LiveKit Rust
   Core, a Rust toolchain, or a Rust FFI runtime.
2. Treat `include/livekit/capi/livekit.h` as the stable ABI of the prebuilt Windows DLL. Use the
   native C++ DLL surface only when the MSVC toolset, architecture, CRT, configuration, iterator
   debug level, and SDK headers match.
3. Publish Windows packages as `x64 + MSVC v143 + /MD[d] + DLL` by default. Keep `/MT[d]` as an
   explicit source-build option, not the default prebuilt distribution.
4. Encode the platform, architecture, toolset, CRT, and library type in every Windows package name,
   for example `livekit-client-cpp-0.5.0-windows-x64-v143-md-dll.zip`.
5. Use the official JavaScript, Go server, and C++ SDKs to verify protocol behavior, feature
   coverage, and interoperability, but do not copy their API shape or internal architecture.
6. Never let ordinary tests implicitly start real services, modify host networking, or open
   physical audio devices. Keep those scenarios behind explicit opt-in runners.

## Current baseline

The validated Windows path currently includes:

- room, participant, track, and subscription lifecycles; signal resume, ICE restart, full
  reconnect, and room migration recovery;
- VP8, VP9, H264, AV1, simulcast, SVC, backup codecs, runtime encoding changes, and degradation
  preferences;
- PCM audio, multi-format video frames, frame metadata, callbacks, and pull-based audio/video
  streams;
- DataPacket, DataStream, typed DataTrack, RPC, chat, transcription, metrics, and SIP DTMF;
- media and data E2EE, key rotation, ratcheting, reconnect recovery, and official-SDK
  interoperability checks;
- Windows microphone, speaker, camera, screen, window, and system-audio integration with AEC, AGC,
  and noise suppression;
- RTC statistics, unified logging, Perfetto/Chrome tracing, a stable C ABI, C/C++ examples, and
  layered tests;
- `/MD` Release and `/MDd` Debug builds of libwebrtc and the LiveKit DLL for SDK `0.5.0`; and
- `x64-v143-md-dll` package identity, CMake compatibility metadata, and Release/Debug package
  consumer compilation.

Topic documents and recorded test results define the detailed behavior. This roadmap does not
duplicate completed implementation reports.

## R0: Close the 0.5.0 Windows release

Status: in progress, highest priority.

- [x] Set the SDK version to `0.5.0`.
- [x] Add independent `/MD` and `/MDd` builds for the monolithic `webrtc.lib` without enabling
  WebRTC component mode.
- [x] Align LiveKit, media-capture, and target-side vcpkg dependencies on the dynamic CRT.
- [x] Make `windows-x64-v143-md-dll` the default package identity.
- [x] Store architecture, MSVC toolset, and CRT metadata in the installed package and diagnose
  mismatched native C++ consumers.
- [x] Produce `livekit-client-cpp-0.5.0-windows-x64-v143-md-dll.zip` and complete scoped
  Release/Debug C/C++ consumer compilation plus the C++ version probe.
- [ ] Push the dynamic-CRT WebRTC build commit to the `build` branch of
  `zesun96/webrtc-build` so the public README commands are reproducible.
- [ ] Publish the final SDK ZIP, SHA-256 digest, release notes, and required Visual C++
  Redistributable guidance.
- [ ] State explicitly that `/MDd` artifacts are for development machines and are not a
  redistributable production runtime.

When a release task requests package correctness only, do not run tracing, audio queues, Room
lifecycle tests, hardware audio, or the full integration matrix. A full release gate must be
started and recorded as a separate explicit task.

## R1: Strengthen Windows distribution engineering

Status: begin after R0.

1. Add negative package-metadata tests for x86/ARM64, non-MSVC compilers, incorrect CRT selection,
   and mismatched toolsets.
2. Install a reproducibility manifest containing the compiler version, Windows SDK, libwebrtc
   revision, vcpkg triplet, and dependency versions.
3. Add release artifact signing, checksums, and publication checks. Signing keys and credentials
   must come only from the controlled release environment.
4. Retain C ABI export-table verification. Use a C++ consumer smoke test only for matched
   toolchains and never describe it as a cross-version ABI guarantee.
5. Keep automatic Windows CI paused or manually triggered until binary fixtures and execution
   costs are defined. Do not make the independent documentation workflow an implicit build step.

Completion condition: a clean machine can select the correct asset from its filename and manifest,
and unsupported configurations fail with actionable diagnostics.

## R2: Maintain protocol evolution and interoperability

Status: continuous.

1. For every `livekit-protocol`, LiveKit Server, or libwebrtc upgrade, audit new fields, defaults,
   state-machine behavior, and feature negotiation. Successful compilation is not protocol
   compatibility evidence.
2. Preserve real-room interoperability between two instances of this SDK and at least one official
   client SDK, especially for E2EE, migration, DataTrack, frame metadata, codecs, and reconnect.
3. Express new protocol capabilities using this project's RAII, value snapshots, structured errors,
   and size-versioned C ABI.
4. Keep server administration, Egress, Ingress, SIP administration, and server-side Agent features
   outside the client SDK. Create a separate server C++ SDK if those capabilities are required.

Completion condition: every protocol upgrade has a difference report, deterministic tests,
real-server validation, interoperability evidence, and updated topic documentation.

## R3: Quality and long-term stability

Status: continuous and selected according to change risk.

1. Run unit, functional, and installed-package consumer tests for ordinary changes. Keep network,
   soak, Visual Leak Detector, and hardware-audio tests opt-in.
2. Validate reliability changes with resume, ICE restart, full reconnect, server restart, and
   capability-recovery scenarios.
3. Cover VP8, VP9, H264, and AV1 for media changes. Preserve deterministic APM tests and explicit
   hardware acceptance for audio-processing changes.
4. Pair every C ABI declaration with implementation, export checks, null/versioned-structure tests,
   and a pure C consumer.
5. Prevent logs, traces, and failure archives from containing tokens, secrets, raw SDP, ICE details,
   keys, or media payloads.

See [RELIABILITY_TESTING.md](RELIABILITY_TESTING.md), [integration.md](integration.md), and
[QUALITY_GATES.md](QUALITY_GATES.md) for the existing procedures and acceptance evidence.

## R4: Linux and additional platforms

Status: paused; not a blocker for the current Windows release.

Resume Ubuntu 24.04 work only after a matching Linux libwebrtc package and a repeatable validation
environment are available. Then proceed in this order:

1. Fix the x86_64 dependency layout, CMake preset, installation tree, and consumer project.
2. Build the SDK, C/C++ examples, and unit/functional tests.
3. Run media, data, E2EE, and reconnect scenarios against a real LiveKit Server.
4. Add ASan/UBSan, Valgrind, and Linux CI.
5. Evaluate Linux ARM64 and macOS Intel/Apple Silicon only after x86_64 acceptance. Do not declare
   support before validation.

## Execution order

Unless a high-priority data-safety, ABI, or crash defect intervenes, proceed in this order:

1. Finish the R0 WebRTC fork push and publish the `0.5.0` Windows `/MD` package.
2. Complete the R1 manifest, negative consumer tests, and release automation.
3. Continue R2 protocol-difference audits and interoperability regression.
4. Select the R3 quality matrix according to each change's risk.
5. Resume R4 only when the required environment is ready, and do not advertise unvalidated
   platform branches as supported.

## Definition of done

A capability may be marked complete only when it includes:

- a public C++ API and, where required, a stable C ABI;
- an independent C/C++ implementation without LiveKit Rust Core;
- explicit ownership, threading, error, cancellation, and terminal-state semantics;
- examples and user documentation;
- deterministic local tests;
- real LiveKit Server and official-SDK interoperability validation when the environment permits;
  and
- checks for build identity, packaging, ABI behavior, sensitive logs, and regression risk.

Move long implementation narratives and one-time comparisons out of the active roadmap. Do not
create a second project-wide implementation plan.
