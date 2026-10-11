"""Recorder ABI and input checks without opening a media device or file."""

import os
import unittest

from livekit.rtc import LiveKitError, Room


@unittest.skipUnless(os.environ.get("LIVEKIT_CLIENT_CPP_LIBRARY"), "native library is not set")
class TrackRecordingTests(unittest.IsolatedAsyncioTestCase):
    async def test_disconnected_recorder_and_options(self):
        async with Room() as room:
            with self.assertRaises(ValueError):
                await room.start_track_recording("", "TR_track", "capture.ivf")
            with self.assertRaises(ValueError):
                await room.start_track_recording("peer", "TR_track", "capture.ivf",
                                                 queue_capacity=0)
            with self.assertRaises(LiveKitError):
                await room.start_track_recording("peer", "TR_track", "capture.ivf")
