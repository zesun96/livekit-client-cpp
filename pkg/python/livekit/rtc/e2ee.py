"""Room E2EE configuration and key control through the C ABI."""

from __future__ import annotations

import ctypes as C
from dataclasses import dataclass
from enum import IntEnum
from typing import TYPE_CHECKING

from ._native import E2EEOptionsRaw, FrameCryptorInfoRaw, LiveKitError, decode, utf8

if TYPE_CHECKING:
    from .room import Room


class KeyDerivation(IntEnum):
    PBKDF2_SHA256 = 0
    HKDF_SHA256 = 1


class FrameCryptorDirection(IntEnum):
    SENDER = 0
    RECEIVER = 1


@dataclass(frozen=True)
class E2EEOptions:
    enabled: bool = True
    shared_key: bytes | None = None
    ratchet_salt: bytes | None = None
    unencrypted_magic_bytes: bytes | None = None
    ratchet_window_size: int | None = None
    failure_tolerance: int | None = None
    key_ring_size: int | None = None
    key_derivation: KeyDerivation | None = None

    def _native(self, library) -> E2EEOptionsRaw:
        raw = E2EEOptionsRaw()
        library.lk_e2ee_options_init(C.byref(raw))
        raw.enabled = int(self.enabled)
        for name in ("shared_key", "ratchet_salt", "unencrypted_magic_bytes"):
            value = getattr(self, name)
            if value is None:
                continue
            if not isinstance(value, bytes):
                raise TypeError(f"{name} must be bytes")
            if name == "shared_key" and not value:
                raise ValueError("shared_key must not be empty")
            buffer = (C.c_uint8 * len(value)).from_buffer_copy(value)
            setattr(raw, name, buffer)
            setattr(raw, name + "_size", len(value))
        for name in ("ratchet_window_size", "key_ring_size"):
            value = getattr(self, name)
            if value is not None:
                if value < 0:
                    raise ValueError(f"{name} must be nonnegative")
                setattr(raw, name, value)
        if self.failure_tolerance is not None:
            raw.failure_tolerance = self.failure_tolerance
        if self.key_derivation is not None:
            raw.key_derivation = int(self.key_derivation)
        return raw


@dataclass(frozen=True)
class EncryptionState:
    track_id: str
    participant_identity: str
    kind: int
    direction: int
    enabled: bool
    key_index: int
    state: int

    @classmethod
    def from_native(cls, value) -> EncryptionState:
        return cls(
            track_id=decode(value.track_id),
            participant_identity=decode(value.participant_identity),
            kind=value.kind, direction=value.direction,
            enabled=bool(value.enabled), key_index=value.key_index,
            state=value.state,
        )


@dataclass(frozen=True)
class FrameCryptor:
    track_id: str
    participant_identity: str
    kind: int
    direction: FrameCryptorDirection
    enabled: bool
    key_index: int
    state: int


def _index(value: int) -> int:
    if not isinstance(value, int) or value < 0:
        raise ValueError("key_index must be a nonnegative integer")
    return value


def _key_buffer(value: bytes):
    if not isinstance(value, bytes) or not value:
        raise ValueError("key must be nonempty bytes")
    buffer = C.create_string_buffer(value)
    return C.cast(buffer, C.c_void_p), len(value)


class E2EEManager:
    def __init__(self, room: Room):
        self._room = room

    @property
    def is_configured(self) -> bool:
        self._room._assert_open()
        return bool(self._room._native.lib.lk_room_e2ee_is_configured(self._room._handle))

    @property
    def is_enabled(self) -> bool:
        self._room._assert_open()
        return bool(self._room._native.lib.lk_room_e2ee_is_enabled(self._room._handle))

    @property
    def data_key_index(self) -> int:
        self._room._assert_open()
        return self._room._native.lib.lk_room_e2ee_data_key_index(self._room._handle)

    async def set_enabled(self, enabled: bool) -> None:
        await self._room._run(
            self._room._native.lib.lk_room_e2ee_set_enabled,
            self._room._handle, int(enabled),
        )

    async def set_shared_key(self, key: bytes, key_index: int = 0) -> None:
        pointer, size = _key_buffer(key)
        await self._room._run(
            self._room._native.lib.lk_room_e2ee_set_shared_key,
            self._room._handle, pointer, size, _index(key_index),
        )

    async def export_shared_key(self, key_index: int = 0) -> bytes | None:
        index = _index(key_index)
        lib = self._room._native.lib

        def call():
            if not lib.lk_room_e2ee_is_configured(self._room._handle):
                raise LiveKitError("E2EE is not configured for this room")
            size = lib.lk_room_e2ee_export_shared_key(self._room._handle, index, None, 0)
            if not size:
                message = decode(lib.lk_last_error())
                if message:
                    raise LiveKitError(message)
                return None
            buffer = (C.c_uint8 * size)()
            actual = lib.lk_room_e2ee_export_shared_key(
                self._room._handle, index, C.cast(buffer, C.c_void_p), size
            )
            return bytes(buffer[:actual])

        return await self._room._run_callable(call)

    async def ratchet_shared_key(self, key_index: int = 0) -> None:
        await self._room._run(
            self._room._native.lib.lk_room_e2ee_ratchet_shared_key,
            self._room._handle, _index(key_index),
        )

    async def remove_shared_key(self, key_index: int = 0) -> None:
        await self._room._run(
            self._room._native.lib.lk_room_e2ee_remove_shared_key,
            self._room._handle, _index(key_index),
        )

    async def set_participant_key(
        self, participant_identity: str, key: bytes, key_index: int = 0,
    ) -> None:
        pointer, size = _key_buffer(key)
        await self._room._run(
            self._room._native.lib.lk_room_e2ee_set_participant_key,
            self._room._handle, utf8(participant_identity), pointer, size, _index(key_index),
        )

    async def export_participant_key(
        self, participant_identity: str, key_index: int = 0,
    ) -> bytes | None:
        identity = utf8(participant_identity)
        index = _index(key_index)
        lib = self._room._native.lib

        def call():
            if not lib.lk_room_e2ee_is_configured(self._room._handle):
                raise LiveKitError("E2EE is not configured for this room")
            size = lib.lk_room_e2ee_export_participant_key(
                self._room._handle, identity, index, None, 0
            )
            if not size:
                message = decode(lib.lk_last_error())
                if message:
                    raise LiveKitError(message)
                return None
            buffer = (C.c_uint8 * size)()
            actual = lib.lk_room_e2ee_export_participant_key(
                self._room._handle, identity, index, C.cast(buffer, C.c_void_p), size
            )
            return bytes(buffer[:actual])

        return await self._room._run_callable(call)

    async def ratchet_participant_key(
        self, participant_identity: str, key_index: int = 0,
    ) -> None:
        await self._room._run(
            self._room._native.lib.lk_room_e2ee_ratchet_participant_key,
            self._room._handle, utf8(participant_identity), _index(key_index),
        )

    async def remove_participant_key(
        self, participant_identity: str, key_index: int = 0,
    ) -> None:
        await self._room._run(
            self._room._native.lib.lk_room_e2ee_remove_participant_key,
            self._room._handle, utf8(participant_identity), _index(key_index),
        )

    async def clear_keys(self) -> None:
        await self._room._run(
            self._room._native.lib.lk_room_e2ee_clear_keys, self._room._handle
        )

    async def set_data_key_index(self, key_index: int) -> None:
        await self._room._run(
            self._room._native.lib.lk_room_e2ee_set_data_key_index,
            self._room._handle, _index(key_index),
        )

    async def set_frame_cryptor_enabled(
        self, track_id: str, direction: FrameCryptorDirection, enabled: bool,
    ) -> None:
        await self._room._run(
            self._room._native.lib.lk_room_e2ee_set_frame_cryptor_enabled,
            self._room._handle, utf8(track_id), int(direction), int(enabled),
        )

    async def set_frame_cryptor_key_index(
        self, track_id: str, direction: FrameCryptorDirection, key_index: int,
    ) -> None:
        await self._room._run(
            self._room._native.lib.lk_room_e2ee_set_frame_cryptor_key_index,
            self._room._handle, utf8(track_id), int(direction), _index(key_index),
        )

    async def set_participant_enabled(
        self, participant_identity: str, enabled: bool,
    ) -> int:
        count = C.c_size_t()
        await self._room._run(
            self._room._native.lib.lk_room_e2ee_set_participant_enabled,
            self._room._handle, utf8(participant_identity), int(enabled), C.byref(count),
        )
        return count.value

    async def frame_cryptors(self) -> list[FrameCryptor]:
        sdk = self._room._native
        lib = sdk.lib

        def call():
            snapshot = C.c_void_p()
            sdk.check(lib.lk_frame_cryptor_list_create(
                self._room._handle, C.byref(snapshot)
            ))
            try:
                cryptors = []
                for index in range(lib.lk_frame_cryptor_list_count(snapshot)):
                    info = FrameCryptorInfoRaw()
                    info.struct_size = C.sizeof(info)
                    sdk.check(lib.lk_frame_cryptor_list_info(
                        snapshot, index, C.byref(info)
                    ))

                    def string(suffix: str) -> str:
                        fn = getattr(lib, "lk_frame_cryptor_list_" + suffix)
                        size = fn(snapshot, index, None, 0)
                        value = C.create_string_buffer(size)
                        fn(snapshot, index, value, size)
                        return decode(value.value)

                    cryptors.append(FrameCryptor(
                        track_id=string("track_id"),
                        participant_identity=string("participant_identity"),
                        kind=info.kind,
                        direction=FrameCryptorDirection(info.direction),
                        enabled=bool(info.enabled),
                        key_index=info.key_index, state=info.state,
                    ))
                return cryptors
            finally:
                lib.lk_frame_cryptor_list_destroy(snapshot)

        return await self._room._run_callable(call)
