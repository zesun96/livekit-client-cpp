"""Dispatch copied native events on one asyncio loop with bounded storage."""

from __future__ import annotations

import asyncio
import logging
import threading
from collections import deque
from typing import Callable


_log = logging.getLogger(__name__)


class EventDispatcher:
    def __init__(self, loop_getter: Callable[[], asyncio.AbstractEventLoop | None],
                 capacity: int = 1024):
        if capacity <= 0:
            raise ValueError("capacity must be positive")
        self._loop_getter = loop_getter
        self._capacity = capacity
        self._listeners: dict[str, list[Callable]] = {}
        self.stream_handlers: dict[tuple[str, str], Callable] = {}
        self._pending: deque[tuple[str, tuple]] = deque()
        self._lock = threading.Lock()
        self._scheduled = False
        self._closed = False
        self._dropped = 0

    def on(self, event: str, callback: Callable | None = None):
        if callback is None:
            return lambda fn: self.on(event, fn)
        if not callable(callback):
            raise TypeError("callback must be callable")
        self._listeners.setdefault(event, []).append(callback)
        return callback

    def off(self, event: str, callback: Callable) -> None:
        listeners = self._listeners.get(event, [])
        if callback in listeners:
            listeners.remove(callback)

    def post(self, event: str, *args) -> None:
        loop = self._loop_getter()
        if loop is None or loop.is_closed():
            return
        with self._lock:
            if self._closed:
                return
            if len(self._pending) == self._capacity:
                self._pending.popleft()
                self._dropped += 1
            self._pending.append((event, args))
            if self._scheduled:
                return
            self._scheduled = True
        try:
            loop.call_soon_threadsafe(self._drain)
        except RuntimeError:
            with self._lock:
                self._pending.clear()
                self._scheduled = False

    def _drain(self) -> None:
        for _ in range(256):
            with self._lock:
                if self._closed or not self._pending:
                    self._scheduled = False
                    return
                event, args = self._pending.popleft()
            self._emit(event, *args)
        loop = self._loop_getter()
        if loop is not None and not loop.is_closed():
            loop.call_soon(self._drain)

    def _emit(self, event: str, *args) -> None:
        callbacks = list(self._listeners.get(event, ()))
        if event in ("text_stream_event", "byte_stream_event"):
            kind = "text" if event == "text_stream_event" else "byte"
            handler = self.stream_handlers.get((kind, args[0].topic))
            if handler is not None:
                callbacks.append(handler)
        for callback in callbacks:
            try:
                result = callback(*args)
                if asyncio.iscoroutine(result):
                    task = asyncio.create_task(result)
                    task.add_done_callback(self._task_done)
            except Exception:
                _log.exception("LiveKit event handler failed: %s", event)

    @staticmethod
    def _task_done(task: asyncio.Task) -> None:
        if task.cancelled():
            return
        try:
            task.result()
        except Exception:
            _log.exception("LiveKit async event handler failed")

    @property
    def dropped_events(self) -> int:
        with self._lock:
            return self._dropped

    def close(self) -> None:
        with self._lock:
            self._closed = True
            self._pending.clear()
            self._scheduled = False
        self._listeners.clear()
        self.stream_handlers.clear()
