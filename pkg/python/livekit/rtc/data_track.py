"""DataTrack schemas, publishers, snapshots, and bounded pull readers."""

from __future__ import annotations

import asyncio
import ctypes as C
import threading
from dataclasses import dataclass
from enum import IntEnum

from ._native import (
    DataTrackPublishOptionsRaw, DataTrackSchemaIdRaw, DataTrackSnapshotInfoRaw,
    DataTrackSubscriptionOptionsRaw, LiveKitError, decode, utf8,
)


class DataTrackFrameEncoding(IntEnum):
    UNSPECIFIED = 0
    ROS1 = 1
    CDR = 2
    PROTOBUF = 3
    FLATBUFFER = 4
    CBOR = 5
    MSGPACK = 6
    JSON = 7
    CUSTOM = 8


class DataTrackSchemaEncoding(IntEnum):
    UNSPECIFIED = 0
    PROTOBUF = 1
    FLATBUFFER = 2
    ROS1_MESSAGE = 3
    ROS2_MESSAGE = 4
    ROS2_IDL = 5
    OMG_IDL = 6
    JSON_SCHEMA = 7
    CUSTOM = 8


class DataTrackError(LiveKitError):
    pass


def _check(native, code: int) -> None:
    if code:
        raise DataTrackError(decode(native.lib.lk_last_error()) or
                             f"DataTrack error {code}", code)


@dataclass(frozen=True)
class DataTrackSchemaId:
    name: str
    encoding: DataTrackSchemaEncoding
    custom_encoding: str = ""

    def _raw(self, lib) -> DataTrackSchemaIdRaw:
        raw = DataTrackSchemaIdRaw()
        lib.lk_data_track_schema_id_init(C.byref(raw))
        raw.name = utf8(self.name)
        raw.encoding = int(self.encoding)
        raw.custom_encoding = utf8(self.custom_encoding)
        return raw


@dataclass(frozen=True)
class DataTrackSchema:
    id: DataTrackSchemaId
    definition: bytes


@dataclass(frozen=True)
class DataTrackPublishOptions:
    name: str
    frame_encoding: DataTrackFrameEncoding | None = None
    custom_frame_encoding: str = ""
    schema: DataTrackSchemaId | None = None

    def _raw(self, lib) -> DataTrackPublishOptionsRaw:
        raw = DataTrackPublishOptionsRaw()
        lib.lk_data_track_publish_options_init(C.byref(raw))
        raw.name = utf8(self.name)
        if self.frame_encoding is not None:
            raw.has_frame_encoding = 1
            raw.frame_encoding = int(self.frame_encoding)
            raw.custom_frame_encoding = utf8(self.custom_frame_encoding)
        if self.schema is not None:
            raw.has_schema = 1
            schema = self.schema._raw(lib)
            raw.schema = schema
            raw._schema_keepalive = schema
        return raw


@dataclass(frozen=True)
class DataTrackSubscriptionOptions:
    target_fps: int | None = None
    buffer_capacity: int = 16
    max_partial_frames: int = 1

    def _raw(self, lib) -> DataTrackSubscriptionOptionsRaw:
        raw = DataTrackSubscriptionOptionsRaw()
        lib.lk_data_track_subscription_options_init(C.byref(raw))
        if self.target_fps is not None:
            if not 0 < self.target_fps <= 0xFFFFFFFF:
                raise ValueError("target_fps must be positive")
            raw.has_target_fps = 1
            raw.target_fps = self.target_fps
        if self.buffer_capacity <= 0 or self.max_partial_frames <= 0:
            raise ValueError("DataTrack reader capacities must be positive")
        raw.buffer_capacity = self.buffer_capacity
        raw.max_partial_frames = self.max_partial_frames
        return raw


@dataclass(frozen=True)
class DataTrackFrame:
    data: bytes
    user_timestamp: int | None = None


@dataclass(frozen=True)
class RemoteDataTrack:
    publisher_identity: str
    sid: str
    name: str
    publisher_handle: int
    uses_e2ee: bool
    frame_encoding: int | None
    custom_frame_encoding: str
    schema_name: str
    schema_encoding: int | None
    custom_schema_encoding: str
    is_published: bool


@dataclass(frozen=True)
class DataTrackPublication:
    publisher_handle: int
    sid: str
    name: str
    uses_e2ee: bool
    frame_encoding: int | None
    custom_frame_encoding: str
    schema_name: str
    schema_encoding: int | None
    custom_schema_encoding: str

    @classmethod
    def from_native(cls, value):
        return cls(
            publisher_handle=value.publisher_handle,
            sid=decode(value.sid), name=decode(value.name),
            uses_e2ee=bool(value.uses_e2ee),
            frame_encoding=value.frame_encoding if value.has_frame_encoding else None,
            custom_frame_encoding=decode(value.custom_frame_encoding),
            schema_name=decode(value.schema_name),
            schema_encoding=value.schema_encoding if value.has_schema else None,
            custom_schema_encoding=decode(value.custom_schema_encoding),
        )


class LocalDataTrack:
    def __init__(self, room, handle: C.c_void_p):
        self._room = room
        self._handle = handle

    def _assert_open(self):
        if not self._handle:
            raise LiveKitError("local DataTrack is closed")

    @property
    def sid(self) -> str:
        self._assert_open()
        return self._room._native.string("lk_local_data_track_sid", self._handle)

    @property
    def name(self) -> str:
        self._assert_open()
        return self._room._native.string("lk_local_data_track_name", self._handle)

    @property
    def is_published(self) -> bool:
        self._assert_open()
        return bool(self._room._native.lib.lk_local_data_track_is_published(self._handle))

    @property
    def publication(self) -> DataTrackPublication:
        """Detached metadata for this local DataTrack."""
        self._assert_open()
        lib = self._room._native.lib
        raw = DataTrackSnapshotInfoRaw()
        lib.lk_data_track_snapshot_info_init(C.byref(raw))
        self._room._native.check(lib.lk_local_data_track_info(
            self._handle, C.byref(raw)
        ))
        get = lambda name: self._room._native.string(
            "lk_local_data_track_" + name, self._handle
        )
        return DataTrackPublication(
            publisher_handle=raw.publisher_handle,
            sid=get("sid"), name=get("name"), uses_e2ee=bool(raw.uses_e2ee),
            frame_encoding=raw.frame_encoding if raw.has_frame_encoding else None,
            custom_frame_encoding=get("custom_frame_encoding"),
            schema_name=get("schema_name"),
            schema_encoding=raw.schema_encoding if raw.has_schema else None,
            custom_schema_encoding=get("custom_schema_encoding"),
        )

    async def try_push(self, data: bytes, *, user_timestamp: int | None = None) -> None:
        if not isinstance(data, bytes):
            raise TypeError("DataTrack frame must be bytes")
        if user_timestamp is not None and not 0 <= user_timestamp <= 0xFFFFFFFFFFFFFFFF:
            raise ValueError("user_timestamp is out of range")
        self._assert_open()
        buffer = C.create_string_buffer(data)

        def call():
            _check(self._room._native, self._room._native.lib.lk_local_data_track_try_push(
                self._handle, C.cast(buffer, C.c_void_p), len(data),
                int(user_timestamp is not None), user_timestamp or 0,
            ))
        await self._room._run_callable(call)

    async def unpublish(self) -> None:
        self._assert_open()

        def call():
            _check(self._room._native,
                   self._room._native.lib.lk_local_data_track_unpublish(self._handle))
        await self._room._run_callable(call)

    async def aclose(self) -> None:
        if not self._handle:
            return
        if self._room._closed:
            await self._room.aclose()
            return

        def destroy():
            _check(self._room._native,
                   self._room._native.lib.lk_local_data_track_destroy(self._handle))
            self._handle = C.c_void_p()
            self._room._local_data_tracks.discard(self)
        await self._room._run_callable(destroy)


class DataTrackReader:
    def __init__(self, room, handle: C.c_void_p):
        self._room = room
        self._native = room._native
        self._handle = handle
        self._lock = threading.Lock()
        self._close_lock = threading.Lock()

    @property
    def is_closed(self) -> bool:
        with self._lock:
            return not self._handle or bool(
                self._native.lib.lk_data_track_reader_is_closed(self._handle)
            )

    @property
    def dropped_frames(self) -> int:
        with self._lock:
            return int(self._native.lib.lk_data_track_reader_dropped_frames(self._handle)) \
                if self._handle else 0

    async def read(self, timeout_ms: int = 0) -> DataTrackFrame | None:
        if timeout_ms < 0 or timeout_ms > 0xFFFFFFFF:
            raise ValueError("timeout_ms is out of range")

        def receive():
            with self._lock:
                if not self._handle:
                    return None
                lib = self._native.lib
                frame = C.c_void_p()
                status = lib.lk_data_track_reader_read_for(
                    self._handle, timeout_ms, C.byref(frame)
                )
                if status in (1, 2):
                    return None
                if status != 0:
                    raise DataTrackError(decode(lib.lk_last_error()) or
                                         "DataTrack read failed", status)
                try:
                    size = lib.lk_data_track_frame_data(frame, None, 0)
                    buffer = C.create_string_buffer(size)
                    lib.lk_data_track_frame_data(frame, C.cast(buffer, C.c_void_p), size)
                    timestamp = (lib.lk_data_track_frame_user_timestamp(frame)
                                 if lib.lk_data_track_frame_has_user_timestamp(frame) else None)
                    return DataTrackFrame(bytes(buffer.raw[:size]), timestamp)
                finally:
                    lib.lk_data_track_frame_destroy(frame)

        return await asyncio.to_thread(receive)

    def close(self) -> None:
        with self._close_lock:
            if not self._handle:
                return
            self._native.lib.lk_data_track_reader_close(self._handle)
            with self._lock:
                self._native.lib.lk_data_track_reader_destroy(self._handle)
                self._handle = C.c_void_p()
                self._room._data_track_readers.discard(self)
