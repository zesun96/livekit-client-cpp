"""External media and remote-frame callbacks without physical devices or server."""

import asyncio
import ctypes as C
import os
import unittest

from livekit.rtc import (
    AudioFrame, AudioSource, LiveKitError, Room, VideoBufferType,
    VideoPlane, VideoRotation, VideoSource,
)
from livekit.rtc._native import (
    AudioFrameRaw, ParticipantInfo, TrackPublicationInfo, VideoFrameMetadataRaw,
    VideoFrameRaw,
)


@unittest.skipUnless(os.environ.get("LIVEKIT_CLIENT_CPP_LIBRARY"), "native library is not set")
class ExternalMediaTests(unittest.IsolatedAsyncioTestCase):
    async def test_audio_source_capture_and_disconnected_track_error(self):
        source = AudioSource(sample_rate=48000, num_channels=1, queue_size_ms=100)
        try:
            await source.capture_frame(bytes(480 * 2))
            self.assertGreaterEqual(source.queued_duration_ms, 0)
            source.clear_queue()
            async with Room() as room:
                with self.assertRaisesRegex(LiveKitError, "failed to create audio track"):
                    await room.local_participant.create_audio_track("tone", source)
            source.close()
        finally:
            if source._handle:
                source.close()

    async def test_video_source_frame_and_disconnected_track_error(self):
        source = VideoSource()
        try:
            await source.capture_i420(bytes(2 * 2 * 3 // 2), 2, 2)
            await source.capture_frame(
                bytes(2 * 2 * 4), 2, 2, format=VideoBufferType.RGBA,
                rotation=VideoRotation.R90,
                planes=[VideoPlane(0, 16, 8)], user_timestamp_us=12,
                frame_id=3, user_data=b"metadata",
            )
            self.assertEqual(source.dimensions, (2, 2))
            async with Room() as room:
                with self.assertRaisesRegex(LiveKitError, "failed to create video track"):
                    await room.local_participant.create_video_track("black", source)
            source.close()
        finally:
            if source._handle:
                source.close()

    async def test_media_input_validation(self):
        source = AudioSource()
        video = VideoSource()
        try:
            with self.assertRaises(ValueError):
                await source.capture_frame(b"x")
            with self.assertRaises(ValueError):
                await video.capture_i420(b"bad", 2, 2)
            await source.capture_frame(AudioFrame(bytes(960), 48000, 1, 480))
        finally:
            source.close()
            video.close()

    async def test_device_capture_options_validate_without_opening_devices(self):
        with self.assertRaises(ValueError):
            AudioSource.from_microphone(queue_size_ms=11)
        with self.assertRaises(ValueError):
            AudioSource.from_system_audio(queue_size_ms=0)
        with self.assertRaises(ValueError):
            VideoSource.from_camera(width=0)
        with self.assertRaises(ValueError):
            VideoSource.from_screen(frames_per_second=0)

    async def test_disconnected_stream_and_borrowed_frame_callbacks(self):
        async with Room() as room:
            with self.assertRaises(LiveKitError):
                await room.create_audio_stream("peer", "TR_audio")
            with self.assertRaises(ValueError):
                await room.create_video_stream("peer", "TR_video", capacity=0)
            room._loop = asyncio.get_running_loop()
            audio, video = [], []
            room.on("audio_frame", lambda track, person, frame: audio.append(frame))
            room.on("video_frame_with_metadata",
                    lambda track, person, frame: video.append(frame))
            track = TrackPublicationInfo(
                b"TR_1", b"camera", b"video/VP8", 1, 1, 2, 2, 0, 0, 1,
            )
            participant = ParticipantInfo(b"PA_1", b"peer", b"Peer", b"", 0, 0, 0, 0)
            pcm = C.create_string_buffer(b"\x01\x00\x02\x00")
            audio_raw = AudioFrameRaw(C.cast(pcm, C.c_void_p), 2, 48000, 1, 2)
            room._on_audio_frame(None, None, C.pointer(track),
                                 C.pointer(participant), C.pointer(audio_raw))
            pixels = C.create_string_buffer(b"abcdef")
            extra = C.create_string_buffer(b"id")
            video_raw = VideoFrameRaw(C.cast(pixels, C.c_void_p), 6, 2, 2, 123)
            metadata = VideoFrameMetadataRaw(
                C.sizeof(VideoFrameMetadataRaw), 1, 456, 1, 7,
                1, C.cast(extra, C.c_void_p), 2,
            )
            room._on_video_frame_with_metadata(
                None, None, C.pointer(track), C.pointer(participant),
                C.pointer(video_raw), C.pointer(metadata),
            )
            pcm[0], pixels[0], extra[0] = b"x", b"x", b"x"
            await asyncio.sleep(0)
            self.assertEqual(audio[0].data, b"\x01\x00\x02\x00")
            self.assertEqual(video[0].data, b"abcdef")
            self.assertEqual(video[0].user_data, b"id")
            self.assertEqual(video[0].user_timestamp_us, 456)
