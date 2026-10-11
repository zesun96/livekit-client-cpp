"""Remote track recording handles."""

from __future__ import annotations

import ctypes as C
import threading
from dataclasses import dataclass
from enum import IntEnum

from ._native import LiveKitError, TrackRecorderStatsRaw


class TrackRecorderState(IntEnum):
    RECORDING = 0
    STOPPED = 1
    FAILED = 2


@dataclass(frozen=True)
class TrackRecorderStats:
    state: TrackRecorderState
    frames_written: int
    bytes_written: int
    frames_dropped: int
    output_path: str
    error: str


class TrackRecorder:
    """Records a subscribed remote track to a file on the local machine."""

    def __init__(self, room, handle: C.c_void_p):
        self._room = room
        self._handle = handle
        self._lock = threading.Lock()

    @property
    def stats(self) -> TrackRecorderStats:
        with self._lock:
            if not self._handle:
                raise LiveKitError("track recorder is closed")
            lib = self._room._native.lib
            raw = TrackRecorderStatsRaw()
            lib.lk_track_recorder_stats_init(C.byref(raw))
            self._room._native.check(lib.lk_track_recorder_get_stats(
                self._handle, C.byref(raw)
            ))
            return TrackRecorderStats(
                state=TrackRecorderState(raw.state),
                frames_written=raw.frames_written,
                bytes_written=raw.bytes_written,
                frames_dropped=raw.frames_dropped,
                output_path=self._room._native.string(
                    "lk_track_recorder_output_path", self._handle
                ),
                error=self._room._native.string("lk_track_recorder_error", self._handle),
            )

    async def stop(self) -> None:
        def call():
            with self._lock:
                if self._handle:
                    self._room._native.lib.lk_track_recorder_stop(self._handle)

        await self._room._run_callable(call)

    async def aclose(self) -> None:
        await self._room._run_callable(self._destroy)

    def _destroy(self) -> None:
        with self._lock:
            if self._handle:
                lib = self._room._native.lib
                lib.lk_track_recorder_stop(self._handle)
                lib.lk_track_recorder_destroy(self._handle)
                self._handle = C.c_void_p()
                self._room._recorders.discard(self)
