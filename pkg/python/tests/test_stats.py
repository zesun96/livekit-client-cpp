"""RTC statistics snapshot API without remote participants."""

import os
import unittest

from livekit.rtc import LiveKitError, Room, RtcStatsDirection


@unittest.skipUnless(os.environ.get("LIVEKIT_CLIENT_CPP_LIBRARY"), "native library is not set")
class RtcStatsTests(unittest.TestCase):
    def test_remote_track_lookup_and_direction_values(self):
        room = Room()
        try:
            with self.assertRaisesRegex(LiveKitError, "remote track was not found"):
                room.get_remote_track_rtc_stats("peer", "TR_track")
            self.assertEqual(int(RtcStatsDirection.SEND), 1)
            self.assertEqual(int(RtcStatsDirection.RECEIVE), 2)
        finally:
            import asyncio
            asyncio.run(room.aclose())
