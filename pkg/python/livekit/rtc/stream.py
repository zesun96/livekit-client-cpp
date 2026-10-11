"""Incremental outgoing data streams."""

from __future__ import annotations

import ctypes as C
from dataclasses import dataclass
from enum import IntEnum

from ._native import (
    LiveKitError, STREAM_COMPLETION, STREAM_PROGRESS, decode, utf8,
)


class StreamCompletionStatus(IntEnum):
    COMPLETED = 0
    CANCELLED = 1
    FAILED = 2


@dataclass(frozen=True)
class StreamProgress:
    stream_id: str
    bytes_sent: int
    total_size: int | None


@dataclass(frozen=True)
class StreamCompletion:
    status: StreamCompletionStatus
    stream_id: str
    bytes_sent: int
    total_size: int | None
    reason: str
    error_domain: int
    error_code: int


class StreamCallbackBridge:
    def __init__(self, room):
        self.stream_id = ""

        def on_progress(_user, sent, has_total, total):
            room._post("stream_progress", StreamProgress(
                self.stream_id, sent, total if has_total else None
            ))

        def on_complete(_user, raw):
            try:
                value = raw.contents
                room._post("stream_complete", StreamCompletion(
                    status=StreamCompletionStatus(value.status),
                    stream_id=decode(value.stream_id),
                    bytes_sent=value.bytes_sent,
                    total_size=value.total_size if value.has_total_size else None,
                    reason=decode(value.reason), error_domain=value.error_domain,
                    error_code=value.error_code,
                ))
            except Exception:
                pass

        self.progress = STREAM_PROGRESS(on_progress)
        self.completion = STREAM_COMPLETION(on_complete)


class StreamWriter:
    """A room-owned text or byte stream writer."""

    def __init__(self, room, kind: str, handle: C.c_void_p,
                 callbacks: StreamCallbackBridge):
        self._room = room
        self._kind = kind
        self._handle = handle
        self._prefix = f"lk_{'text' if kind == 'text' else 'byte'}_stream_writer_"
        self._id = room._native.string(self._prefix + "id", handle)
        self._callbacks = callbacks
        callbacks.stream_id = self._id

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
