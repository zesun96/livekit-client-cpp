# Video frame metadata

The SDK can carry application metadata end to end with each externally supplied video frame. The
metadata is independent of `VideoFrame::timestamp_us`, which remains the capture timestamp used by
WebRTC.

Supported fields are:

- `user_timestamp_us`: an application-defined unsigned 64-bit timestamp;
- `frame_id`: an application-defined unsigned 32-bit frame identifier; and
- `user_data`: zero to 232 opaque bytes.

## C++ publishing and receiving

Set metadata on each input frame and advertise the fields in the publish options:

```cpp
livekit::core::VideoFrame frame;
frame.width = 640;
frame.height = 360;
frame.format = livekit::core::VideoBufferType::RGBA;
frame.data = rgba;
frame.timestamp_us = capture_time_us;
frame.metadata = livekit::core::VideoFrameMetadata{};
frame.metadata->user_timestamp_us = application_time_us;
frame.metadata->frame_id = frame_number;
frame.metadata->user_data = std::vector<std::uint8_t>{0x01, 0x02};

livekit::core::TrackPublishOptions options;
options.frame_metadata_features =
    livekit::core::FrameMetadataFeatures{true, true, true};

source->CaptureFrame(frame);
participant->PublishTrack(track.get(), options);
```

Every field is optional. A field is sent only when it is present on the frame and enabled in
`frame_metadata_features`. `CaptureFrame()` rejects user data larger than
`kMaxVideoFrameMetadataUserDataSize`.

Received callback frames expose `VideoFrame::metadata`. `VideoStream` readers expose the same
metadata on the owned frame returned by the reader. Metadata may be absent because the publisher
did not send it, the field was not advertised, or an older publisher/server does not support packet
trailers. Applications must therefore treat it as optional.

The complete publisher and callback examples are in `examples/publish_video` and
`examples/room_event`.

## C API

Initialize every size-versioned structure before filling it:

```c
lk_video_frame_metadata_t metadata;
lk_video_frame_metadata_init(&metadata);
metadata.has_frame_id = 1;
metadata.frame_id = frame_number;
metadata.has_user_data = 1;
metadata.user_data = bytes;
metadata.user_data_size = byte_count;

lk_video_frame_input_t frame;
lk_video_frame_input_init(&frame);
frame.data = rgba;
frame.data_size = rgba_size;
frame.width = 640;
frame.height = 360;
frame.format = LK_VIDEO_BUFFER_RGBA;
frame.timestamp_us = capture_time_us;
frame.metadata = &metadata;

lk_track_publish_options_t options;
lk_track_publish_options_init(&options);
options.frame_metadata_features.frame_id = 1;
options.frame_metadata_features.user_data = 1;
```

Set `lk_room_callbacks_t::on_video_frame_with_metadata` to receive a borrowed metadata view beside
the existing video frame view. The original `on_video_frame` callback remains supported and is
still invoked. Pull readers use `lk_owned_video_frame_metadata()`; returned pointers remain valid
until that owned frame is destroyed.

## Wire, compatibility, and E2EE

The SDK advertises LiveKit `CAP_PACKET_TRAILER` support at room connection and requested trailer
fields in `AddTrackRequest`. Metadata uses the `LKTS` encoded-frame trailer shared with official
LiveKit SDKs. The implementation installs a receiver pass-through transformer for all video tracks
so a media-before-signalling race cannot discard metadata.

With media E2EE, the sender encrypts the encoded frame before appending metadata. The receiver
extracts and strips metadata before decrypting the encoded frame. Consequently the SFU can forward
the trailer without decrypting media; the metadata itself is not encrypted. Do not place secrets in
`user_data`.
