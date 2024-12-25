# livekit-client-cpp-sdk

C++ client SDK for LiveKit in C++17.

## Support Platforms

- [x] Microsoft Windows
- [ ] GNU/Linux
- [ ] Apple macOS
- [ ] iOS
- [ ] Android

## Features

- [ ] Client Websocket Signal
- [ ] Receiving tracks
- [ ] Publishing tracks
- [ ] Data channels
- [ ] E2ee

## Dependencies

- [livekit-protocol](https://github.com/livekit/protocol)(as submodule by default)
- [plog](https://github.com/SergiusTheBest/plog) (as submodule by default)
- [websocket-client](https://github.com/zesun96/websocket-client) (as submodule by default)
- [libwebrtc](https://github.com/livekit/rust-sdks/releases) (cmake FetchContent by default)
- [protobuf](https://github.com/protocolbuffers/protobuf) (cmake find_package by default)
- [nlohmann_json](https://github.com/nlohmann/json) (as submodule by default)

## Examples

See [examples](./examples/)

## Thanks

- [livekit](https://livekit.io/)
- [webrtc-sdk](https://github.com/webrtc-sdk)
- [protobuf](https://github.com/protocolbuffers/protobuf)
- [nlohmann_json](https://github.com/nlohmann/json)
- [plog](https://github.com/SergiusTheBest/plog)
- [websocket-client](https://github.com/zesun96/websocket-client)
- [vcpkg](https://github.com/livekit/protocol)

## License

livekit-client-cpp is licensed under Apache License v2.0.
