"""Offline ABI smoke tests; set LIVEKIT_CLIENT_CPP_LIBRARY to a shared build."""

import asyncio
import ctypes as C
import os
import unittest

from livekit.rtc import ConnectionState, LiveKitError, Room
from livekit.rtc._native import (
    ChatMessage, DataReceived, ParticipantInfo, RoomCallbacks,
    TrackPublicationInfo, TRACK_EVENT, _CALLBACK_NAMES,
)


class RoomTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        if not os.environ.get("LIVEKIT_CLIENT_CPP_LIBRARY"):
            self.skipTest("LIVEKIT_CLIENT_CPP_LIBRARY is not set")
        self.room = Room()

    async def asyncTearDown(self):
        await self.room.aclose()

    async def test_room_lifecycle_and_error(self):
        self.assertEqual(self.room.connection_state, ConnectionState.DISCONNECTED)
        self.assertFalse(self.room.is_connected)
        self.assertEqual(self.room.remote_participants, {})
        with self.assertRaisesRegex(LiveKitError, "URL, and token"):
            await self.room.connect("", "")

    async def test_binary_data_callback_copies_borrowed_buffer(self):
        self.room._loop = asyncio.get_running_loop()
        received = []
        self.room.on("data_received", received.append)
        payload = C.create_string_buffer(b"a\x00b")
        event = DataReceived(
            C.cast(payload, C.c_void_p), 3, b"topic", b"peer", 1
        )
        self.room._on_data(None, None, C.pointer(event))
        payload[0] = b"z"
        await asyncio.sleep(0)
        self.assertEqual(received[0].data, b"a\x00b")
        self.assertEqual(received[0].participant_identity, "peer")

    async def test_close_is_idempotent(self):
        await self.room.aclose()
        await self.room.aclose()
        with self.assertRaisesRegex(LiveKitError, "closed"):
            await self.room.disconnect()

    async def test_chat_callback_copies_borrowed_strings(self):
        self.room._loop = asyncio.get_running_loop()
        received = []
        self.room.on("chat_message_received", received.append)
        message = C.create_string_buffer(b"hello")
        event = ChatMessage(
            b"id", 123, 0, 0, C.cast(message, C.c_char_p), 0, 0, b"peer"
        )
        self.room._on_chat(None, None, C.pointer(event))
        message[0] = b"x"
        await asyncio.sleep(0)
        self.assertEqual(received[0].message, "hello")

    async def test_send_data_requires_connection(self):
        with self.assertRaises(LiveKitError):
            await self.room.publish_data(b"a\x00b", topic="test")

    async def test_track_callback_copies_borrowed_values(self):
        self.room._loop = asyncio.get_running_loop()
        received = []
        self.room.on("track_published", lambda track, person: received.append((track, person)))
        name = C.create_string_buffer(b"camera")
        track = TrackPublicationInfo(
            b"TR_1", C.cast(name, C.c_char_p), b"video/VP8",
            1, 1, 640, 480, 0, 0, 1,
        )
        participant = ParticipantInfo(b"PA_1", b"peer", b"Peer", b"", 0, 0, 0, 0)
        callback = TRACK_EVENT(self.room._callbacks.on_track_published)
        callback(None, self.room._handle, C.pointer(track), C.pointer(participant))
        name[0] = b"x"
        await asyncio.sleep(0)
        self.assertEqual(received[0][0].name, "camera")
        self.assertEqual(received[0][1].identity, "peer")


class LayoutTests(unittest.TestCase):
    def test_callback_table_layout(self):
        pointer_size = C.sizeof(C.c_void_p)
        self.assertEqual(C.sizeof(RoomCallbacks), (2 + len(_CALLBACK_NAMES)) * pointer_size)
        self.assertEqual(len(_CALLBACK_NAMES), 54)


if __name__ == "__main__":
    unittest.main()
