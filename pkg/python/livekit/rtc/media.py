"""External media sources and room-owned local tracks."""

from __future__ import annotations

import asyncio
import ctypes as C
import threading
from dataclasses import dataclass
from enum import IntEnum

from ._native import (
    AudioFrameRaw, AudioSourceOptionsRaw, LiveKitError, TrackPublishOptionsRaw,
    CameraCaptureOptionsRaw, MicrophoneCaptureOptionsRaw,
    MicrophoneProcessingStatsRaw, ScreenCaptureOptionsRaw,
    SystemAudioCaptureOptionsRaw, VideoFrameMetadataRaw, VideoFrameRaw,
    VideoSourceOptionsRaw, native, utf8,
)


class TrackSource(IntEnum):
    UNKNOWN = 0
    CAMERA = 1
    MICROPHONE = 2
    SCREEN_SHARE = 3
    SCREEN_SHARE_AUDIO = 4


@dataclass(frozen=True)
class AudioFrame:
    data: bytes
    sample_rate: int
    num_channels: int
    samples_per_channel: int

    @classmethod
    def from_native(cls, value: AudioFrameRaw) -> AudioFrame:
        return cls(
            data=C.string_at(value.data, value.sample_count * 2)
            if value.sample_count else b"",
            sample_rate=value.sample_rate, num_channels=value.num_channels,
            samples_per_channel=value.samples_per_channel,
        )


@dataclass(frozen=True)
class MicrophoneProcessingStats:
    capture_frames_processed: int
    render_frames_processed: int
    capture_processing_errors: int
    render_processing_errors: int
    frames_dropped: int
    echo_cancellation_enabled: bool
    echo_return_loss_db: float | None
    echo_return_loss_enhancement_db: float | None
    residual_echo_likelihood: float | None
    residual_echo_likelihood_recent_max: float | None
    delay_median_ms: int | None
    delay_standard_deviation_ms: int | None
    delay_ms: int | None


@dataclass(frozen=True)
class VideoFrame:
    data: bytes
    width: int
    height: int
    timestamp_us: int
    user_timestamp_us: int | None = None
    frame_id: int | None = None
    user_data: bytes | None = None

    @classmethod
    def from_native(cls, value: VideoFrameRaw, metadata: VideoFrameMetadataRaw | None = None):
        return cls(
            data=C.string_at(value.data, value.data_size) if value.data_size else b"",
            width=value.width, height=value.height, timestamp_us=value.timestamp_us,
            user_timestamp_us=(metadata.user_timestamp_us if metadata and
                               metadata.has_user_timestamp_us else None),
            frame_id=metadata.frame_id if metadata and metadata.has_frame_id else None,
            user_data=(C.string_at(metadata.user_data, metadata.user_data_size)
                       if metadata and metadata.has_user_data else None),
        )


@dataclass(frozen=True)
class TrackPublishOptions:
    source: TrackSource = TrackSource.UNKNOWN
    dtx: bool = True
    red: bool = True
    simulcast: bool = True
    stream: str = ""
    video_codec: int | None = None
    scalability_mode: str = "L3T3_KEY"
    preconnect_buffer: bool = False

    def _native(self, lib) -> TrackPublishOptionsRaw:
        raw = TrackPublishOptionsRaw()
        lib.lk_track_publish_options_init(C.byref(raw))
        raw.source = int(self.source)
        raw.dtx = int(self.dtx)
        raw.red = int(self.red)
        raw.simulcast = int(self.simulcast)
        raw.stream = utf8(self.stream)
        if self.video_codec is not None:
            raw.video_codec = int(self.video_codec)
        raw.scalability_mode = utf8(self.scalability_mode)
        raw.preconnect_buffer = int(self.preconnect_buffer)
        return raw


class AudioSource:
    """Signed 16-bit interleaved PCM source, external or device-backed."""

    def __init__(
        self, sample_rate: int = 48000, num_channels: int = 1,
        queue_size_ms: int = 1000, *, echo_cancellation: bool = False,
        auto_gain_control: bool = False, noise_suppression: bool = False,
    ):
        if (sample_rate <= 0 or sample_rate > 0xFFFFFFFF or sample_rate % 100 or
                num_channels <= 0 or num_channels > 0xFFFFFFFF or
                queue_size_ms <= 0 or queue_size_ms > 0xFFFFFFFF or
                queue_size_ms % 10):
            raise ValueError("invalid audio source sample rate, channels, or queue size")
        self._native = native()
        self._lock = threading.Lock()
        self._handle = C.c_void_p()
        options = AudioSourceOptionsRaw()
        self._native.lib.lk_audio_source_options_init(C.byref(options))
        options.sample_rate = sample_rate
        options.num_channels = num_channels
        options.queue_size_ms = queue_size_ms
        options.echo_cancellation = int(echo_cancellation)
        options.auto_gain_control = int(auto_gain_control)
        options.noise_suppression = int(noise_suppression)
        self._native.check(self._native.lib.lk_audio_source_create(
            C.byref(options), C.byref(self._handle)
        ))
        self.sample_rate = sample_rate
        self.num_channels = num_channels
        self._kind = "external"

    @classmethod
    def from_microphone(
        cls, device_id: str = "", *, queue_size_ms: int = 200,
        echo_cancellation: bool = True, auto_gain_control: bool = True,
        noise_suppression: bool = True,
    ) -> AudioSource:
        if queue_size_ms <= 0 or queue_size_ms > 0xFFFFFFFF or queue_size_ms % 10:
            raise ValueError("queue_size_ms must be a positive multiple of 10")
        result = cls.__new__(cls)
        result._native = native()
        result._lock = threading.Lock()
        result._handle = C.c_void_p()
        options = MicrophoneCaptureOptionsRaw()
        result._native.lib.lk_microphone_capture_options_init(C.byref(options))
        options.device_id = utf8(device_id)
        options.queue_size_ms = queue_size_ms
        options.echo_cancellation = int(echo_cancellation)
        options.auto_gain_control = int(auto_gain_control)
        options.noise_suppression = int(noise_suppression)
        result._native.check(result._native.lib.lk_audio_source_create_microphone(
            C.byref(options), C.byref(result._handle)
        ))
        result.sample_rate, result.num_channels = 48000, 1
        result._kind = "microphone"
        return result

    @classmethod
    def from_system_audio(
        cls, device_id: str = "", *, queue_size_ms: int = 200,
    ) -> AudioSource:
        if queue_size_ms <= 0 or queue_size_ms > 0xFFFFFFFF or queue_size_ms % 10:
            raise ValueError("queue_size_ms must be a positive multiple of 10")
        result = cls.__new__(cls)
        result._native = native()
        result._lock = threading.Lock()
        result._handle = C.c_void_p()
        options = SystemAudioCaptureOptionsRaw()
        result._native.lib.lk_system_audio_capture_options_init(C.byref(options))
        options.device_id = utf8(device_id)
        options.queue_size_ms = queue_size_ms
        result._native.check(result._native.lib.lk_audio_source_create_system_audio(
            C.byref(options), C.byref(result._handle)
        ))
        result.sample_rate, result.num_channels = 48000, 2
        result._kind = "system_audio"
        return result

    def _capture_function(self, suffix: str):
        if self._kind == "external":
            raise LiveKitError("external audio source has no capture device")
        return getattr(self._native.lib, f"lk_audio_source_{self._kind}_{suffix}")

    async def start_capture(self) -> None:
        await asyncio.to_thread(self._capture_operation, "start")

    async def stop_capture(self) -> None:
        await asyncio.to_thread(self._capture_operation, "stop")

    def _capture_operation(self, suffix: str) -> None:
        with self._lock:
            self._assert_open()
            self._native.check(self._capture_function(suffix)(self._handle))

    @property
    def is_capturing(self) -> bool:
        with self._lock:
            self._assert_open()
            return bool(self._capture_function("is_capturing")(self._handle))

    @property
    def device_id(self) -> str:
        with self._lock:
            self._assert_open()
            self._capture_function("device_id")
            return self._native.string(f"lk_audio_source_{self._kind}_device_id", self._handle)

    async def switch_device(self, device_id: str) -> None:
        encoded = utf8(device_id)

        def switch():
            with self._lock:
                self._assert_open()
                self._native.check(self._capture_function("switch_device")(
                    self._handle, encoded
                ))
        await asyncio.to_thread(switch)

    def _assert_microphone(self):
        if self._kind != "microphone":
            raise LiveKitError("microphone source is required")

    @property
    def muted(self) -> bool:
        with self._lock:
            self._assert_open()
            self._assert_microphone()
            return bool(self._native.lib.lk_audio_source_microphone_is_muted(self._handle))

    async def set_muted(self, muted: bool) -> None:
        await asyncio.to_thread(self._set_microphone_value, "set_muted", int(muted))

    @property
    def volume(self) -> float:
        with self._lock:
            self._assert_open()
            self._assert_microphone()
            return float(self._native.lib.lk_audio_source_microphone_volume(self._handle))

    async def set_volume(self, volume: float) -> None:
        await asyncio.to_thread(self._set_microphone_value, "set_volume", volume)

    def _set_microphone_value(self, suffix: str, value) -> None:
        with self._lock:
            self._assert_open()
            self._assert_microphone()
            fn = getattr(self._native.lib, "lk_audio_source_microphone_" + suffix)
            self._native.check(fn(self._handle, value))

    @property
    def processing_options(self) -> tuple[bool, bool, bool]:
        with self._lock:
            self._assert_open()
            self._assert_microphone()
            raw = AudioSourceOptionsRaw()
            self._native.lib.lk_audio_source_options_init(C.byref(raw))
            self._native.check(self._native.lib.lk_audio_source_microphone_processing_options(
                self._handle, C.byref(raw)
            ))
            return (bool(raw.echo_cancellation), bool(raw.auto_gain_control),
                    bool(raw.noise_suppression))

    async def set_processing_options(
        self, *, echo_cancellation: bool, auto_gain_control: bool,
        noise_suppression: bool,
    ) -> None:
        raw = AudioSourceOptionsRaw()
        self._native.lib.lk_audio_source_options_init(C.byref(raw))
        raw.echo_cancellation = int(echo_cancellation)
        raw.auto_gain_control = int(auto_gain_control)
        raw.noise_suppression = int(noise_suppression)

        def update():
            with self._lock:
                self._assert_open()
                self._assert_microphone()
                self._native.check(
                    self._native.lib.lk_audio_source_microphone_set_processing_options(
                        self._handle, C.byref(raw)
                    )
                )
        await asyncio.to_thread(update)

    @property
    def processing_stats(self) -> MicrophoneProcessingStats:
        with self._lock:
            self._assert_open()
            self._assert_microphone()
            raw = MicrophoneProcessingStatsRaw()
            self._native.lib.lk_microphone_processing_stats_init(C.byref(raw))
            self._native.check(self._native.lib.lk_audio_source_microphone_processing_stats(
                self._handle, C.byref(raw)
            ))
            return MicrophoneProcessingStats(
                capture_frames_processed=raw.capture_frames_processed,
                render_frames_processed=raw.render_frames_processed,
                capture_processing_errors=raw.capture_processing_errors,
                render_processing_errors=raw.render_processing_errors,
                frames_dropped=raw.frames_dropped,
                echo_cancellation_enabled=bool(raw.echo_cancellation_enabled),
                echo_return_loss_db=(raw.echo_return_loss_db
                                     if raw.echo_return_loss_available else None),
                echo_return_loss_enhancement_db=(raw.echo_return_loss_enhancement_db
                    if raw.echo_return_loss_enhancement_available else None),
                residual_echo_likelihood=(raw.residual_echo_likelihood
                    if raw.residual_echo_likelihood_available else None),
                residual_echo_likelihood_recent_max=(raw.residual_echo_likelihood_recent_max
                    if raw.residual_echo_likelihood_recent_max_available else None),
                delay_median_ms=raw.delay_median_ms if raw.delay_median_available else None,
                delay_standard_deviation_ms=(raw.delay_standard_deviation_ms
                    if raw.delay_standard_deviation_available else None),
                delay_ms=raw.delay_ms if raw.delay_available else None,
            )

    def _assert_open(self):
        if not self._handle:
            raise LiveKitError("audio source is closed")

    def close(self) -> None:
        with self._lock:
            if self._handle:
                self._native.check(self._native.lib.lk_audio_source_destroy(self._handle))
                self._handle = C.c_void_p()

    async def capture_frame(self, pcm16: bytes | AudioFrame) -> None:
        if isinstance(pcm16, AudioFrame):
            if (pcm16.sample_rate != self.sample_rate or
                    pcm16.num_channels != self.num_channels or
                    len(pcm16.data) != pcm16.samples_per_channel * self.num_channels * 2):
                raise ValueError("audio frame does not match source format")
            pcm16 = pcm16.data
        if not isinstance(pcm16, bytes):
            raise TypeError("PCM data must be bytes")
        frame_bytes = 2 * self.num_channels
        if not pcm16 or len(pcm16) % frame_bytes:
            raise ValueError("PCM data must contain complete interleaved samples")
        if len(pcm16) // frame_bytes > 0xFFFFFFFF:
            raise ValueError("PCM frame is too large")

        def capture():
            with self._lock:
                self._assert_open()
                buffer = C.create_string_buffer(pcm16)
                self._native.check(self._native.lib.lk_audio_source_capture_frame(
                    self._handle, C.cast(buffer, C.c_void_p), len(pcm16) // frame_bytes,
                ))

        await asyncio.to_thread(capture)

    @property
    def queued_duration_ms(self) -> int:
        with self._lock:
            self._assert_open()
            duration = C.c_uint32()
            self._native.check(self._native.lib.lk_audio_source_queued_duration_ms(
                self._handle, C.byref(duration)
            ))
            return duration.value

    def clear_queue(self) -> None:
        with self._lock:
            self._assert_open()
            self._native.check(self._native.lib.lk_audio_source_clear_queue(self._handle))

    async def wait_for_playout(self, timeout_ms: int = 10000) -> None:
        def wait():
            with self._lock:
                self._assert_open()
                self._native.check(self._native.lib.lk_audio_source_wait_for_playout(
                    self._handle, timeout_ms
                ))
        await asyncio.to_thread(wait)


class VideoSource:
    """I420 video source, external or device-backed."""

    def __init__(self, *, is_screencast: bool = False):
        self._native = native()
        self._lock = threading.Lock()
        self._handle = C.c_void_p()
        options = VideoSourceOptionsRaw()
        self._native.lib.lk_video_source_options_init(C.byref(options))
        options.is_screencast = int(is_screencast)
        self._native.check(self._native.lib.lk_video_source_create(
            C.byref(options), C.byref(self._handle)
        ))
        self._kind = "external"

    @classmethod
    def from_camera(
        cls, device_id: str = "", *, width: int = 1280,
        height: int = 720, frames_per_second: int = 30,
    ) -> VideoSource:
        if (min(width, height, frames_per_second) <= 0 or
                max(width, height, frames_per_second) > 0xFFFFFFFF):
            raise ValueError("camera dimensions and frame rate must be positive")
        result = cls.__new__(cls)
        result._native = native()
        result._lock = threading.Lock()
        result._handle = C.c_void_p()
        options = CameraCaptureOptionsRaw()
        result._native.lib.lk_camera_capture_options_init(C.byref(options))
        options.device_id = utf8(device_id)
        options.width, options.height = width, height
        options.frames_per_second = frames_per_second
        result._native.check(result._native.lib.lk_video_source_create_camera(
            C.byref(options), C.byref(result._handle)
        ))
        result._kind = "camera"
        return result

    @classmethod
    def from_screen(
        cls, source_id: str = "", *, frames_per_second: int = 30,
        include_cursor: bool = True,
    ) -> VideoSource:
        if frames_per_second <= 0 or frames_per_second > 0xFFFFFFFF:
            raise ValueError("screen frame rate must be positive")
        result = cls.__new__(cls)
        result._native = native()
        result._lock = threading.Lock()
        result._handle = C.c_void_p()
        options = ScreenCaptureOptionsRaw()
        result._native.lib.lk_screen_capture_options_init(C.byref(options))
        options.source_id = utf8(source_id)
        options.frames_per_second = frames_per_second
        options.include_cursor = int(include_cursor)
        result._native.check(result._native.lib.lk_video_source_create_screen(
            C.byref(options), C.byref(result._handle)
        ))
        result._kind = "screen"
        return result

    def _capture_function(self, suffix: str):
        if self._kind == "external":
            raise LiveKitError("external video source has no capture device")
        return getattr(self._native.lib, f"lk_video_source_{self._kind}_{suffix}")

    async def start_capture(self) -> None:
        await asyncio.to_thread(self._capture_operation, "start")

    async def stop_capture(self) -> None:
        await asyncio.to_thread(self._capture_operation, "stop")

    def _capture_operation(self, suffix: str) -> None:
        with self._lock:
            self._assert_open()
            self._native.check(self._capture_function(suffix)(self._handle))

    @property
    def is_capturing(self) -> bool:
        with self._lock:
            self._assert_open()
            return bool(self._capture_function("is_capturing")(self._handle))

    @property
    def capture_id(self) -> str:
        with self._lock:
            self._assert_open()
            suffix = "device_id" if self._kind == "camera" else "source_id"
            self._capture_function(suffix)
            return self._native.string(f"lk_video_source_{self._kind}_{suffix}", self._handle)

    async def switch_capture(self, capture_id: str) -> None:
        encoded = utf8(capture_id)

        def switch():
            with self._lock:
                self._assert_open()
                suffix = "switch_device" if self._kind == "camera" else "switch_source"
                self._native.check(self._capture_function(suffix)(self._handle, encoded))
        await asyncio.to_thread(switch)

    def _assert_open(self):
        if not self._handle:
            raise LiveKitError("video source is closed")

    def close(self) -> None:
        with self._lock:
            if self._handle:
                self._native.check(self._native.lib.lk_video_source_destroy(self._handle))
                self._handle = C.c_void_p()

    async def capture_i420(
        self, data: bytes, width: int, height: int, timestamp_us: int = 0,
    ) -> None:
        if not isinstance(data, bytes):
            raise TypeError("I420 data must be bytes")
        if (width <= 0 or height <= 0 or width > 0xFFFFFFFF or
                height > 0xFFFFFFFF or width % 2 or height % 2):
            raise ValueError("I420 dimensions must be positive and even")
        if len(data) != width * height * 3 // 2:
            raise ValueError("I420 data size does not match dimensions")

        def capture():
            with self._lock:
                self._assert_open()
                buffer = C.create_string_buffer(data)
                self._native.check(self._native.lib.lk_video_source_capture_i420(
                    self._handle, C.cast(buffer, C.c_void_p), len(data),
                    width, height, timestamp_us,
                ))
        await asyncio.to_thread(capture)

    @property
    def dimensions(self) -> tuple[int, int]:
        with self._lock:
            self._assert_open()
            width, height = C.c_uint32(), C.c_uint32()
            self._native.check(self._native.lib.lk_video_source_dimensions(
                self._handle, C.byref(width), C.byref(height)
            ))
            return width.value, height.value


class LocalTrack:
    """A native track owned by a Room; its source must outlive the track."""

    def __init__(self, room, source: AudioSource | VideoSource, handle: C.c_void_p):
        self._room = room
        self._source = source
        self._handle = handle

    def _assert_open(self):
        if not self._handle:
            raise LiveKitError("local track is closed")

    async def publish(self, options: TrackPublishOptions | None = None) -> None:
        self._assert_open()
        raw = (options or TrackPublishOptions())._native(self._room._native.lib)
        await self._room._run(self._room._native.lib.lk_local_track_publish,
                              self._room._handle, self._handle, C.byref(raw))

    async def unpublish(self, *, stop_on_unpublish: bool = False) -> None:
        self._assert_open()
        await self._room._run(self._room._native.lib.lk_local_track_unpublish,
                              self._handle, int(stop_on_unpublish))

    async def set_muted(self, muted: bool) -> None:
        self._assert_open()
        await self._room._run(self._room._native.lib.lk_local_track_set_muted,
                              self._handle, int(muted))

    async def aclose(self) -> None:
        if not self._handle:
            return
        if self._room._closed:
            await self._room.aclose()
            return

        def destroy():
            self._room._native.check(
                self._room._native.lib.lk_local_track_destroy(self._handle)
            )
            self._handle = C.c_void_p()
            self._room._local_tracks.discard(self)

        await self._room._run_callable(destroy)


class RemoteMediaStream:
    """Bounded audio or video frame reader for one remote track."""

    def __init__(self, room, kind: str, handle: C.c_void_p):
        self._room = room
        self._native = room._native
        self._kind = kind
        self._handle = handle
        self._lock = threading.Lock()
        self._close_lock = threading.Lock()

    @property
    def is_closed(self) -> bool:
        with self._lock:
            return not self._handle or bool(getattr(
                self._native.lib, f"lk_{self._kind}_stream_is_closed"
            )(self._handle))

    @property
    def dropped_frames(self) -> int:
        with self._lock:
            return int(getattr(
                self._native.lib, f"lk_{self._kind}_stream_dropped_frames"
            )(self._handle)) if self._handle else 0

    async def read(self, timeout_ms: int = 0) -> AudioFrame | VideoFrame | None:
        if not 0 <= timeout_ms <= 0xFFFFFFFF:
            raise ValueError("timeout_ms is out of range")

        def receive():
            with self._lock:
                if not self._handle:
                    return None
                lib = self._native.lib
                kind = self._kind
                frame = C.c_void_p()
                status = getattr(lib, f"lk_{kind}_stream_read_for")(
                    self._handle, timeout_ms, C.byref(frame)
                )
                if status in (1, 2):
                    return None
                if status != 0:
                    raise LiveKitError("media stream read failed", status)
                try:
                    if kind == "audio":
                        raw = AudioFrameRaw()
                        self._native.check(lib.lk_owned_audio_frame_data(
                            frame, C.byref(raw)
                        ))
                        return AudioFrame.from_native(raw)
                    raw = VideoFrameRaw()
                    self._native.check(lib.lk_owned_video_frame_data(frame, C.byref(raw)))
                    metadata = VideoFrameMetadataRaw()
                    metadata.struct_size = C.sizeof(metadata)
                    self._native.check(lib.lk_owned_video_frame_metadata(
                        frame, C.byref(metadata)
                    ))
                    return VideoFrame.from_native(raw, metadata)
                finally:
                    getattr(lib, f"lk_owned_{kind}_frame_destroy")(frame)

        return await asyncio.to_thread(receive)

    def close(self) -> None:
        with self._close_lock:
            if not self._handle:
                return
            lib = self._native.lib
            getattr(lib, f"lk_{self._kind}_stream_close")(self._handle)
            with self._lock:
                getattr(lib, f"lk_{self._kind}_stream_destroy")(self._handle)
                self._handle = C.c_void_p()
                self._room._media_streams.discard(self)
