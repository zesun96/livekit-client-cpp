"""External media sources and room-owned local tracks."""

from __future__ import annotations

import asyncio
import ctypes as C
import threading
from dataclasses import dataclass
from enum import IntEnum

from ._native import (
    AudioSourceOptionsRaw, LiveKitError, TrackPublishOptionsRaw,
    VideoSourceOptionsRaw, native, utf8,
)


class TrackSource(IntEnum):
    UNKNOWN = 0
    CAMERA = 1
    MICROPHONE = 2
    SCREEN_SHARE = 3
    SCREEN_SHARE_AUDIO = 4


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
    """Caller-supplied signed 16-bit interleaved PCM. No audio device is opened."""

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

    def _assert_open(self):
        if not self._handle:
            raise LiveKitError("audio source is closed")

    def close(self) -> None:
        with self._lock:
            if self._handle:
                self._native.check(self._native.lib.lk_audio_source_destroy(self._handle))
                self._handle = C.c_void_p()

    async def capture_frame(self, pcm16: bytes) -> None:
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
    """Caller-supplied I420 video frames. No camera or screen device is opened."""

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
