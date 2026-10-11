"""Bridge native token refresh requests onto a Room's asyncio loop."""

from __future__ import annotations

import asyncio
import ctypes as C
import inspect
import threading
from concurrent.futures import TimeoutError
from typing import Callable

from ._models import TokenSourceFetchOptions, TokenSourceResponse, copy_attributes
from ._native import (
    Attribute, TOKEN_SOURCE_CALLBACK, TokenSourceFetchOptionsRaw, decode, utf8,
)


def fetch_options_raw(lib, options: TokenSourceFetchOptions) -> TokenSourceFetchOptionsRaw:
    raw = TokenSourceFetchOptionsRaw()
    lib.lk_token_source_fetch_options_init(C.byref(raw))
    for name in (
        "room_name", "participant_name", "participant_identity", "participant_metadata",
        "agent_name", "agent_metadata", "deployment",
    ):
        setattr(raw, name, utf8(getattr(options, name)))
    attributes = (Attribute * len(options.participant_attributes))(
        *(Attribute(utf8(key), utf8(value))
          for key, value in options.participant_attributes.items())
    )
    raw.participant_attributes = attributes
    raw.participant_attribute_count = len(attributes)
    raw._keepalive = attributes
    return raw


class TokenSourceBridge:
    def __init__(
        self, provider: Callable, loop: asyncio.AbstractEventLoop,
        *, timeout_seconds: float = 30.0,
    ):
        if not callable(provider):
            raise TypeError("token source provider must be callable")
        self._provider = provider
        self._loop = loop
        self._timeout_seconds = timeout_seconds
        # Native code copies these strings immediately after each callback returns.
        # Keep the latest response per callback thread for concurrent refreshes.
        self._responses: dict[int, tuple[bytes, bytes]] = {}
        self.last_error: Exception | None = None
        self.callback = TOKEN_SOURCE_CALLBACK(self._invoke)

    async def _call_provider(self, request: TokenSourceFetchOptions):
        result = self._provider(request)
        if inspect.isawaitable(result):
            result = await result
        return result

    def _invoke(self, _user, options, force_refresh, response) -> int:
        if not options or not response:
            return 1
        try:
            value = options.contents
            request = TokenSourceFetchOptions(
                room_name=decode(value.room_name),
                participant_name=decode(value.participant_name),
                participant_identity=decode(value.participant_identity),
                participant_metadata=decode(value.participant_metadata),
                participant_attributes=copy_attributes(
                    value.participant_attributes, value.participant_attribute_count
                ),
                agent_name=decode(value.agent_name),
                agent_metadata=decode(value.agent_metadata),
                deployment=decode(value.deployment),
                force_refresh=bool(force_refresh),
            )
            if self._loop.is_closed():
                raise RuntimeError("room event loop is closed")
            call = self._call_provider(request)
            try:
                future = asyncio.run_coroutine_threadsafe(call, self._loop)
            except RuntimeError:
                call.close()
                raise
            try:
                result = future.result(timeout=self._timeout_seconds)
            except TimeoutError:
                future.cancel()
                raise TimeoutError("token source provider timed out") from None
            if isinstance(result, tuple) and len(result) == 2:
                result = TokenSourceResponse(*result)
            if not isinstance(result, TokenSourceResponse):
                raise TypeError("token source must return TokenSourceResponse or (url, token)")
            server_url = utf8(result.server_url)
            participant_token = utf8(result.participant_token)
            if not server_url or not participant_token:
                raise ValueError("token source returned an empty URL or token")
            self._responses[threading.get_ident()] = (server_url, participant_token)
            response.contents.server_url = server_url
            response.contents.participant_token = participant_token
            self.last_error = None
            return 0
        except Exception as error:
            self.last_error = error
            return 4
