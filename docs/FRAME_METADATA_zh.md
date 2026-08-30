# 视频帧元数据

SDK 可以在每个外部输入视频帧上携带端到端应用元数据。它与
`VideoFrame::timestamp_us` 相互独立；后者仍是 WebRTC 使用的采集时间戳。

支持三个可选字段：

- `user_timestamp_us`：应用定义的 64 位无符号时间戳；
- `frame_id`：应用定义的 32 位无符号帧编号；
- `user_data`：最多 232 字节的不透明应用数据。

## C++ 发布与接收

在输入帧上设置元数据，并在发布选项中声明需要传输的字段：

```cpp
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

只有同时“存在于当前帧”并且“在发布选项中启用”的字段才会发送。超过
`kMaxVideoFrameMetadataUserDataSize` 的 `user_data` 会使 `CaptureFrame()` 返回失败。

接收端可从回调帧或 `VideoStream` 拉取帧的 `VideoFrame::metadata` 读取数据。发布端未发送、
字段未声明，或旧版本发布端/服务器不支持 packet trailer 时，该字段为空，因此应用必须按可选值
处理。完整示例见 `examples/publish_video` 和 `examples/room_event`。

## C API

使用前先调用 `lk_video_frame_metadata_init()`、`lk_video_frame_input_init()` 和
`lk_track_publish_options_init()`。将 metadata 指针放入 `lk_video_frame_input_t::metadata`，并在
`lk_track_publish_options_t::frame_metadata_features` 中启用对应字段。

接收时可设置 `lk_room_callbacks_t::on_video_frame_with_metadata`。旧的 `on_video_frame` 回调仍会
正常调用。拉取式视频流通过 `lk_owned_video_frame_metadata()` 返回借用视图，视图有效期不超过对应
owned frame。

## 兼容性与 E2EE

连接时 SDK 声明 LiveKit `CAP_PACKET_TRAILER`，发布时通过 `AddTrackRequest` 声明字段，线上使用与
官方 SDK 兼容的 `LKTS` encoded-frame trailer。接收 transformer 对所有视频轨安全安装，无 trailer
时仅透传，从而避免媒体事件早于信令更新时丢失元数据。

开启媒体 E2EE 时，发送端先加密编码帧，再追加 metadata；接收端先剥离 metadata，再解密编码帧。
因此服务器不需要解密媒体即可转发 trailer，但 metadata 本身不加密，请勿在 `user_data` 中放入
秘密信息。
