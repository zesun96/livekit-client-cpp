# End-to-end encryption (E2EE)

This SDK implements LiveKit-compatible AES-GCM end-to-end encryption in pure C++. Its public API
uses the project's own RAII, value-snapshot, error-object, and non-owning observer conventions. It
does not depend on LiveKit Rust Core, a Rust FFI, or a Rust runtime.

## Configure a room

Every participant that needs to communicate must receive the same key through an application-owned
secure channel. Never embed room tokens, API secrets, or long-lived encryption keys in an
application binary.

```cpp
#include <livekit/core/livekit_client.h>

livekit::core::RoomOptions options;
options.e2ee.emplace();
options.e2ee->encryption_type = livekit::core::EncryptionType::Gcm;
options.e2ee->shared_key = livekit::core::E2eeKey{
    // Raw key bytes supplied by the application's secure key-distribution system.
};

auto room = livekit::core::CreateRoomUnique();
room->Connect(url, token, options);
```

E2EE is disabled when `RoomOptions::e2ee` is absent or its encryption type is
`EncryptionType::None`. `EncryptionType::Custom` is rejected explicitly instead of silently
downgrading the room to plaintext.

The default PBKDF2-SHA256 derivation is compatible with password/string key material used by other
LiveKit SDKs. HKDF-SHA256 is available for raw random key material. All peers must use compatible
key material, derivation, salt, key slot, and ratchet settings.

## Keys and runtime control

`RoomInterface::GetE2EEManager()` returns a non-owning pointer to the manager owned by the room. Do
not delete it or retain it beyond the room's lifetime.

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

Applications may use the shared key ring or assign participant-specific keys with
`KeyProvider::SetKey(participant_identity, key, index)`. `RatchetSharedKey` and `RatchetKey`
perform an explicit forward ratchet. Receivers also try forward-derived keys within the configured
ratchet window after an authentication failure. `FrameCryptors()` returns value snapshots and does
not expose internal WebRTC objects.

Disabling E2EE at runtime republishes existing local tracks so signaling and media encryption stay
consistent. Treat this as a visible room transition, not as a per-packet toggle.

## State events

Implement `RoomEventInterface::OnEncryptionStateChanged`, or install a callback directly with
`E2EEManager::SetStateCallback`. States cover success, missing keys, encryption/decryption failure,
successful automatic ratcheting, and internal errors. Callbacks may arrive on an E2EE worker thread;
they must not perform long blocking operations.

## Coverage

- Encoded audio and VP8, H264, AV1, and H265 video when H265 is enabled in libwebrtc.
- User packets, chat messages, RPC, and data-stream header/chunk/trailer packets.
- SIP DTMF, speaker updates, metrics, and transcription remain outside the E2EE wrapper, matching
  LiveKit client behavior.
- Cryptors are detached automatically on unpublish, unsubscribe, participant departure, disconnect,
  and full reconnect.

TLS/WSS is still required. E2EE protects supported media and data payloads; it does not replace
server authentication, token permissions, secure key distribution, endpoint security, or traffic
analysis protection.

## Verification

The functional suite includes fixed PBKDF2/HKDF vectors, encoded audio/video SFrame round trips,
data encryption, malformed-packet checks, key slots, automatic ratcheting, and AV1 RTP
packetization/depacketization. The opt-in real-room test
`LiveKitServerTest.EncryptsAudioVideoAndDataEndToEnd` verifies encrypted audio, VP8, H264, or AV1
video, data, signaled encryption metadata, state events, switching media and data key slots from 0
to 1 and back, and a live shared-key ratchet between two C++ clients. The companion reconnect and
key-error tests verify E2EE after publisher full/media reconnect, subscriber signal resume, missing
keys, incorrect keys, and recovery after the correct key is installed. All three codecs have been
verified against LiveKit Server 1.13.5 on Windows x64.

`LiveKitServerTest.InteroperatesWithOfficialJsE2EEPeer` and
`test/integration/run_js_e2ee_interop.mjs` additionally verify that the official JS SDK 2.21.0 can
decrypt this SDK's audio, VP8 or H264 video, and data, and that this SDK can decrypt data returned
by the JS peer. This test also covers the official JS wire behavior that omits the proto3-default
`EncryptedPacket.encryption_type` field. It has been verified against LiveKit Server 1.13.5.

`LiveKitServerTest.InteroperatesWithOfficialCppE2EEPeer` verifies encrypted audio, VP8 or H264
video, and bidirectional data against the official C++ SDK v1.8.0. Configure the external fixture
from an extracted official SDK archive and build both test executables:

```powershell
cmake -S . -B out/build/vs2022-x64-release `
  -DBUILD_INTEGRATION_TESTS=ON `
  -DOFFICIAL_LIVEKIT_CPP_ROOT=C:/path/to/livekit-sdk-windows-x64-1.8.0
cmake --build out/build/vs2022-x64-release --config Release `
  --target livekit_server_integration_tests livekit_official_cpp_e2ee_peer
```

Set `LIVEKIT_URL`, `LIVEKIT_TOKEN`, and `LIVEKIT_TOKEN_2` to two different identities in the same
room, then run the integration test. Alternatively, use the local-server harness:

```powershell
.\test\integration\run_reconnect_matrix.ps1 `
  -ServerExecutable "C:\path\to\livekit-server.exe" `
  -LkExecutable "C:\path\to\lk.exe" `
  -ApiKey "devkey" `
  -ApiSecret "a-development-secret-with-at-least-32-characters" `
  -Scenario E2EE `
  -VideoCodec h264
```

After installing and building the separately downloaded official JS SDK, run its browser-based
interoperability fixture with:

```powershell
node .\test\integration\run_js_e2ee_interop.mjs `
  --official-js-sdk "C:\path\to\client-sdk-js" `
  --config "C:\path\to\livekit-server-config.yaml" `
  --video-codec h264
```

Run the official C++ fixture with:

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

AV1 E2EE uses a routeable AV1 OBU envelope and is verified between two clients built from this SDK.
The official JS SDK 2.21.0 rejects AV1 E2EE, and official C++ AV1 E2EE interoperability is not
claimed. Official peers are external test programs only; this SDK never links their Rust Core into
the library or adopts their public API/ownership model.
