"""Offline token-source tests that do not connect to a server."""

import asyncio
import ctypes as C
import gc
import os
import threading
import unittest

from livekit.rtc import (
    LiveKitError, Room, TokenSourceFetchOptions, TokenSourceResponse,
)
from livekit.rtc._native import (
    Attribute, TokenSourceFetchOptionsRaw, TokenSourceResponseRaw, native,
)
from livekit.rtc._token_source import TokenSourceBridge, fetch_options_raw


@unittest.skipUnless(os.environ.get("LIVEKIT_CLIENT_CPP_LIBRARY"), "native library is not set")
class TokenSourceTests(unittest.IsolatedAsyncioTestCase):
    async def test_fetch_options_keep_attributes_alive(self):
        raw = fetch_options_raw(native().lib, TokenSourceFetchOptions(
            room_name="studio", participant_attributes={"role": "viewer"},
        ))
        gc.collect()
        self.assertEqual(raw.room_name, b"studio")
        self.assertEqual(raw.participant_attribute_count, 1)
        self.assertEqual(raw.participant_attributes[0].key, b"role")
        self.assertEqual(raw.participant_attributes[0].value, b"viewer")

    async def test_async_provider_receives_detached_refresh_request(self):
        loop = asyncio.get_running_loop()
        observed = []

        async def provide(request):
            observed.append((threading.get_ident(), request))
            return TokenSourceResponse("wss://example.test", "test-token")

        bridge = TokenSourceBridge(provide, loop)
        name = C.create_string_buffer(b"studio")
        attributes = (Attribute * 1)(Attribute(b"role", b"viewer"))
        request = TokenSourceFetchOptionsRaw(
            C.sizeof(TokenSourceFetchOptionsRaw), C.cast(name, C.c_char_p),
            b"Alice", b"alice", b"", attributes, 1, b"", b"", b"",
        )
        response = TokenSourceResponseRaw()
        native().lib.lk_token_source_response_init(C.byref(response))
        status = await asyncio.to_thread(
            bridge.callback, None, C.byref(request), 1, C.byref(response)
        )
        name[0] = b"x"
        gc.collect()
        self.assertEqual(status, 0)
        self.assertEqual(observed[0][0], threading.get_ident())
        self.assertEqual(observed[0][1].room_name, "studio")
        self.assertEqual(observed[0][1].participant_attributes, {"role": "viewer"})
        self.assertTrue(observed[0][1].force_refresh)
        self.assertEqual(response.server_url, b"wss://example.test")
        self.assertEqual(response.participant_token, b"test-token")

    async def test_provider_error_is_reported_without_network(self):
        async with Room() as room:
            def fail(_request):
                raise ValueError("missing token")

            with self.assertRaisesRegex(LiveKitError, "token source failed: missing token"):
                await room.connect_with_token_source(fail)

    async def test_provider_rejects_invalid_result(self):
        bridge = TokenSourceBridge(lambda _: ("", ""), asyncio.get_running_loop())
        request = TokenSourceFetchOptionsRaw()
        native().lib.lk_token_source_fetch_options_init(C.byref(request))
        response = TokenSourceResponseRaw()
        status = await asyncio.to_thread(
            bridge.callback, None, C.byref(request), 0, C.byref(response)
        )
        self.assertEqual(status, 4)
        self.assertIsInstance(bridge.last_error, ValueError)
