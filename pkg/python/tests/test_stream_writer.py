"""Outgoing stream validation without a LiveKit server."""

import os
import asyncio
import ctypes as C
import unittest

from livekit.rtc import LiveKitError, Room, StreamCompletionStatus
from livekit.rtc._native import StreamCompletionRaw
from livekit.rtc.stream import StreamCallbackBridge


@unittest.skipUnless(os.environ.get("LIVEKIT_CLIENT_CPP_LIBRARY"), "native library is not set")
class StreamWriterTests(unittest.IsolatedAsyncioTestCase):
    async def test_progress_and_completion_copy_borrowed_data(self):
        async with Room() as room:
            room._loop = asyncio.get_running_loop()
            progress, completion = [], []
            room.on("stream_progress", progress.append)
            room.on("stream_complete", completion.append)
            bridge = StreamCallbackBridge(room)
            bridge.stream_id = "stream-1"
            bridge.progress(None, 3, 1, 5)
            raw = StreamCompletionRaw(
                0, b"stream-1", 5, 1, 5, b"", 0, 0
            )
            bridge.completion(None, C.byref(raw))
            await asyncio.sleep(0)
            self.assertEqual(progress[0].bytes_sent, 3)
            self.assertEqual(progress[0].total_size, 5)
            self.assertEqual(completion[0].status, StreamCompletionStatus.COMPLETED)
            self.assertEqual(completion[0].stream_id, "stream-1")

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
