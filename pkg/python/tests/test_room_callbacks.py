"""Borrowed native room events are detached before asyncio dispatch."""

import asyncio
import ctypes as C
import os
import unittest

from livekit.rtc import DataChannelBufferStatus, Room, RoomSnapshot
from livekit.rtc._native import (
    BUFFER_STATUS_EVENT, ROOM_SNAPSHOT_EVENT, DataChannelBufferStatusRaw,
    RoomSnapshotRaw,
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
