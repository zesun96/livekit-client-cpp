"""RPC handler bridge for the C ABI output-parameter callback."""

from __future__ import annotations

import asyncio
import inspect
import threading
from concurrent.futures import TimeoutError
from typing import Callable

from ._models import RpcError, RpcInvocationData
from ._native import RPC_HANDLER_OUT, decode, utf8


class RpcHandlerBridge:
    def __init__(self, handler: Callable, loop: asyncio.AbstractEventLoop):
        if not callable(handler):
            raise TypeError("RPC handler must be callable")
        self._handler = handler
        self._loop = loop
        self._responses: dict[int, tuple[bytes, bytes, bytes]] = {}
        self.callback = RPC_HANDLER_OUT(self._invoke)

    async def _call(self, request: RpcInvocationData):
        response = self._handler(request)
        if inspect.isawaitable(response):
            response = await response
        return response

    def _invoke(self, _user, invocation, output) -> None:
        if not invocation or not output:
            return
        request = invocation.contents
        detached = RpcInvocationData(
            request_id=decode(request.request_id),
            caller_identity=decode(request.caller_identity),
            payload=decode(request.payload),
            response_timeout_ms=request.response_timeout_ms,
        )
        try:
            if self._loop.is_closed():
                raise RuntimeError("room event loop is closed")
            call = self._call(detached)
            try:
                future = asyncio.run_coroutine_threadsafe(call, self._loop)
            except RuntimeError:
                call.close()
                raise
            try:
                timeout = request.response_timeout_ms / 1000 if request.response_timeout_ms else 15
                result = future.result(timeout=timeout)
            except TimeoutError:
                future.cancel()
                raise RpcError("RPC handler timed out", 1502) from None
            if not isinstance(result, str):
                raise TypeError("RPC handler must return a string")
            payload = utf8(result)
            self._responses[threading.get_ident()] = (payload, b"", b"")
            output.contents.payload = payload
        except BaseException as error:
            if isinstance(error, RpcError):
                code, message, data = error.code, str(error), error.data
            else:
                code, message, data = 1500, str(error), ""
            message_bytes, data_bytes = utf8(message), utf8(data)
            self._responses[threading.get_ident()] = (b"", message_bytes, data_bytes)
            output.contents.error_code = code
            output.contents.error_message = message_bytes
            output.contents.error_data = data_bytes
