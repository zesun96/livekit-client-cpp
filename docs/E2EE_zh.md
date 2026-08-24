# 端到端加密（E2EE）

本 SDK 使用纯 C++ 实现兼容 LiveKit 的 AES-GCM 端到端加密。公开 API 遵循项目自己的 RAII、
值快照、错误对象和非拥有观察指针风格，不依赖 LiveKit Rust Core、Rust FFI 或 Rust 运行时。

## 配置房间

需要互相通信的参与者必须通过应用自己的安全通道获得相同密钥。不要把房间 token、API secret
或长期加密密钥硬编码到程序中。

```cpp
#include <livekit/core/livekit_client.h>

livekit::core::RoomOptions options;
options.e2ee.emplace();
options.e2ee->encryption_type = livekit::core::EncryptionType::Gcm;
options.e2ee->shared_key = livekit::core::E2eeKey{
    // 由应用的安全密钥分发系统提供的原始密钥字节。
};

auto room = livekit::core::CreateRoomUnique();
room->Connect(url, token, options);
```

`RoomOptions::e2ee` 未设置或加密类型为 `EncryptionType::None` 时不启用 E2EE。
`EncryptionType::Custom` 当前会被明确拒绝，不会静默降级为明文。

默认的 PBKDF2-SHA256 派生方式兼容其他 LiveKit SDK 使用的密码/字符串密钥材料；对于随机原始
密钥材料也可使用 HKDF-SHA256。所有通信端必须使用兼容的密钥材料、派生算法、salt、密钥槽位
和 ratchet 配置。

## 密钥和运行时控制

`RoomInterface::GetE2EEManager()` 返回由 Room 拥有的非拥有指针。不要删除该指针，也不要让它
跨越 Room 的生命周期。

```cpp
auto* e2ee = room->GetE2EEManager();
if (e2ee != nullptr) {
    e2ee->Keys().SetSharedKey(next_key, 1);
    e2ee->SetDataKeyIndex(1);
    e2ee->SetFrameCryptorKeyIndex(
        track_sid, livekit::core::FrameCryptorDirection::Sender, 1);
    e2ee->SetParticipantEnabled("participant-identity", true);
}
```

应用既可以使用共享 key ring，也可以通过
`KeyProvider::SetKey(participant_identity, key, index)` 设置参与者独立密钥。
`RatchetSharedKey` 和 `RatchetKey` 执行显式前向轮换；发生认证失败时，接收端也会在配置的
ratchet window 内尝试前向派生。`FrameCryptors()` 返回独立值快照，不暴露内部 WebRTC 对象。

运行时关闭或重新启用 E2EE 会重新发布已有本地轨道，以保证信令声明与媒体加密状态一致。应把
它视为可见的房间状态切换，而不是逐包开关。

## 状态事件

应用可实现 `RoomEventInterface::OnEncryptionStateChanged`，或直接设置
`E2EEManager::SetStateCallback`。状态包括成功、缺少密钥、加密失败、解密失败、自动 ratchet
成功和内部错误。回调可能来自 E2EE 工作线程，不应在回调中执行长时间阻塞操作。

## 覆盖范围

- 编码音频，以及 VP8、H264、AV1 和在 libwebrtc 启用时的 H265 视频；
- UserPacket、ChatMessage、RPC 和 DataStream Header/Chunk/Trailer；
- SIP DTMF、speaker update、metrics 和 transcription 按 LiveKit 客户端行为保持在 E2EE
  包装之外；
- 取消发布、取消订阅、参与者离开、断开连接和 full reconnect 时自动卸载 cryptor。

传输层仍应使用 TLS/WSS。E2EE 保护支持的媒体和数据载荷，但不能替代服务端认证、token 权限、
安全密钥分发、终端安全或流量分析防护。

## 验证

functional 测试包含固定 PBKDF2/HKDF 向量、编码音视频 SFrame 往返、数据加密、畸形包检查、
密钥槽位、自动 ratchet 和 AV1 RTP 封包/解包。可选的真实房间测试
`LiveKitServerTest.EncryptsAudioVideoAndDataEndToEnd` 会在两个 C++ 客户端之间验证加密音频、
VP8、H264 或 AV1 视频、数据、信令加密元数据、状态事件、媒体和数据密钥槽位从 0 切换到 1
再切回，以及在线共享密钥轮换。配套测试还会验证 publisher 的 full/media reconnect、
subscriber 的 signal resume、缺失密钥、错误密钥以及设置正确密钥后的恢复。三个 codec 均已在
Windows x64 的 LiveKit Server 1.13.5 上通过验证。

`LiveKitServerTest.InteroperatesWithOfficialJsE2EEPeer` 和
`test/integration/run_js_e2ee_interop.mjs` 还会验证：官方 JS SDK 2.21.0 可以解密本 SDK 的
音频、VP8 或 H264 视频和数据，本 SDK 可以解密 JS 对端返回的数据。该测试同时覆盖官方 JS 省略
proto3 默认字段 `EncryptedPacket.encryption_type` 的线上行为，并已在 LiveKit Server 1.13.5
上通过验证。

`LiveKitServerTest.InteroperatesWithOfficialCppE2EEPeer` 会针对官方 C++ SDK v1.8.0 验证加密
音频、VP8 或 H264 视频和双向数据。使用官方 SDK 解压目录配置并构建外部测试夹具：

```powershell
cmake -S . -B out/build/vs2022-x64-release `
  -DBUILD_INTEGRATION_TESTS=ON `
  -DOFFICIAL_LIVEKIT_CPP_ROOT=C:/path/to/livekit-sdk-windows-x64-1.8.0
cmake --build out/build/vs2022-x64-release --config Release `
  --target livekit_server_integration_tests livekit_official_cpp_e2ee_peer
```

将 `LIVEKIT_URL`、`LIVEKIT_TOKEN` 和 `LIVEKIT_TOKEN_2` 设置为同一房间内两个不同身份后，
即可运行 integration 测试。也可以使用本地服务器测试脚本：

```powershell
.\test\integration\run_reconnect_matrix.ps1 `
  -ServerExecutable "C:\path\to\livekit-server.exe" `
  -LkExecutable "C:\path\to\lk.exe" `
  -ApiKey "devkey" `
  -ApiSecret "a-development-secret-with-at-least-32-characters" `
  -Scenario E2EE `
  -VideoCodec h264
```

单独下载的官方 JS SDK 安装依赖并构建后，可运行浏览器互操作夹具：

```powershell
node .\test\integration\run_js_e2ee_interop.mjs `
  --official-js-sdk "C:\path\to\client-sdk-js" `
  --config "C:\path\to\livekit-server-config.yaml" `
  --video-codec h264
```

官方 C++ 夹具的运行方式：

```powershell
.\test\integration\run_reconnect_matrix.ps1 `
  -ServerExecutable "C:\path\to\livekit-server.exe" `
  -LkExecutable "C:\path\to\lk.exe" `
  -ApiKey "devkey" `
  -ApiSecret "a-development-secret-with-at-least-32-characters" `
  -Scenario OfficialCpp `
  -VideoCodec h264 `
  -OfficialCppPeerExecutable `
    ".\out\build\vs2022-x64-release\test\integration\Release\livekit_official_cpp_e2ee_peer.exe"
```

AV1 E2EE 使用可路由的 AV1 OBU 封装，已在两个本 SDK 客户端之间通过验证。官方 JS SDK
2.21.0 会拒绝 AV1 E2EE，目前也不声明与官方 C++ SDK 的 AV1 E2EE 互操作。官方 SDK 只作为
外部测试程序；本 SDK 库不链接它们的 Rust Core，也不采用其公开 API 或所有权模型。
