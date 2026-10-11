"""Borrowed native room events are detached before asyncio dispatch."""

import asyncio
import ctypes as C
import os
import unittest

from livekit.rtc import DataChannelBufferStatus, Room, RoomSnapshot
from livekit.rtc._native import (
    BUFFER_STATUS_EVENT, ROOM_SNAPSHOT_EVENT, DataChannelBufferStatusRaw,
    RoomSnapshotRaw, TRANSCRIPTION_EVENT, METRICS_EVENT,
    TranscriptionSegmentRaw, TranscriptionReceivedRaw, MetricsReceivedRaw,
    MetricTimestampRaw,
)


@unittest.skipUnless(os.environ.get("LIVEKIT_CLIENT_CPP_LIBRARY"), "native library is not set")
class RoomCallbackTests(unittest.IsolatedAsyncioTestCase):
    async def test_room_and_buffer_events(self):
        async with Room() as room:
            room._loop = asyncio.get_running_loop()
            snapshots, buffers = [], []
            room.on("room_updated", snapshots.append)
            room.on("data_channel_buffer_status_changed", buffers.append)
            snapshot = RoomSnapshotRaw(C.sizeof(RoomSnapshotRaw), b"RM_one", b"room", b"{}", 1)
            status = DataChannelBufferStatusRaw(1, 10, 20, 5, 1)
            ROOM_SNAPSHOT_EVENT(room._callbacks.on_room_updated)(
                None, room._handle, C.byref(snapshot)
            )
            BUFFER_STATUS_EVENT(room._callbacks.on_data_channel_buffer_status_changed)(
                None, room._handle, C.byref(status)
            )
            await asyncio.sleep(0)
            self.assertEqual(snapshots[0], RoomSnapshot("RM_one", "room", "{}", True))
            self.assertEqual(buffers[0], DataChannelBufferStatus(True, 10, 20, 5, True))

    async def test_transcription_and_metrics_are_detached(self):
        async with Room() as room:
            room._loop = asyncio.get_running_loop()
            transcriptions, metrics = [], []
            room.on("transcription_received", transcriptions.append)
            room.on("metrics_received", metrics.append)
            segments = (TranscriptionSegmentRaw * 1)(TranscriptionSegmentRaw(
                b"s1", b"hello", b"en", 1, 2, 1, 3, 4
            ))
            transcription = TranscriptionReceivedRaw(b"peer", b"TR_audio", segments, 1)
            event = MetricsReceivedRaw(
                100, 1, MetricTimestampRaw(0, 100), None, 0,
                None, 0, None, 0, b"peer",
            )
            TRANSCRIPTION_EVENT(room._callbacks.on_transcription_received)(
                None, room._handle, C.byref(transcription)
            )
            METRICS_EVENT(room._callbacks.on_metrics_received)(
                None, room._handle, C.byref(event)
            )
            await asyncio.sleep(0)
            self.assertEqual(transcriptions[0].segments[0].text, "hello")
            self.assertEqual(metrics[0].normalized_timestamp.nanos, 100)
