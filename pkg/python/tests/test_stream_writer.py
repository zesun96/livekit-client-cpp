"""Outgoing stream validation without a LiveKit server."""

import os
import unittest

from livekit.rtc import LiveKitError, Room


@unittest.skipUnless(os.environ.get("LIVEKIT_CLIENT_CPP_LIBRARY"), "native library is not set")
class StreamWriterTests(unittest.IsolatedAsyncioTestCase):
    async def test_option_validation_and_disconnected_room(self):
        async with Room() as room:
            with self.assertRaises(ValueError):
                await room.stream_text(total_size=-1)
            with self.assertRaises(ValueError):
                await room.stream_bytes(chunk_size=0)
            with self.assertRaises(ValueError):
                await room.stream_text(version=0x80000000)
            with self.assertRaises(LiveKitError):
                await room.stream_text(topic="chat")
            with self.assertRaises(LiveKitError):
                await room.stream_bytes(topic="files")
