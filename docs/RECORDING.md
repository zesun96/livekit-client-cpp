# Remote track recording

The native C++ API can record one subscribed remote track to one file. The implementation follows
the per-track file-saving model used by the LiveKit Go SDK: received video stays encoded, while
audio is written from libwebrtc's decoded PCM output.

## Start and stop a recorder

Start recording from `RoomEventInterface::OnTrackSubscribed()` and retain the returned recorder for
as long as recording should continue:

```cpp
#include <livekit/core/livekit_client.h>

std::vector<std::unique_ptr<livekit::core::TrackRecorder>> recorders;

void OnTrackSubscribed(livekit::core::RemoteTrackInterface* track) override {
    livekit::core::TrackRecorderOptions options;
    options.output_path = "recordings/" + track->Sid();
    options.queue_capacity = 256;

    std::string error;
    auto recorder = livekit::core::StartTrackRecording(*track, options, &error);
    if (recorder) {
        recorders.push_back(std::move(recorder));
    } else {
        // Report `error` through the application's logging path.
    }
}
```

Call `TrackRecorder::Stop()` before inspecting or moving the output file. `Stop()` is idempotent,
waits for the writer thread, and finalizes the WAV or IVF header. Destroying the recorder also calls
`Stop()`.

`TrackRecorder::Stats()` reports the state, resolved output path, written frames and payload bytes,
dropped input frames, and a terminal error. The parent directory in `output_path` must already
exist.

## Output formats

| Remote media | Received codec | File |
| --- | --- | --- |
| Audio | libwebrtc-decoded signed PCM16 | RIFF/WAVE (`.wav`) |
| Video | H264 | Annex-B elementary stream (`.h264`) |
| Video | H265 | Annex-B elementary stream (`.h265`) |
| Video | VP8 or VP9 | IVF (`.ivf`) |
| Video | AV1 | IVF (`.ivf`) |

The matching extension is appended unless it is already present. Encoded video recording starts at
a key frame. If the bounded queue drops video, the recorder skips dependent frames until the next
key frame so it does not deliberately append an undecodable dependency chain.

Applications that need direct access to received encoded video can use
`RemoteTrackInterface::CreateEncodedVideoStream()` and consume owned `EncodedVideoFrame` values.
H264/H265 frames use Annex-B start codes; the other supported codecs contain one complete encoded
frame per value.

## Scope and limitations

- This is local, per-remote-track recording. It does not invoke LiveKit Egress and does not mix,
  composite, transcode, or synchronize multiple tracks.
- Audio is WAV rather than Ogg Opus because libwebrtc exposes the stable remote audio sink after
  decode, while its encoded-audio receive path is not a public recording interface.
- A video codec change during one recording is a terminal error. Start a new recorder for the new
  codec.
- Classic RIFF/WAVE output is limited to 4 GiB. Rotate the recorder before reaching that limit.
- Raw H264/H265 and IVF files are useful for capture and later processing but are not MP4 files.
  Container muxing can be added as a separate layer later. Bento4 is not linked into the SDK by
  this implementation.
- Recording encrypted-room video has not yet been validated as an E2EE interoperability contract.
  Do not rely on this path for encrypted production recordings until that validation is added.
