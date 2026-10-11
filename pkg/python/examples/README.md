# Python examples

These examples mirror the Lua SDK's feature-oriented examples while using the
Python binding's async API. Build `livekitclient.dll`, make its native dependencies
discoverable, set `LIVEKIT_CLIENT_CPP_LIBRARY`, and run from this directory (or
use the full script path). Connection examples read `LIVEKIT_URL` and
`LIVEKIT_TOKEN`; `--url` and `--token` override them. Tokens need room-join
permission. For two-participant examples, use distinct identities in one room.

| Example | What it does | Opens a physical device? |
| --- | --- | --- |
| `media_devices.py` | List audio/video devices and screen sources | No |
| `room_echo.py` | Join, greet, and print data/chat | No |
| `room_event.py` | Log participant, track, data, chat, and stream events | No |
| `data_transfer.py` | Send text, bytes, optional file, and data | No |
| `stream_writer.py` | Write incremental text stream | No |
| `data_track_schema.py` | Store JSON schema and publish DataTrack frame | No |
| `token_source.py` | Supply connection credentials through callback | No |
| `rpc_receiver.py`, `rpc_caller.py` | Register and call `example.echo` | No |
| `publish_audio.py`, `publish_video.py` | Publish synthetic tone or I420 video | No |
| `receive_media.py` | Manually subscribe and read remote frames | No |
| `receive_media_events.py` | Receive remote frames through callbacks | No |
| `record_remote.py` | Save remote audio as WAV and video in its encoded format | No |
| `publish_microphone.py`, `publish_system_audio.py` | Publish microphone or output loopback | Yes |
| `publish_camera.py`, `publish_screen.py` | Publish camera or monitor capture | Yes |
| `two_peer_smoke.py` | Check two Python peers, data, RPC, streams, DataTrack, and media | No |

Run a receiver first in one terminal, then a publisher in another:

```powershell
python pkg/python/examples/receive_media.py --seconds 15
python pkg/python/examples/publish_video.py --seconds 5
```

For RPC, run `rpc_receiver.py --seconds 30` first. Pass its printed identity
to `rpc_caller.py <identity>`. To record, create an output directory and run
`record_remote.py <output_dir> --seconds 15` alongside a publisher.
`data_track_schema.py` requires the server's participant data blob feature.
The token source example's environment lookup is illustrative; production
applications should fetch fresh tokens from their own authenticated service.

On this repository's Windows layout, `run_local_smoke.ps1` starts the local
LiveKit server, creates temporary test tokens, runs `two_peer_smoke.py`, and
cleans up. Pass `-Example publish_audio`, `-Example publish_video`,
`-Example data_transfer`, `-Example stream_writer`, `-Example token_source`,
`-Example data_track_schema`, `-Example receive_media`,
`-Example receive_media_events`, `-Example record_remote`, or `-Example rpc`
to run one of those examples instead. The default DLL directory is
`out/build/sdk-dll-md-release/Release`; pass `-BuildDirectory` to change it.
The smoke test's tokens can also be supplied manually through
`LIVEKIT_TOKEN_A` and `LIVEKIT_TOKEN_B` with `LIVEKIT_URL`.

The four physical-capture scripts require a usable device or source. Pass
`--device-id` to select one; `publish_screen.py` chooses the first monitor by
default. They do not run as part of the local smoke test.
