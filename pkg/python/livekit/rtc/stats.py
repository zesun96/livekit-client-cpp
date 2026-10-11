"""Detached, normalized RTC track statistics."""

from __future__ import annotations

import ctypes as C
from dataclasses import dataclass
from enum import IntEnum

from ._native import RtcTrackStatsRaw, decode


class RtcStatsDirection(IntEnum):
    UNKNOWN = 0
    SEND = 1
    RECEIVE = 2


@dataclass(frozen=True)
class RtcTrackStats:
    id: str
    kind: str
    rid: str
    codec_mime_type: str
    codec_implementation: str
    quality_limitation_reason: str
    direction: RtcStatsDirection
    timestamp_ms: float
    bytes: int
    packets: int
    packets_lost: int
    bitrate_bps: float | None
    round_trip_time_seconds: float | None
    jitter_seconds: float | None
    audio_level: float | None
    concealed_samples: int
    frame_width: int
    frame_height: int
    frames_per_second: float
    frames: int
    frames_dropped: int
    fir_count: int
    pli_count: int
    nack_count: int
    qp_sum: int


def read_rtc_stats(sdk, snapshot: C.c_void_p) -> tuple[RtcTrackStats, ...]:
    """Copy a caller-owned native snapshot and release it."""
    lib = sdk.lib
    try:
        result = []
        for index in range(lib.lk_rtc_stats_snapshot_count(snapshot)):
            raw = RtcTrackStatsRaw()
            lib.lk_rtc_track_stats_init(C.byref(raw))
            sdk.check(lib.lk_rtc_stats_snapshot_info(snapshot, index, C.byref(raw)))
            def value(name: str) -> str:
                fn = getattr(lib, "lk_rtc_stats_snapshot_" + name)
                size = fn(snapshot, index, None, 0)
                if not size:
                    return ""
                buffer = C.create_string_buffer(size)
                fn(snapshot, index, buffer, size)
                return decode(buffer.value)
            result.append(RtcTrackStats(
                id=value("id"), kind=value("kind"), rid=value("rid"),
                codec_mime_type=value("codec_mime_type"),
                codec_implementation=value("codec_implementation"),
                quality_limitation_reason=value("quality_limitation_reason"),
                direction=RtcStatsDirection(raw.direction),
                timestamp_ms=raw.timestamp_ms, bytes=raw.bytes,
                packets=raw.packets, packets_lost=raw.packets_lost,
                bitrate_bps=raw.bitrate_bps if raw.has_bitrate_bps else None,
                round_trip_time_seconds=(raw.round_trip_time_seconds
                    if raw.has_round_trip_time_seconds else None),
                jitter_seconds=raw.jitter_seconds if raw.has_jitter_seconds else None,
                audio_level=raw.audio_level if raw.has_audio_level else None,
                concealed_samples=raw.concealed_samples,
                frame_width=raw.frame_width, frame_height=raw.frame_height,
                frames_per_second=raw.frames_per_second, frames=raw.frames,
                frames_dropped=raw.frames_dropped, fir_count=raw.fir_count,
                pli_count=raw.pli_count, nack_count=raw.nack_count,
                qp_sum=raw.qp_sum,
            ))
        return tuple(result)
    finally:
        lib.lk_rtc_stats_snapshot_destroy(snapshot)
