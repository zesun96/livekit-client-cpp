"""Offline RPC handler bridge and registration checks."""

import asyncio
import ctypes as C
import gc
import os
import unittest

from livekit.rtc import LiveKitError, Room, RpcError, RpcInvocationData
from livekit.rtc._native import RpcHandlerResultRaw, RpcInvocationRaw
from livekit.rtc._rpc import RpcHandlerBridge


@unittest.skipUnless(os.environ.get("LIVEKIT_CLIENT_CPP_LIBRARY"), "native library is not set")
class RpcHandlerTests(unittest.IsolatedAsyncioTestCase):
    async def test_async_handler_copies_request_and_response(self):
        observed = []

        async def echo(request):
            observed.append(request)
            return request.payload + "!"

        bridge = RpcHandlerBridge(echo, asyncio.get_running_loop())
        payload = C.create_string_buffer(b"hello")
        invocation = RpcInvocationRaw(
            b"req", b"alice", C.cast(payload, C.c_char_p), 1000,
        )
        result = RpcHandlerResultRaw()
        await asyncio.to_thread(
            bridge.callback, None, C.byref(invocation), C.byref(result)
        )
        payload[0] = b"x"
        gc.collect()
        self.assertIsInstance(observed[0], RpcInvocationData)
        self.assertEqual(observed[0].payload, "hello")
        self.assertEqual(result.payload, b"hello!")
        self.assertEqual(result.error_code, 0)

    async def test_handler_errors_map_to_rpc_result(self):
        def fail(_request):
            raise RpcError("denied", 1500, "policy")

        bridge = RpcHandlerBridge(fail, asyncio.get_running_loop())
        invocation = RpcInvocationRaw(b"req", b"alice", b"hello", 1000)
        result = RpcHandlerResultRaw()
        await asyncio.to_thread(
            bridge.callback, None, C.byref(invocation), C.byref(result)
        )
        self.assertEqual(result.error_code, 1500)
        self.assertEqual(result.error_message, b"denied")
        self.assertEqual(result.error_data, b"policy")

    async def test_native_registration_lifecycle(self):
        async with Room() as room:
            room.local_participant.register_rpc_method("echo", lambda request: request.payload)
            with self.assertRaises(LiveKitError):
                room.register_rpc_method("echo", lambda request: "other")
            room.local_participant.unregister_rpc_method("echo")
            room.register_rpc_method("echo", lambda request: "again")
