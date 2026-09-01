# Documentation index

The `docs` directory is the single source for maintained public project documentation. Files
without a language suffix are written in English; Chinese translations use the `_zh.md` suffix.

## Start here

| Topic | Document | Purpose |
| --- | --- | --- |
| Current implementation route | [ROADMAP.md](ROADMAP.md) | The only source for priorities, status, and future work |
| SDK capabilities | [FEATURES.md](FEATURES.md) | Implemented features and public behavior boundaries |
| Source builds | [BUILDING.md](BUILDING.md) | Dependencies, toolchains, and build instructions |
| Windows distribution | [WINDOWS_SDK_PACKAGING.md](WINDOWS_SDK_PACKAGING.md) | `/MD` DLL packaging, deployment, and consumer checks |

## Design and usage

| Topic | Document |
| --- | --- |
| API reference generation | [API_REFERENCE.md](API_REFERENCE.md) |
| Media devices, playback, and audio processing | [design/media-device-design.md](design/media-device-design.md) |
| End-to-end encryption | [E2EE.md](E2EE.md) / [Chinese](E2EE_zh.md) |
| Video frame metadata | [FRAME_METADATA.md](FRAME_METADATA.md) / [Chinese](FRAME_METADATA_zh.md) |
| Runtime tracing | [TRACING.md](TRACING.md) |

## Validation and quality

| Topic | Document |
| --- | --- |
| Integration tests and recorded results | [integration.md](integration.md) |
| Weak-network, soak, and fault-injection testing | [RELIABILITY_TESTING.md](RELIABILITY_TESTING.md) |
| Formatting, static analysis, and memory checks | [QUALITY_GATES.md](QUALITY_GATES.md) |

Maintenance rules:

1. Update priorities and future tasks only in `ROADMAP.md`.
2. Record implemented behavior in `FEATURES.md`, and acceptance evidence in `integration.md` or the
   relevant quality document.
3. Keep topic documents focused on stable architecture and usage constraints instead of repeating
   overall project status.
4. Keep English and `_zh.md` translations aligned when public behavior changes.
5. Do not reference IDE-local notes or internal planning records from public documentation.
