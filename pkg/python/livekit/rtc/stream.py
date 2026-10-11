"""Incremental outgoing data streams."""

from __future__ import annotations

import ctypes as C

from ._native import LiveKitError, utf8


class StreamWriter:
    """A room-owned text or byte stream writer."""

    def __init__(self, room, kind: str, handle: C.c_void_p):
        self._room = room
        self._kind = kind
        self._handle = handle
        self._prefix = f"lk_{'text' if kind == 'text' else 'byte'}_stream_writer_"
        self._id = room._native.string(self._prefix + "id", handle)

    @property
    def id(self) -> str:
        return self._id

    @property
    def is_closed(self) -> bool:
        return not self._handle or bool(getattr(
            self._room._native.lib, self._prefix + "is_closed"
        )(self._handle))

    async def write(self, chunk: str | bytes) -> None:
        if self._kind == "text":
            if not isinstance(chunk, str):
                raise TypeError("text stream chunks must be strings")
            data = utf8(chunk)
        else:
            if not isinstance(chunk, bytes):
                raise TypeError("byte stream chunks must be bytes")
            data = chunk

        def call():
            if not self._handle:
                raise LiveKitError("stream writer is closed")
            buffer = C.create_string_buffer(data)
            value = (C.cast(buffer, C.c_void_p) if self._kind == "bytes" else data)
            self._room._native.check(getattr(
                self._room._native.lib, self._prefix + "write"
            )(self._handle, value, len(data)))

        await self._room._run_callable(call)

    async def close(self) -> None:
        await self._finish("close", None)

    async def cancel(self, reason: str = "cancelled") -> None:
        await self._finish("cancel", utf8(reason))

    async def _finish(self, operation: str, reason: bytes | None) -> None:
        def call():
            if not self._handle:
                return
            fn = getattr(self._room._native.lib, self._prefix + operation)
            self._room._native.check(fn(self._handle, reason) if reason is not None
                                    else fn(self._handle))
            self._destroy()

        await self._room._run_callable(call)

    def _destroy(self) -> None:
        if self._handle:
            getattr(self._room._native.lib, self._prefix + "destroy")(self._handle)
            self._handle = C.c_void_p()
            self._room._stream_writers.discard(self)
