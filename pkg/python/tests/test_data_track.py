"""Offline DataTrack ABI and borrowed-event checks."""

import asyncio
import ctypes as C
import os
import unittest

from livekit.rtc import (
    DataTrackError, DataTrackFrame, DataTrackFrameEncoding, DataTrackPublishOptions,
    DataTrackSchema, DataTrackSchemaEncoding, DataTrackSchemaId,
    DataTrackSubscriptionOptions, Room,
)
from livekit.rtc._native import DataTrackFrameViewRaw, DataTrackInfoRaw, ParticipantInfo


@unittest.skipUnless(os.environ.get("LIVEKIT_CLIENT_CPP_LIBRARY"), "native library is not set")
class DataTrackTests(unittest.IsolatedAsyncioTestCase):
    async def test_schema_and_disconnected_publish(self):
        async with Room() as room:
            self.assertEqual(room.remote_data_tracks, {})
            schema = DataTrackSchema(
                DataTrackSchemaId("telemetry", DataTrackSchemaEncoding.JSON_SCHEMA),
                b'{"type":"object"}',
            )
            raw = schema.id._raw(room._native.lib)
            self.assertEqual(raw.name, b"telemetry")
            self.assertEqual(raw.encoding, DataTrackSchemaEncoding.JSON_SCHEMA)
            with self.assertRaises(DataTrackError) as context:
                await room.store_data_track_schema(schema)
            self.assertEqual(context.exception.code, 6)
            with self.assertRaises(DataTrackError) as context:
                await room.publish_data_track(DataTrackPublishOptions(
                    "telemetry", frame_encoding=DataTrackFrameEncoding.JSON,
                    schema=schema.id,
                ))
            self.assertEqual(context.exception.code, 6)
            with self.assertRaises(DataTrackError):
                await room.subscribe_data_track(
                    "peer", "DT_track", DataTrackSubscriptionOptions()
                )

    async def test_frame_callback_copies_borrowed_payload(self):
        async with Room() as room:
            room._loop = asyncio.get_running_loop()
            received = []
            room.on("data_track_frame", lambda track, person, frame:
                    received.append((track, person, frame)))
            payload = C.create_string_buffer(b"a\x00b")
            track = DataTrackInfoRaw(
                1, b"DT_one", b"telemetry", 0, 1, 7, b"", 0, b"", 0, b"",
            )
            person = ParticipantInfo(b"PA_one", b"peer", b"Peer", b"", 0, 0, 0, 0)
            frame = DataTrackFrameViewRaw(C.cast(payload, C.c_void_p), 3, 1, 123)
            room._on_data_track_frame(None, None, C.pointer(track),
                                      C.pointer(person), C.pointer(frame))
            payload[0] = b"x"
            await asyncio.sleep(0)
            self.assertEqual(received[0][0].sid, "DT_one")
            self.assertEqual(received[0][1].identity, "peer")
            self.assertEqual(received[0][2], DataTrackFrame(b"a\x00b", 123))

    async def test_subscription_capacity_validation(self):
        async with Room() as room:
            with self.assertRaises(ValueError):
                await room.subscribe_data_track(
                    "peer", "DT_one", DataTrackSubscriptionOptions(buffer_capacity=0)
                )
