"""Offline ABI smoke tests; set LIVEKIT_CLIENT_CPP_LIBRARY to a shared build."""

import asyncio
import ctypes as C
import os
import unittest

from livekit.rtc import (
    ConnectionState, E2EEOptions, EncryptionState, FrameCryptorDirection, KeyDerivation,
    LiveKitError, Room, RoomOptions, StreamEventType,
)
from livekit.rtc._native import (
    Attribute, ByteStreamEvent, ChatMessage, DataReceived, FileReceived, ParticipantInfo,
    RoomCallbacks, RoomConnectOptions, TextReceived, TextStreamEvent, TrackPublicationInfo,
    EncryptionStateRaw, ENCRYPTION_EVENT, TRACK_EVENT, _CALLBACK_NAMES,
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
        await asyncio.gather(self.room.aclose(), self.room.aclose())
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

    async def test_completed_stream_events_copy_content_and_metadata(self):
        self.room._loop = asyncio.get_running_loop()
        texts, files = [], []
        self.room.on("text_received", texts.append)
        self.room.on("file_received", files.append)
        attributes = (Attribute * 1)(Attribute(b"key", b"value"))
        attachments = (C.c_char_p * 1)(b"stream-2")
        message = C.create_string_buffer(b"hello")
        text = TextReceived(
            b"stream-1", C.cast(message, C.c_char_p), b"topic", b"peer",
            b"reply", 123, attachments, 1, attributes, 1,
        )
        self.room._on_text(None, None, C.pointer(text))
        payload = C.create_string_buffer(b"a\x00b")
        received = FileReceived(
            C.cast(payload, C.c_void_p), 3, b"stream-3", b"file.bin",
            b"application/octet-stream", b"topic", b"peer", attributes, 1, 456,
        )
        self.room._on_bytes("file_received", C.pointer(received))
        message[0], payload[0] = b"x", b"x"
        await asyncio.sleep(0)
        self.assertEqual(texts[0].text, "hello")
        self.assertEqual(texts[0].attached_stream_ids, ("stream-2",))
        self.assertEqual(texts[0].attributes, {"key": "value"})
        self.assertEqual(files[0].data, b"a\x00b")

    async def test_incremental_text_handler_and_queue_limit(self):
        self.room._loop = asyncio.get_running_loop()
        received = []
        self.room.register_text_stream_handler("topic", received.append)
        content = C.create_string_buffer(b"hi")
        event = TextStreamEvent(
            1, b"stream-1", b"text/plain", b"topic", b"peer",
            C.cast(content, C.c_void_p), 2, 0, 1, 10, b"",
            None, 0, 123, b"", None, 0,
        )
        self.room._on_text_stream(None, None, C.pointer(event))
        content[0] = b"x"
        await asyncio.sleep(0)
        self.assertEqual(received[0].content, "hi")
        self.assertEqual(received[0].type, StreamEventType.CHUNK)
        self.room.unregister_text_stream_handler("topic")
        for _ in range(1025):
            self.room._post("no_listener")
        self.assertEqual(self.room.dropped_events, 1)

    async def test_incremental_byte_handler_copies_binary_chunk(self):
        self.room._loop = asyncio.get_running_loop()
        received = []
        self.room.register_byte_stream_handler("assets", received.append)
        content = C.create_string_buffer(b"a\x00b")
        event = ByteStreamEvent(
            1, b"stream-1", b"asset.bin", b"application/octet-stream",
            b"assets", b"peer", C.cast(content, C.c_void_p), 3,
            0, 0, 0, b"", None, 0, 123,
        )
        self.room._on_byte_stream(None, None, C.pointer(event))
        content[0] = b"x"
        await asyncio.sleep(0)
        self.assertEqual(received[0].content, b"a\x00b")
        self.assertIsNone(received[0].total_size)
        self.room.unregister_byte_stream_handler("assets")

    async def test_text_stream_decodes_across_chunk_boundaries(self):
        self.room._loop = asyncio.get_running_loop()
        received = []
        self.room.on("text_stream_event", received.append)
        for index, chunk in enumerate((b"\xe4", b"\xb8\xad")):
            content = C.create_string_buffer(chunk)
            event = TextStreamEvent(
                1, b"stream-1", b"text/plain", b"topic", b"peer",
                C.cast(content, C.c_void_p), len(chunk), index, 0, 0, b"",
                None, 0, 123, b"", None, 0,
            )
            self.room._on_text_stream(None, None, C.pointer(event))
        await asyncio.sleep(0)
        self.assertEqual([event.content for event in received], ["", "中"])

    async def test_e2ee_options_and_unconfigured_key_api(self):
        options = E2EEOptions(
            shared_key=b"secret\x00key", ratchet_salt=b"salt",
            key_derivation=KeyDerivation.HKDF_SHA256,
        )
        raw = RoomConnectOptions()
        raw.e2ee_options = C.pointer(options._native(self.room._native.lib))
        self.assertEqual(
            raw.e2ee_options.contents.struct_size,
            C.sizeof(type(raw.e2ee_options.contents)),
        )
        self.assertEqual(C.string_at(
            raw.e2ee_options.contents.shared_key,
            raw.e2ee_options.contents.shared_key_size,
        ), b"secret\x00key")
        self.assertEqual(raw.e2ee_options.contents.key_derivation, 1)
        self.assertFalse(self.room.e2ee.is_configured)
        with self.assertRaisesRegex(LiveKitError, "not configured"):
            await self.room.e2ee.set_shared_key(b"key")
        with self.assertRaisesRegex(LiveKitError, "not configured"):
            await self.room.e2ee.export_shared_key()
        with self.assertRaisesRegex(LiveKitError, "not configured"):
            await self.room.e2ee.frame_cryptors()
        with self.assertRaisesRegex(LiveKitError, "not configured"):
            await self.room.e2ee.set_frame_cryptor_enabled(
                "TR_1", FrameCryptorDirection.SENDER, True
            )
        with self.assertRaises(ValueError):
            await self.room.e2ee.set_shared_key(b"key", key_index=-1)
        with self.assertRaisesRegex(LiveKitError, "URL, and token"):
            await self.room.connect("", "", RoomOptions(encryption=options))

    async def test_encryption_event_copies_borrowed_strings(self):
        self.room._loop = asyncio.get_running_loop()
        received = []
        self.room.on("encryption_state_changed", received.append)
        track_id = C.create_string_buffer(b"TR_1")
        raw = EncryptionStateRaw(
            C.cast(track_id, C.c_char_p), b"peer", 1, 0, 1, 2, 1
        )
        callback = ENCRYPTION_EVENT(self.room._callbacks.on_encryption_state_changed)
        callback(None, self.room._handle, C.pointer(raw))
        track_id[0] = b"x"
        await asyncio.sleep(0)
        self.assertIsInstance(received[0], EncryptionState)
        self.assertEqual(received[0].track_id, "TR_1")


class LayoutTests(unittest.TestCase):
    def test_callback_table_layout(self):
        pointer_size = C.sizeof(C.c_void_p)
        self.assertEqual(C.sizeof(RoomCallbacks), (2 + len(_CALLBACK_NAMES)) * pointer_size)
        self.assertEqual(len(_CALLBACK_NAMES), 54)


if __name__ == "__main__":
    unittest.main()
