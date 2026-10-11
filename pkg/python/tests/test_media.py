"""External media and local-track lifecycle without physical devices or server."""

import os
import unittest

from livekit.rtc import AudioSource, LiveKitError, Room, VideoSource


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
        finally:
            source.close()
            video.close()
