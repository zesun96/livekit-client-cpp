"""Build borrowed C connection and subscription options with explicit lifetimes."""

from __future__ import annotations

import ctypes as C

from ._models import ParticipantTrackPermission, ReconnectContext, ReconnectReason, RoomOptions
from ._native import (
    IceServerRaw, ParticipantTrackPermissionRaw, RECONNECT_POLICY,
    RoomConnectOptions, decode, utf8,
)


def connection_options(lib, options: RoomOptions) -> tuple[RoomConnectOptions, object | None]:
    raw = RoomConnectOptions()
    lib.lk_room_connect_options_init(C.byref(raw))
    raw.auto_subscribe = int(options.auto_subscribe)
    raw.adaptive_stream = int(options.adaptive_stream)
    raw.dynacast = int(options.dynacast)
    if options.join_retries is not None:
        raw.join_retries = options.join_retries
    if options.reconnect_timeout_ms is not None:
        raw.reconnect_timeout_ms = options.reconnect_timeout_ms

    keepalive: list[object] = []
    if options.rtc_config is not None:
        config = options.rtc_config
        raw.continual_gathering_policy = int(config.continual_gathering_policy)
        raw.ice_transport_type = int(config.ice_transport_type)
        servers = (IceServerRaw * len(config.ice_servers))()
        for index, server in enumerate(config.ice_servers):
            if not server.urls:
                raise ValueError("ICE server must have at least one URL")
            lib.lk_ice_server_init(C.byref(servers[index]))
            urls = (C.c_char_p * len(server.urls))(*(utf8(url) for url in server.urls))
            servers[index].urls = urls
            servers[index].url_count = len(server.urls)
            servers[index].username = utf8(server.username)
            servers[index].password = utf8(server.password)
            keepalive.append(urls)
        raw.ice_servers = servers
        raw.ice_server_count = len(servers)
        keepalive.append(servers)

    encryption = options.encryption or options.e2ee
    if encryption is not None:
        e2ee = encryption._native(lib)
        raw.e2ee_options = C.pointer(e2ee)
        keepalive.append(e2ee)

    callback = None
    if options.reconnect_policy is not None:
        if not callable(options.reconnect_policy):
            raise TypeError("reconnect_policy must be callable")

        def invoke(_user, context, delay):
            try:
                value = context.contents
                reason = ReconnectReason._value2member_map_.get(
                    value.reason, ReconnectReason.UNKNOWN
                )
                decision = options.reconnect_policy(ReconnectContext(
                    retry_count=value.retry_count, elapsed_ms=value.elapsed_ms,
                    reason=reason, server_url=decode(value.server_url),
                ))
                if decision is None or not isinstance(decision, int):
                    return 0
                if not 0 <= decision <= 0xFFFFFFFF:
                    return 0
                delay[0] = decision
                return 1
            except Exception:
                # ctypes callbacks must never propagate exceptions into native code.
                return 0

        callback = RECONNECT_POLICY(invoke)
        raw.reconnect_policy = C.cast(callback, C.c_void_p).value

    raw._keepalive = keepalive
    return raw, callback


def subscription_permissions(lib, permissions: list[ParticipantTrackPermission]):
    raw = (ParticipantTrackPermissionRaw * len(permissions))()
    keepalive: list[object] = []
    for index, permission in enumerate(permissions):
        if not (permission.participant_sid or permission.participant_identity):
            raise ValueError("participant permission requires a SID or identity")
        lib.lk_participant_track_permission_init(C.byref(raw[index]))
        raw[index].participant_sid = utf8(permission.participant_sid)
        raw[index].participant_identity = utf8(permission.participant_identity)
        raw[index].allow_all = int(permission.allow_all)
        tracks = (C.c_char_p * len(permission.allowed_track_sids))(
            *(utf8(sid) for sid in permission.allowed_track_sids)
        )
        raw[index].allowed_track_sids = tracks
        raw[index].allowed_track_sid_count = len(tracks)
        keepalive.append(tracks)
    raw._keepalive = keepalive
    return raw
