"""Host device enumeration through the C ABI; does not open media devices."""

from __future__ import annotations

import ctypes as C
from dataclasses import dataclass

from ._native import MediaDeviceInfo, ScreenSourceInfo, decode, native


@dataclass(frozen=True)
class MediaDevice:
    id: str
    label: str
    kind: int
    is_default: bool


@dataclass(frozen=True)
class ScreenSource:
    id: str
    label: str
    kind: int
    x: int
    y: int
    width: int
    height: int


def _list(prefix: str, info_type, build):
    sdk = native()
    lib = sdk.lib
    value = C.c_void_p()
    sdk.check(getattr(lib, prefix + "_create")(C.byref(value)))
    try:
        items = []
        for index in range(getattr(lib, prefix + "_count")(value)):
            info = info_type()
            info.struct_size = C.sizeof(info_type)
            sdk.check(getattr(lib, prefix + "_info")(value, index, C.byref(info)))

            def string(suffix: str) -> str:
                getter = getattr(lib, prefix + "_" + suffix)
                size = getter(value, index, None, 0)
                buffer = C.create_string_buffer(size)
                getter(value, index, buffer, size)
                return decode(buffer.value)

            items.append(build(info, string("id"), string("label")))
        return items
    finally:
        getattr(lib, prefix + "_destroy")(value)


def list_media_devices() -> list[MediaDevice]:
    return _list(
        "lk_media_device_list", MediaDeviceInfo,
        lambda info, id, label: MediaDevice(
            id=id, label=label, kind=info.kind, is_default=bool(info.is_default)
        ),
    )


def list_screen_sources() -> list[ScreenSource]:
    return _list(
        "lk_screen_source_list", ScreenSourceInfo,
        lambda info, id, label: ScreenSource(
            id=id, label=label, kind=info.kind, x=info.x, y=info.y,
            width=info.width, height=info.height,
        ),
    )
