"""Process-wide SDK logging and tracing controls."""

from __future__ import annotations

import ctypes as C
import threading
from dataclasses import dataclass
from enum import IntEnum, IntFlag
from typing import Callable

from ._native import (
    LogOptionsRaw, LogRecordRaw, TraceOptionsRaw, TraceRecordRaw, decode, native, utf8,
)


class LogLevel(IntEnum):
    TRACE = 0
    DEBUG = 1
    INFO = 2
    WARNING = 3
    ERROR = 4
    OFF = 5


class LogSource(IntEnum):
    LIVEKIT = 0
    WEBRTC = 1
    WEBSOCKET = 2


class TraceCategory(IntFlag):
    LIFECYCLE = 1 << 0
    SIGNALING = 1 << 1
    TRANSPORT = 1 << 2
    TRACK = 1 << 3
    DATA = 1 << 4
    RPC = 1 << 5
    E2EE = 1 << 6
    ALL = (1 << 7) - 1


class TracePhase(IntEnum):
    INSTANT = 0
    DURATION_BEGIN = 1
    DURATION_END = 2
    ASYNC_BEGIN = 3
    ASYNC_END = 4


@dataclass(frozen=True)
class LogRecord:
    level: LogLevel
    source: LogSource
    message: str
    file: str
    line: int


@dataclass(frozen=True)
class TraceRecord:
    phase: TracePhase
    category: TraceCategory
    name: str
    timestamp_us: int
    thread_id: int
    correlation_id: int


_LOG_CALLBACK = C.CFUNCTYPE(None, C.c_void_p, C.POINTER(LogRecordRaw))
_TRACE_CALLBACK = C.CFUNCTYPE(None, C.c_void_p, C.POINTER(TraceRecordRaw))
_callback_lock = threading.RLock()
_log_callback = None
_trace_callback = None


def get_log_levels() -> tuple[LogLevel, LogLevel, LogLevel]:
    sdk = native()
    raw = LogOptionsRaw()
    sdk.lib.lk_log_options_init(C.byref(raw))
    sdk.check(sdk.lib.lk_log_get_options(C.byref(raw)))
    return tuple(LogLevel(value) for value in (
        raw.livekit_level, raw.webrtc_level, raw.websocket_level
    ))


def set_log_levels(
    livekit: LogLevel, webrtc: LogLevel, websocket: LogLevel,
) -> None:
    sdk = native()
    raw = LogOptionsRaw()
    sdk.lib.lk_log_options_init(C.byref(raw))
    raw.livekit_level = int(LogLevel(livekit))
    raw.webrtc_level = int(LogLevel(webrtc))
    raw.websocket_level = int(LogLevel(websocket))
    sdk.check(sdk.lib.lk_log_set_options(C.byref(raw)))


def set_log_callback(handler: Callable[[LogRecord], None] | None) -> None:
    """Install a short, thread-safe callback; pass None to unregister it."""
    global _log_callback
    if handler is not None and not callable(handler):
        raise TypeError("handler must be callable or None")

    def invoke(_user, record):
        try:
            value = record.contents
            handler(LogRecord(
                LogLevel(value.level), LogSource(value.source),
                decode(value.message), decode(value.file), value.line,
            ))
        except Exception:
            pass  # Python exceptions must not cross the C callback boundary.

    with _callback_lock:
        callback = _LOG_CALLBACK(invoke) if handler is not None else None
        sdk = native()
        sdk.check(sdk.lib.lk_log_set_callback(
            C.cast(callback, C.c_void_p) if callback else None, None
        ))
        _log_callback = callback


def get_trace_options() -> tuple[bool, TraceCategory]:
    sdk = native()
    raw = TraceOptionsRaw()
    sdk.lib.lk_trace_options_init(C.byref(raw))
    sdk.check(sdk.lib.lk_trace_get_options(C.byref(raw)))
    return bool(raw.enabled), TraceCategory(raw.category_mask)


def set_trace_options(*, enabled: bool, categories: TraceCategory = TraceCategory.ALL) -> None:
    if int(categories) & ~int(TraceCategory.ALL):
        raise ValueError("invalid trace category mask")
    sdk = native()
    raw = TraceOptionsRaw()
    sdk.lib.lk_trace_options_init(C.byref(raw))
    raw.enabled = int(enabled)
    raw.category_mask = int(categories)
    sdk.check(sdk.lib.lk_trace_set_options(C.byref(raw)))


def set_trace_callback(handler: Callable[[TraceRecord], None] | None) -> None:
    """Install a short, thread-safe callback; pass None to unregister it."""
    global _trace_callback
    if handler is not None and not callable(handler):
        raise TypeError("handler must be callable or None")

    def invoke(_user, record):
        try:
            value = record.contents
            handler(TraceRecord(
                TracePhase(value.phase), TraceCategory(value.category),
                decode(value.name), value.timestamp_us, value.thread_id,
                value.correlation_id,
            ))
        except Exception:
            pass

    with _callback_lock:
        callback = _TRACE_CALLBACK(invoke) if handler is not None else None
        sdk = native()
        sdk.check(sdk.lib.lk_trace_set_callback(
            C.cast(callback, C.c_void_p) if callback else None, None
        ))
        _trace_callback = callback


def start_trace_json_file(path: str) -> None:
    if not path:
        raise ValueError("trace JSON path is required")
    sdk = native()
    sdk.check(sdk.lib.lk_trace_start_json_file(utf8(path)))


def stop_trace() -> None:
    sdk = native()
    sdk.check(sdk.lib.lk_trace_stop())
