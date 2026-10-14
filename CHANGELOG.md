# Changelog

All notable changes to livekit-client-cpp will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Commit history is grouped by version, newest first within each category.
Unreleased contains commits after the latest release. The 0.1.0 boundary uses its version
bump commit because no local v0.1.0 tag exists; its entries include the initial development history.

## [Unreleased]

## [0.7.0] - 2026-10-14

### Added

- 2026-10-14: feat(swift): add native client bindings and examples ([#133](https://github.com/zesun96/livekit-client-cpp/pull/133)) ([31529ff](https://github.com/zesun96/livekit-client-cpp/commit/31529fffb0b98750a3a8cbf230a911e2206e78d0)).
- 2026-10-14: feat(java): add JNI LiveKit client binding ([#131](https://github.com/zesun96/livekit-client-cpp/pull/131)) ([2c999ff](https://github.com/zesun96/livekit-client-cpp/commit/2c999ffbfd0a0f65a05f0b09eaf900da98a50d91)).
- 2026-10-11: feat(dotnet): add .NET 8 LiveKit client binding ([#130](https://github.com/zesun96/livekit-client-cpp/pull/130)) ([37bc21f](https://github.com/zesun96/livekit-client-cpp/commit/37bc21faaeadbe2857d380d27bc695e09e9b5a68)).
- 2026-10-11: feat(zig): add C ABI binding, tests, and examples ([#128](https://github.com/zesun96/livekit-client-cpp/pull/128)) ([4e314a3](https://github.com/zesun96/livekit-client-cpp/commit/4e314a3b710e7f6c22c3d54427fa5012f1ae6937)).
- 2026-10-11: feat(python): add Python SDK binding ([#126](https://github.com/zesun96/livekit-client-cpp/pull/126)) ([e6bfecf](https://github.com/zesun96/livekit-client-cpp/commit/e6bfecf49f6df7972ab2e9757eb43ade0b30020b)).
- 2026-10-11: feat(lua): export Lua SDK interface; ([#125](https://github.com/zesun96/livekit-client-cpp/pull/125)) ([5c7f6a7](https://github.com/zesun96/livekit-client-cpp/commit/5c7f6a7a9c79eb5ad480fcee651c49a402e6ccee)).
- 2026-10-07: feat(lua): add synchronous and coroutine client binding ([#124](https://github.com/zesun96/livekit-client-cpp/pull/124)) ([348f5a4](https://github.com/zesun96/livekit-client-cpp/commit/348f5a443a25a0132221c4e3e6e4048fbdca7e43)).
- 2026-09-01: feat(recording): add remote track file recorder ([#123](https://github.com/zesun96/livekit-client-cpp/pull/123)) ([f5cc5ed](https://github.com/zesun96/livekit-client-cpp/commit/f5cc5ed6e69130bc76145c90bc19654402041f0d)).

### Changed

- 2026-10-14: Align the native SDK, Python, .NET, and Java package versions to 0.7.0.
- 2026-10-14: docs: add binding examples and platform matrices ([#132](https://github.com/zesun96/livekit-client-cpp/pull/132)) ([84be8d6](https://github.com/zesun96/livekit-client-cpp/commit/84be8d633ba84b92ba23965ac2edd6be766013ac)).
- 2026-10-11: docs: add Zig binding to main readme ([#129](https://github.com/zesun96/livekit-client-cpp/pull/129)) ([f7b93d0](https://github.com/zesun96/livekit-client-cpp/commit/f7b93d065dcdbd817d9f479df9fb7eac6d048cc8)).

### Fixed

- 2026-10-14: fix(test): support functional checks with shared sdk builds ([ab76a5b](https://github.com/zesun96/livekit-client-cpp/commit/ab76a5b1a3c327299d132cc9b722b73c30ccd1f0)).

## [0.6.0] - 2026-09-01

### Changed

- 2026-09-01: release(version): bump version to 0.6.0 ([#122](https://github.com/zesun96/livekit-client-cpp/pull/122)) ([89e4c57](https://github.com/zesun96/livekit-client-cpp/commit/89e4c574ca599be1486c73005fd3ada97ed47acb)).
- 2026-09-01: docs: consolidate public documentation roadmap ([#121](https://github.com/zesun96/livekit-client-cpp/pull/121)) ([6732bdd](https://github.com/zesun96/livekit-client-cpp/commit/6732bdd0551cfadef70bafacd41e7f208fb0ad73)).
- 2026-09-01: build(windows): publish MD SDK packages by default ([#120](https://github.com/zesun96/livekit-client-cpp/pull/120)) ([39547c6](https://github.com/zesun96/livekit-client-cpp/commit/39547c6e90424575c8c046ff6cad9baf0e4c9655)).
- 2026-09-01: build(windows): add dynamic CRT SDK packages ([#119](https://github.com/zesun96/livekit-client-cpp/pull/119)) ([f1f2126](https://github.com/zesun96/livekit-client-cpp/commit/f1f212631f031a06788c2ac70f147b5774d3879a)).
- 2026-08-31: docs(readme): clarify Windows DLL ABI support ([#117](https://github.com/zesun96/livekit-client-cpp/pull/117)) ([ac59b16](https://github.com/zesun96/livekit-client-cpp/commit/ac59b1696490887ed7d60f867612be1c1391a20f)).

### Fixed

- 2026-09-01: fix(audio): stop repeating tasks on owning queue ([#118](https://github.com/zesun96/livekit-client-cpp/pull/118)) ([7a89e28](https://github.com/zesun96/livekit-client-cpp/commit/7a89e286bac5c403a8acb7b644f2a6fbcab9bf80)).

## [0.5.0] - 2026-08-31

### Added

- 2026-08-31: feat(audio): add external PCM queue flow control ([#114](https://github.com/zesun96/livekit-client-cpp/pull/114)) ([533fffa](https://github.com/zesun96/livekit-client-cpp/commit/533fffae8cfe39a95783861c38bbb1893663ec26)).
- 2026-08-31: feat(tracing): add Perfetto-compatible runtime tracing ([#110](https://github.com/zesun96/livekit-client-cpp/pull/110)) ([bf7e5bc](https://github.com/zesun96/livekit-client-cpp/commit/bf7e5bc75998e43dcd3f5e1419a15f43a665b59b)).
- 2026-08-31: feat(capi): complete room and object parity ([#109](https://github.com/zesun96/livekit-client-cpp/pull/109)) ([9045bfe](https://github.com/zesun96/livekit-client-cpp/commit/9045bfee0445b4b1d903f05e34e65ce69dacff92)).
- 2026-08-30: feat(media): add degradation and preconnect controls ([#108](https://github.com/zesun96/livekit-client-cpp/pull/108)) ([95ebb6e](https://github.com/zesun96/livekit-client-cpp/commit/95ebb6e3b92f6f8631b94b005b22c4321b7e72ce)).
- 2026-08-30: feat(media): add end-to-end video frame metadata ([#107](https://github.com/zesun96/livekit-client-cpp/pull/107)) ([4ca3bdc](https://github.com/zesun96/livekit-client-cpp/commit/4ca3bdc303258293753bdc190e2547ec4e94bb98)).
- 2026-08-30: feat(media): add pull-based remote streams ([#106](https://github.com/zesun96/livekit-client-cpp/pull/106)) ([f5a3267](https://github.com/zesun96/livekit-client-cpp/commit/f5a32670874df71a50afdbc2026a999618382406)).
- 2026-08-30: feat(media): support multi-format video frames ([#105](https://github.com/zesun96/livekit-client-cpp/pull/105)) ([61a353d](https://github.com/zesun96/livekit-client-cpp/commit/61a353d8cbb18f1bd69f28b69301ac91b91d975d)).

### Changed

- 2026-08-31: release(version): bump version to 0.5.0 ([#116](https://github.com/zesun96/livekit-client-cpp/pull/116)) ([0dcc88d](https://github.com/zesun96/livekit-client-cpp/commit/0dcc88ddb59707949e03c4427676b5fa49ad68e3)).
- 2026-08-31: build(quality): add clang and VLD quality gates ([#112](https://github.com/zesun96/livekit-client-cpp/pull/112)) ([6f054a0](https://github.com/zesun96/livekit-client-cpp/commit/6f054a091f8a042642bb8df65f6e22631ca27f10)).
- 2026-08-31: docs(api): add Doxygen reference and manual CI gate ([#111](https://github.com/zesun96/livekit-client-cpp/pull/111)) ([c933472](https://github.com/zesun96/livekit-client-cpp/commit/c933472a8c626cfb3d17ee2c1dc4de241853b64c)).

### Fixed

- 2026-08-31: fix(media): prevent dangling local track access on disconnect; ([#115](https://github.com/zesun96/livekit-client-cpp/pull/115)) ([845eeab](https://github.com/zesun96/livekit-client-cpp/commit/845eeab13f6da157f9774d70d24cd43b5fe77d48)).
- 2026-08-31: fix(audio): enforce external PCM source queue bounds ([#113](https://github.com/zesun96/livekit-client-cpp/pull/113)) ([ceabcfe](https://github.com/zesun96/livekit-client-cpp/commit/ceabcfedb868b0077cd8687351086f0114108105)).

## [0.4.0] - 2026-08-30

### Added

- 2026-08-30: feat(connection): complete cloud room migration support ([#103](https://github.com/zesun96/livekit-client-cpp/pull/103)) ([3313455](https://github.com/zesun96/livekit-client-cpp/commit/331345543f4b80bceab843616c1dc199208281e0)).
- 2026-08-29: feat(connection): add dynamic token sources and room migration events ([#102](https://github.com/zesun96/livekit-client-cpp/pull/102)) ([11d31b4](https://github.com/zesun96/livekit-client-cpp/commit/11d31b4ec1e6f33b27d3fb6aa69d647a34bef4f7)).

### Changed

- 2026-08-30: release(version): bump version to 0.4.0 ([#104](https://github.com/zesun96/livekit-client-cpp/pull/104)) ([bd23511](https://github.com/zesun96/livekit-client-cpp/commit/bd2351173fd352a3d2d4eaa2860a121871d28cc4)).
- 2026-08-29: docs: split readme and record integration results ([#101](https://github.com/zesun96/livekit-client-cpp/pull/101)) ([52f40e9](https://github.com/zesun96/livekit-client-cpp/commit/52f40e9cdfc5e92e8073b37df65b82cbe3d57bf3)).

## [0.3.0] - 2026-08-29

### Added

- 2026-08-29: feat(audio): complete hardware APM quality validation ([#99](https://github.com/zesun96/livekit-client-cpp/pull/99)) ([a713b72](https://github.com/zesun96/livekit-client-cpp/commit/a713b7233c87798b3333f683c9cc2b85d717967e)).

### Changed

- 2026-08-29: release(version): bump version to 0.3.0 ([#100](https://github.com/zesun96/livekit-client-cpp/pull/100)) ([6382e58](https://github.com/zesun96/livekit-client-cpp/commit/6382e587e581c7dffe18091a22e0ec9dee3316e7)).
- 2026-08-29: test(integration): add comprehensive matrix coverage for lifecycle, recovery, and reliability ([#98](https://github.com/zesun96/livekit-client-cpp/pull/98)) ([e62b266](https://github.com/zesun96/livekit-client-cpp/commit/e62b266d4a6cf3e2a73f33d6c1212ab374603b6f)).
- 2026-08-27: chore(docs): update libewebrtc source link ([#97](https://github.com/zesun96/livekit-client-cpp/pull/97)) ([973f915](https://github.com/zesun96/livekit-client-cpp/commit/973f915a9c9c674838a82f6973182b46ba44e409)).

## [0.2.0] - 2026-08-27

### Added

- 2026-08-27: feat(build): package release and debug Windows DLLs ([#95](https://github.com/zesun96/livekit-client-cpp/pull/95)) ([0bce78d](https://github.com/zesun96/livekit-client-cpp/commit/0bce78d4e7d049a9276850b6a555e3cf18cd8bcd)).
- 2026-08-27: feat(build): package Windows SDK variants ([#94](https://github.com/zesun96/livekit-client-cpp/pull/94)) ([#94](https://github.com/zesun96/livekit-client-cpp/pull/94)) ([36dd0b6](https://github.com/zesun96/livekit-client-cpp/commit/36dd0b6ddd2c71891053d16023e893d2136962b3)).
- 2026-08-26: feat(capi): add structured error reporting ([#92](https://github.com/zesun96/livekit-client-cpp/pull/92)) ([a31b5b1](https://github.com/zesun96/livekit-client-cpp/commit/a31b5b14a737f0bcef4c591509954596dfdc513c)).
- 2026-08-26: feat(capi): preserve data stream metadata ([#91](https://github.com/zesun96/livekit-client-cpp/pull/91)) ([20b36af](https://github.com/zesun96/livekit-client-cpp/commit/20b36affa6208796a6c1dcfbfd73191be4ed13ff)).
- 2026-08-25: feat(capi): complete remote data track controls ([#90](https://github.com/zesun96/livekit-client-cpp/pull/90)) ([f2bc017](https://github.com/zesun96/livekit-client-cpp/commit/f2bc0172a294509c3b57c2b0c14655fc425c6b5d)).

### Changed

- 2026-08-27: release(version): bump version to 0.2.0 ([#96](https://github.com/zesun96/livekit-client-cpp/pull/96)) ([d2d9632](https://github.com/zesun96/livekit-client-cpp/commit/d2d9632ef9e2efbdd32e64a775f613160f456ae1)).
- 2026-08-26: test(capi): add pure C consumer and reconnect coverage ([#93](https://github.com/zesun96/livekit-client-cpp/pull/93)) ([fb9d054](https://github.com/zesun96/livekit-client-cpp/commit/fb9d054607f45d608331096d2443908d79b333a6)).

## [0.1.0] - 2026-08-25

### Added

- 2026-08-25: feat(capi): expose unified logging callbacks ([#88](https://github.com/zesun96/livekit-client-cpp/pull/88)) ([e62a87b](https://github.com/zesun96/livekit-client-cpp/commit/e62a87bc3b0bd0b51a0de28c432197b523d2efc9)).
- 2026-08-25: feat(logging): add unified sdk log sink ([#87](https://github.com/zesun96/livekit-client-cpp/pull/87)) ([041656f](https://github.com/zesun96/livekit-client-cpp/commit/041656feaaaa4a72c37286b446b68a89966c5b0f)).
- 2026-08-25: feat(capi): export capi ([#85](https://github.com/zesun96/livekit-client-cpp/pull/85)) ([3fad6fc](https://github.com/zesun96/livekit-client-cpp/commit/3fad6fcc6173ae7b0295c85d4c8ccadf383f951a)).
- 2026-08-25: feat(capi): expose end-to-end encryption controls ([#84](https://github.com/zesun96/livekit-client-cpp/pull/84)) ([963a912](https://github.com/zesun96/livekit-client-cpp/commit/963a912f74305f032c2b2c9e93da231a109f143c)).
- 2026-08-24: feat(media): support runtime video encoding updates ([#83](https://github.com/zesun96/livekit-client-cpp/pull/83)) ([f076da0](https://github.com/zesun96/livekit-client-cpp/commit/f076da038b9a7a202dd8e5c8e59d495e880c3b9d)).
- 2026-08-24: feat(media): add on-demand backup codec publishing ([#82](https://github.com/zesun96/livekit-client-cpp/pull/82)) ([c76f6e1](https://github.com/zesun96/livekit-client-cpp/commit/c76f6e1f8c86864323a6a248519ee16323500030)).
- 2026-08-24: feat(media): add VP9 and AV1 SVC publishing ([#81](https://github.com/zesun96/livekit-client-cpp/pull/81)) ([6397ed0](https://github.com/zesun96/livekit-client-cpp/commit/6397ed0550efa980a781df03a97931a6f4597049)).
- 2026-08-24: feat: add DataTrack schema storage and lookup ([#80](https://github.com/zesun96/livekit-client-cpp/pull/80)) ([f9a26b4](https://github.com/zesun96/livekit-client-cpp/commit/f9a26b4c33b6ceb0124671bf0b3bceab67895855)).
- 2026-08-24: feat: add typed DataTrack transport and Windows gates ([#79](https://github.com/zesun96/livekit-client-cpp/pull/79)) ([d5d56a2](https://github.com/zesun96/livekit-client-cpp/commit/d5d56a28051dac1380703f622bfd53c2c9d77039)).
- 2026-08-24: feat: enable H264 and complete E2EE coverage ([#78](https://github.com/zesun96/livekit-client-cpp/pull/78)) ([a2b4490](https://github.com/zesun96/livekit-client-cpp/commit/a2b4490c2ec934899a424e3776e6102eeeed1b3b)).
- 2026-08-22: feat(e2ee): Complete development and validation for this round ([#77](https://github.com/zesun96/livekit-client-cpp/pull/77)) ([060a34f](https://github.com/zesun96/livekit-client-cpp/commit/060a34ff47470e1b20c5211fa684f5d028b6b759)).
- 2026-08-22: feat(media): create native microphone, camera, monitor, and window sources and publish them with the normal local-track ([#76](https://github.com/zesun96/livekit-client-cpp/pull/76)) ([4ea7b51](https://github.com/zesun96/livekit-client-cpp/commit/4ea7b51361e0f6c06579eb593ce50cc9ac9cb602)).
- 2026-08-15: feat(capi): add asynchronous completion callbacks and remote participant snapshots ([#75](https://github.com/zesun96/livekit-client-cpp/pull/75)) ([454a19c](https://github.com/zesun96/livekit-client-cpp/commit/454a19c8525c4b871bac85b6a50a6e3cfb124614)).
- 2026-08-14: feat: add configurable reconnect policy and reconnect recovery test ([#74](https://github.com/zesun96/livekit-client-cpp/pull/74)) ([79b8d87](https://github.com/zesun96/livekit-client-cpp/commit/79b8d874369f813120977d8533952669ae0229b1)).
- 2026-08-14: feat: support compressed data streams ([#73](https://github.com/zesun96/livekit-client-cpp/pull/73)) ([25f68fe](https://github.com/zesun96/livekit-client-cpp/commit/25f68fe611df74d8cadd791a0d4bed72b33fb6c5)).
- 2026-08-14: feat: add rtc stats monitoring ([#72](https://github.com/zesun96/livekit-client-cpp/pull/72)) ([6cf8c2a](https://github.com/zesun96/livekit-client-cpp/commit/6cf8c2af7e26ab5c18edd2a921e5083ee3950902)).
- 2026-08-13: feat: add screen share publishing helpers ([#71](https://github.com/zesun96/livekit-client-cpp/pull/71)) ([0e49e04](https://github.com/zesun96/livekit-client-cpp/commit/0e49e04292cfe3658d4d3f84db1b9a105929ecbc)).
- 2026-08-13: feat: support selectable video codecs ([#70](https://github.com/zesun96/livekit-client-cpp/pull/70)) ([55e65ef](https://github.com/zesun96/livekit-client-cpp/commit/55e65ef7e47ce9826a8d651288804fb17f8f4f67)).
- 2026-08-13: feat: add simulcast and dynacast publishing ([#69](https://github.com/zesun96/livekit-client-cpp/pull/69)) ([0e8570f](https://github.com/zesun96/livekit-client-cpp/commit/0e8570f89f55ad6cdc3b798260cda162f7cf6701)).
- 2026-08-13: feat: expose subscribed quality updates ([#68](https://github.com/zesun96/livekit-client-cpp/pull/68)) ([b704b0a](https://github.com/zesun96/livekit-client-cpp/commit/b704b0a1e078c0cfac4fb86ee0e5c84723ca514e)).
- 2026-08-13: feat: add local track subscribed events ([#67](https://github.com/zesun96/livekit-client-cpp/pull/67)) ([fa07b19](https://github.com/zesun96/livekit-client-cpp/commit/fa07b19814dabda0b2d2a3dd121254280bcc0ddd)).
- 2026-08-13: feat: add participant permission events ([#66](https://github.com/zesun96/livekit-client-cpp/pull/66)) ([f747f9c](https://github.com/zesun96/livekit-client-cpp/commit/f747f9cb89c941e00380a0aceffa6688ae90df01)).
- 2026-08-13: feat: add connection state events ([#65](https://github.com/zesun96/livekit-client-cpp/pull/65)) ([c4e61fa](https://github.com/zesun96/livekit-client-cpp/commit/c4e61fa633ea97c0ccb435e86787b02f811e2f12)).
- 2026-08-13: feat: add metrics batch events ([#64](https://github.com/zesun96/livekit-client-cpp/pull/64)) ([912837a](https://github.com/zesun96/livekit-client-cpp/commit/912837a07b827ea59ada4fd8150562ebac157583)).
- 2026-08-13: feat: add recording status events ([#63](https://github.com/zesun96/livekit-client-cpp/pull/63)) ([f4d6c29](https://github.com/zesun96/livekit-client-cpp/commit/f4d6c293a70a25b5eeb29092938cac36025fb860)).
- 2026-08-13: feat: add transcription segment events ([#62](https://github.com/zesun96/livekit-client-cpp/pull/62)) ([f0af31f](https://github.com/zesun96/livekit-client-cpp/commit/f0af31fa6bb9f58f6e2f9bbe36dc1fc360d1c73b)).
- 2026-08-13: feat: add structured chat messages ([#61](https://github.com/zesun96/livekit-client-cpp/pull/61)) ([5cef541](https://github.com/zesun96/livekit-client-cpp/commit/5cef54130772be15f11293cf8f0931bc1d0e80ed)).
- 2026-08-13: feat: add SIP DTMF data messages ([#60](https://github.com/zesun96/livekit-client-cpp/pull/60)) ([72150b7](https://github.com/zesun96/livekit-client-cpp/commit/72150b77b09083d8e3e306dcfd2d3a903acac4ba)).
- 2026-08-13: feat: add bounded data channel backpressure ([#58](https://github.com/zesun96/livekit-client-cpp/pull/58)) ([5ad7e76](https://github.com/zesun96/livekit-client-cpp/commit/5ad7e76b27134389156a29c81b38807142447e91)).
- 2026-08-13: feat: add incremental data streams ([#57](https://github.com/zesun96/livekit-client-cpp/pull/57)) ([6007db6](https://github.com/zesun96/livekit-client-cpp/commit/6007db68e6b5239edfdc6620a362cf962e066a40)).
- 2026-08-13: feat: add remote track subscription controls ([#56](https://github.com/zesun96/livekit-client-cpp/pull/56)) ([51eda93](https://github.com/zesun96/livekit-client-cpp/commit/51eda9341705476ccd0a3fcdcb5a354f2aa46d46)).
- 2026-08-13: feat: report track subscription failures ([#55](https://github.com/zesun96/livekit-client-cpp/pull/55)) ([eb1b94d](https://github.com/zesun96/livekit-client-cpp/commit/eb1b94d52cbcfe30c1dc8fae7ca35dad06de0e26)).
- 2026-08-13: feat: add track subscription permissions ([#54](https://github.com/zesun96/livekit-client-cpp/pull/54)) ([3d9ca0b](https://github.com/zesun96/livekit-client-cpp/commit/3d9ca0be680f3489a75b47a0146ceddf666f3c39)).
- 2026-08-13: feat: add participant RPC support ([#53](https://github.com/zesun96/livekit-client-cpp/pull/53)) ([e6ba28c](https://github.com/zesun96/livekit-client-cpp/commit/e6ba28c495e92a1b3b99de4db5bfd4b54daaef96)).
- 2026-08-13: feat: resume interrupted room sessions ([#52](https://github.com/zesun96/livekit-client-cpp/pull/52)) ([7d6a478](https://github.com/zesun96/livekit-client-cpp/commit/7d6a4782ac3b54622ae069b45836f76a93c8c019)).
- 2026-08-13: feat: add automatic room reconnection ([#51](https://github.com/zesun96/livekit-client-cpp/pull/51)) ([05610bf](https://github.com/zesun96/livekit-client-cpp/commit/05610bf3f58d257e3022d16129c477785d5ab60b)).
- 2026-08-13: feat: add text and byte data streams ([#50](https://github.com/zesun96/livekit-client-cpp/pull/50)) ([50f5226](https://github.com/zesun96/livekit-client-cpp/commit/50f5226c887da1da775ab7978641f1d2648a940a)).
- 2026-08-12: feat: add local track unpublish lifecycle ([#49](https://github.com/zesun96/livekit-client-cpp/pull/49)) ([d746aab](https://github.com/zesun96/livekit-client-cpp/commit/d746aab35b02ce8ff950bf79048da462b850bee4)).
- 2026-08-12: feat: add track controls and room state APIs ([#48](https://github.com/zesun96/livekit-client-cpp/pull/48)) ([3f5f150](https://github.com/zesun96/livekit-client-cpp/commit/3f5f15039199df3de8006a25735057609e8fa1a5)).
- 2026-08-12: feat: add stable C API and C example ([#47](https://github.com/zesun96/livekit-client-cpp/pull/47)) ([6cfb639](https://github.com/zesun96/livekit-client-cpp/commit/6cfb6391135f3a66b69f821e8cb5121dcff7bbfa)).
- 2026-08-12: feat: add participant and room state events ([#46](https://github.com/zesun96/livekit-client-cpp/pull/46)) ([2b40e04](https://github.com/zesun96/livekit-client-cpp/commit/2b40e04e3a6c94ff1ec7b5f0288abf09b6a1cfc2)).
- 2026-08-12: feat: add media and data transfer support ([#45](https://github.com/zesun96/livekit-client-cpp/pull/45)) ([de24877](https://github.com/zesun96/livekit-client-cpp/commit/de248777be429e0d5689abe6ba566f5d6e8eeb24)).
- 2025-07-28: add audio device; ([#40](https://github.com/zesun96/livekit-client-cpp/pull/40)) ([71bf036](https://github.com/zesun96/livekit-client-cpp/commit/71bf03676bd027cd3e1fca29a7129f50fa2fb34c)).
- 2025-07-27: WIP: add audio publish and play from disk example; ([#39](https://github.com/zesun96/livekit-client-cpp/pull/39)) ([e60886f](https://github.com/zesun96/livekit-client-cpp/commit/e60886fd1d92f7de1f6944f7a2d3e84bca7bbcf8)).
- 2025-05-25: add room event interface and room connect event; ([#38](https://github.com/zesun96/livekit-client-cpp/pull/38)) ([7f4b9c4](https://github.com/zesun96/livekit-client-cpp/commit/7f4b9c4854da28c7f1873799eeead6ed510ee500)).
- 2025-05-21: add participant and track interface; ([#37](https://github.com/zesun96/livekit-client-cpp/pull/37)) ([7026e7b](https://github.com/zesun96/livekit-client-cpp/commit/7026e7b90dbde89882f1635f5b37793aa4a48df7)).
- 2025-05-18: add participant dir and participant impl; ([#36](https://github.com/zesun96/livekit-client-cpp/pull/36)) ([aba640e](https://github.com/zesun96/livekit-client-cpp/commit/aba640e40b53492e70e34ef3b172a00f205902f0)).
- 2025-04-08: add ping timer; ([#28](https://github.com/zesun96/livekit-client-cpp/pull/28)) ([d031d88](https://github.com/zesun96/livekit-client-cpp/commit/d031d88c9064d6745297b156bb076c74151fe14d)).
- 2025-02-22: add websocket wrap impl; ([#24](https://github.com/zesun96/livekit-client-cpp/pull/24)) ([5c6a6fc](https://github.com/zesun96/livekit-client-cpp/commit/5c6a6fca711e20213ce2b8e81c427dcc80328270)).
- 2025-01-19: add hook rtc peerconnect events ([#21](https://github.com/zesun96/livekit-client-cpp/pull/21)) ([d51ec81](https://github.com/zesun96/livekit-client-cpp/commit/d51ec819987dbf72732842ad0de25bff58886afd)).
- 2025-01-19: add hook rtc peerconnect events; ([#20](https://github.com/zesun96/livekit-client-cpp/pull/20)) ([f099e52](https://github.com/zesun96/livekit-client-cpp/commit/f099e52a8430b0c7255704fd5c3df1bf1d780221)).
- 2025-01-19: add rtc :PeerConnection ([#18](https://github.com/zesun96/livekit-client-cpp/pull/18)) ([a64aa00](https://github.com/zesun96/livekit-client-cpp/commit/a64aa00b1718b672b0156eb64ae75bd55c6aebe6)).
- 2024-12-29: add x64-Release build for windows; set vepkg static build deps; ([#15](https://github.com/zesun96/livekit-client-cpp/pull/15)) ([bddcc66](https://github.com/zesun96/livekit-client-cpp/commit/bddcc66b02e5b733690dd16ba2d08b1c83c48ef4)).
- 2024-12-28: add proto signal ([#12](https://github.com/zesun96/livekit-client-cpp/pull/12)) ([b162edf](https://github.com/zesun96/livekit-client-cpp/commit/b162edfd12fca80b63f6185f18daa50265c7e9b6)).
- 2024-12-25: add SignalClient; ([#10](https://github.com/zesun96/livekit-client-cpp/pull/10)) ([6dc2369](https://github.com/zesun96/livekit-client-cpp/commit/6dc236920d8190fe9ddadb587f65b6c2fe7efd7b)).
- 2024-12-21: add sdk core: signal ([#9](https://github.com/zesun96/livekit-client-cpp/pull/9)) ([4259a1b](https://github.com/zesun96/livekit-client-cpp/commit/4259a1b44d61614ec59e0dc9f38261ea0701e158)).
- 2024-12-11: add sdk core file; ([#8](https://github.com/zesun96/livekit-client-cpp/pull/8)) ([7578f97](https://github.com/zesun96/livekit-client-cpp/commit/7578f9786957fd3733b0debf968f70078ab9451a)).
- 2024-12-09: add sdk init; ([#7](https://github.com/zesun96/livekit-client-cpp/pull/7)) ([4c597a7](https://github.com/zesun96/livekit-client-cpp/commit/4c597a7e74f241ca8693e3f6ced61c12f2b00a06)).
- 2024-12-07: add libwebrtc deps; ([#4](https://github.com/zesun96/livekit-client-cpp/pull/4)) ([d78c1ea](https://github.com/zesun96/livekit-client-cpp/commit/d78c1ead5ea1a63030d525832d3e4a2610d86425)).
- 2024-12-02: add LICENSE ([#3](https://github.com/zesun96/livekit-client-cpp/pull/3)) ([d930450](https://github.com/zesun96/livekit-client-cpp/commit/d93045019d52953397cbda7f2e2ab179ae61d385)).
- 2024-12-02: add deps: plog; websocket-client ([#2](https://github.com/zesun96/livekit-client-cpp/pull/2)) ([b2ce7c1](https://github.com/zesun96/livekit-client-cpp/commit/b2ce7c1af634cba24a3f9ceca4209a27d4680822)).
- 2024-12-02: add docs; ([#1](https://github.com/zesun96/livekit-client-cpp/pull/1)) ([2f2be9b](https://github.com/zesun96/livekit-client-cpp/commit/2f2be9b7a5f1e92c38af78b1c5030a83a2a6d228)).
- 2024-11-04: add livekit cpp init and samples init; ([86a2a6f](https://github.com/zesun96/livekit-client-cpp/commit/86a2a6fb1a223f5a2b2468d510f0edeeb9256b18)).
- 2024-11-04: add protocol modules; ([f1aa5a3](https://github.com/zesun96/livekit-client-cpp/commit/f1aa5a301cafa04ad6da012cb346bcb576aa6816)).
- 2024-11-04: git init ([213f4ba](https://github.com/zesun96/livekit-client-cpp/commit/213f4baa01a91115cd0bd7e5a1199d0672464a01)).

### Changed

- 2026-08-25: release(version): bump version to 0.1.0 ([#89](https://github.com/zesun96/livekit-client-cpp/pull/89)) ([6b11cce](https://github.com/zesun96/livekit-client-cpp/commit/6b11ccebbe72678e24a93d29424aaf207b25e266)).
- 2026-08-11: refactor: modernize dependencies and client lifecycle ([#42](https://github.com/zesun96/livekit-client-cpp/pull/42)) ([4565e69](https://github.com/zesun96/livekit-client-cpp/commit/4565e69d0f18c6ebdc6abc5b51edbb7c32c94ba1)).
- 2025-05-14: use unix eol ([#34](https://github.com/zesun96/livekit-client-cpp/pull/34)) ([8a6195d](https://github.com/zesun96/livekit-client-cpp/commit/8a6195d2bf8ea549c7be38aa5676ba6f25b7d951)).
- 2025-05-14: use unix file eol; ([#33](https://github.com/zesun96/livekit-client-cpp/pull/33)) ([2351c1a](https://github.com/zesun96/livekit-client-cpp/commit/2351c1ab7d898f0bb0aad9956749d944374db77a)).
- 2025-05-14: connect peer; ([#32](https://github.com/zesun96/livekit-client-cpp/pull/32)) ([a493e82](https://github.com/zesun96/livekit-client-cpp/commit/a493e824896a658863d5fda2df6d9432a8cec77e)).
- 2025-04-06: handle ws message and add signal request; ([#27](https://github.com/zesun96/livekit-client-cpp/pull/27)) ([0bab861](https://github.com/zesun96/livekit-client-cpp/commit/0bab861dbcfae994bfe7371f4041786ca95162e4)).
- 2025-04-04: 1. add vs2020 builder; 2. rtc_session code; ([#26](https://github.com/zesun96/livekit-client-cpp/pull/26)) ([1c53a44](https://github.com/zesun96/livekit-client-cpp/commit/1c53a445ea85651a045070f326fbfd57e76c035a)).
- 2025-03-09: handle ws message and fixed SSL conflicts ; ([#25](https://github.com/zesun96/livekit-client-cpp/pull/25)) ([be6da7c](https://github.com/zesun96/livekit-client-cpp/commit/be6da7cd79774a2a7fefb5a5731fe40fe2833ba1)).
- 2025-02-16: use warmcat/libwebsockets replace zesun96/websocket-client; ([#23](https://github.com/zesun96/livekit-client-cpp/pull/23)) ([3e392cb](https://github.com/zesun96/livekit-client-cpp/commit/3e392cb5467263cefaa76e093f9e6969595c0b36)).
- 2025-01-19: code format ([#22](https://github.com/zesun96/livekit-client-cpp/pull/22)) ([28a9056](https://github.com/zesun96/livekit-client-cpp/commit/28a90563b7a115d68a8b4b053bb9f591e3cf429d)).
- 2025-01-19: update readme ([#19](https://github.com/zesun96/livekit-client-cpp/pull/19)) ([0ad5c07](https://github.com/zesun96/livekit-client-cpp/commit/0ad5c075694ccdbaab7a709f78ea9065f90f3fc8)).
- 2024-12-30: switch the webrtc library download repository ([#16](https://github.com/zesun96/livekit-client-cpp/pull/16)) ([d961256](https://github.com/zesun96/livekit-client-cpp/commit/d96125691b3985558654803b323ebe268e0a4b00)).
- 2024-12-29: process after join the room and create  peerconnect ([#14](https://github.com/zesun96/livekit-client-cpp/pull/14)) ([7f36cfe](https://github.com/zesun96/livekit-client-cpp/commit/7f36cfe539f64c2c236ee6a8bcbbf34d796cd1ee)).
- 2024-12-28: code format ([#13](https://github.com/zesun96/livekit-client-cpp/pull/13)) ([1e491f5](https://github.com/zesun96/livekit-client-cpp/commit/1e491f5d807cb6bc3f1c5b9b2c0ede5f96a58202)).
- 2024-12-25: update readme; ([#11](https://github.com/zesun96/livekit-client-cpp/pull/11)) ([59f4458](https://github.com/zesun96/livekit-client-cpp/commit/59f44586be81412bfe1af5e210e33a8e5d21fb33)).
- 2024-12-08: update deps; ([#6](https://github.com/zesun96/livekit-client-cpp/pull/6)) ([b65cf78](https://github.com/zesun96/livekit-client-cpp/commit/b65cf78ee9491afd1299cdc0762f11b1f32ec23f)).

### Fixed

- 2026-08-25: fix(reconnect): stabilize data track republication ([#86](https://github.com/zesun96/livekit-client-cpp/pull/86)) ([f58d354](https://github.com/zesun96/livekit-client-cpp/commit/f58d354c63e1db6d2c3ad15ea396d2f6dfac8e7f)).
- 2026-08-13: fix: restore tracks after subscription permission changes ([#59](https://github.com/zesun96/livekit-client-cpp/pull/59)) ([46e118d](https://github.com/zesun96/livekit-client-cpp/commit/46e118d10492f1afff029f94cc4afe9b76fa60c7)).
- 2026-08-12: fix: stabilize room connection lifecycle ([#44](https://github.com/zesun96/livekit-client-cpp/pull/44)) ([1613379](https://github.com/zesun96/livekit-client-cpp/commit/16133793d41e3deff8655656c31ce94802703fc7)).
- 2026-08-12: fix: support webrtc m144 on windows ([#43](https://github.com/zesun96/livekit-client-cpp/pull/43)) ([ab6d33c](https://github.com/zesun96/livekit-client-cpp/commit/ab6d33caa61ec32b76cf664edde803b6ac7bc21d)).
- 2025-11-22: fix run crash whit websockt closed; ([#41](https://github.com/zesun96/livekit-client-cpp/pull/41)) ([bfd74f6](https://github.com/zesun96/livekit-client-cpp/commit/bfd74f613a235ed6d42bbfd36275ca2f850f0a30)).
- 2025-05-14: fix comments error ([#35](https://github.com/zesun96/livekit-client-cpp/pull/35)) ([8c27066](https://github.com/zesun96/livekit-client-cpp/commit/8c27066c0d73de160dddc33f484c0910bfa15936)).
- 2025-04-08: Fix ping thread not exit after ws closed; ([#31](https://github.com/zesun96/livekit-client-cpp/pull/31)) ([1d4f269](https://github.com/zesun96/livekit-client-cpp/commit/1d4f26952042819b1e319e4d2ce3c8fd37e7f831)).
- 2025-04-08: Fix  serialize crash when sdp is empty; ([#30](https://github.com/zesun96/livekit-client-cpp/pull/30)) ([e1a34c2](https://github.com/zesun96/livekit-client-cpp/commit/e1a34c2b92ff178c59cb9a22fe38df86d4791fdf)).
- 2025-01-19: fix lock conflict dependencies on OpenSSL introducing both WebSocket and WebRTC dependencies; ([#17](https://github.com/zesun96/livekit-client-cpp/pull/17)) ([fc20699](https://github.com/zesun96/livekit-client-cpp/commit/fc206995dc383a2e36c6c5f75faf62fde8a84eea)).

[Unreleased]: https://github.com/zesun96/livekit-client-cpp/compare/v0.7.0...HEAD
[0.7.0]: https://github.com/zesun96/livekit-client-cpp/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/zesun96/livekit-client-cpp/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/zesun96/livekit-client-cpp/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/zesun96/livekit-client-cpp/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/zesun96/livekit-client-cpp/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/zesun96/livekit-client-cpp/compare/6b11ccebbe72678e24a93d29424aaf207b25e266...v0.2.0
[0.1.0]: https://github.com/zesun96/livekit-client-cpp/commit/6b11ccebbe72678e24a93d29424aaf207b25e266
