"""Offline checks for borrowed connection options and native callbacks."""

import ctypes as C
import gc
import os
import unittest

from livekit.rtc import (
    IceServer, IceTransportType, ParticipantTrackPermission, ReconnectReason,
    RoomOptions, RtcConfiguration,
)
from livekit.rtc._connection import connection_options, subscription_permissions
from livekit.rtc._native import ReconnectContextRaw, native


@unittest.skipUnless(os.environ.get("LIVEKIT_CLIENT_CPP_LIBRARY"), "native library is not set")
class ConnectionOptionsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = native().lib

    def test_ice_server_borrowed_values_survive_collection(self):
        raw, callback = connection_options(self.lib, RoomOptions(
            rtc_config=RtcConfiguration(
                ice_servers=[IceServer(["turn:example.test:3478"], "alice", "secret")],
                ice_transport_type=IceTransportType.RELAY,
            ),
        ))
        gc.collect()
        self.assertIsNone(callback)
        self.assertEqual(raw.ice_server_count, 1)
        self.assertEqual(raw.ice_servers[0].urls[0], b"turn:example.test:3478")
        self.assertEqual(raw.ice_servers[0].username, b"alice")
        self.assertEqual(raw.ice_servers[0].password, b"secret")
        self.assertEqual(raw.ice_transport_type, IceTransportType.RELAY)

    def test_reconnect_policy_uses_detached_context_and_delay(self):
        seen = []
        raw, callback = connection_options(self.lib, RoomOptions(
            reconnect_policy=lambda context: seen.append(context) or 250,
        ))
        self.assertTrue(raw.reconnect_policy)
        url = C.create_string_buffer(b"wss://example.test")
        context = ReconnectContextRaw(
            C.sizeof(ReconnectContextRaw), 3, 1200,
            ReconnectReason.MEDIA_FAILURE, C.cast(url, C.c_char_p),
        )
        delay = C.c_uint32()
        self.assertEqual(callback(None, C.byref(context), C.byref(delay)), 1)
        url[0] = b"x"
        self.assertEqual(delay.value, 250)
        self.assertEqual(seen[0].server_url, "wss://example.test")
        self.assertEqual(seen[0].retry_count, 3)
        self.assertEqual(seen[0].reason, ReconnectReason.MEDIA_FAILURE)

    def test_reconnect_policy_stops_on_none_or_exception(self):
        context = ReconnectContextRaw(C.sizeof(ReconnectContextRaw), 0, 0, 0, b"")
        for policy in (lambda _: None, lambda _: 2**32, self._raise):
            _, callback = connection_options(self.lib, RoomOptions(reconnect_policy=policy))
            delay = C.c_uint32(123)
            self.assertEqual(callback(None, C.byref(context), C.byref(delay)), 0)
            self.assertEqual(delay.value, 123)

    @staticmethod
    def _raise(_):
        raise RuntimeError("failed")

    def test_subscription_permissions_keep_track_ids_alive(self):
        raw = subscription_permissions(self.lib, [
            ParticipantTrackPermission(
                participant_identity="peer", allowed_track_sids=["TR_one", "TR_two"]
            ),
        ])
        gc.collect()
        self.assertEqual(raw[0].participant_identity, b"peer")
        self.assertEqual(raw[0].allowed_track_sid_count, 2)
        self.assertEqual(raw[0].allowed_track_sids[1], b"TR_two")
        with self.assertRaisesRegex(ValueError, "SID or identity"):
            subscription_permissions(self.lib, [ParticipantTrackPermission()])
