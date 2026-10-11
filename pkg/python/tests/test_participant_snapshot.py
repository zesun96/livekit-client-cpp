"""Detached participant snapshots and permission values."""

import asyncio
import os
import unittest

from livekit.rtc import Participant, ParticipantPermissions, Room


@unittest.skipUnless(os.environ.get("LIVEKIT_CLIENT_CPP_LIBRARY"), "native library is not set")
class ParticipantSnapshotTests(unittest.TestCase):
    def test_local_snapshot_and_empty_remote_list(self):
        room = Room()
        try:
            participant = room.local_participant_snapshot
            self.assertIsInstance(participant, Participant)
            self.assertTrue(participant.is_local)
            self.assertIsInstance(participant.permissions, ParticipantPermissions)
            self.assertEqual(room.local_participant.snapshot, participant)
            self.assertEqual(room.remote_participants, {})
        finally:
            asyncio.run(room.aclose())
