# Python examples

`room_echo.py` joins an existing room, publishes a greeting, and prints
incoming data and chat. Set `LIVEKIT_URL`, `LIVEKIT_TOKEN`, and
`LIVEKIT_CLIENT_CPP_LIBRARY` before running it.

`two_peer_smoke.py` verifies two Python participants against a real server.
Set `LIVEKIT_URL`, `LIVEKIT_TOKEN_A`, `LIVEKIT_TOKEN_B`, and the native library
path, then run `python pkg/python/examples/two_peer_smoke.py`. The two tokens
must join the same room with distinct identities. The script uses generated
audio and video frames and does not open a physical media device.

On this repository's Windows development layout, run
`pkg/python/examples/run_local_smoke.ps1`. It starts the checked-in local
LiveKit server, creates short-lived test tokens with the checked-in LiveKit
CLI, runs the two-peer example, then stops the server and removes its temporary
config and logs. The script requires a built `livekitclient.dll` in
`out/build/sdk-dll-md-release/Release`, or pass `-BuildDirectory`.
