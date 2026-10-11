"""Room lifecycle and events backed by the livekit-client-cpp C ABI."""

from __future__ import annotations

import asyncio
import codecs
import ctypes as C
import threading
from concurrent.futures import ThreadPoolExecutor
from typing import Callable

from ._native import (
    DATA_EVENT,
    DATA_TRACK_EVENT,
    DATA_TRACK_FRAME_EVENT,
    AUDIO_FRAME_EVENT,
    VIDEO_FRAME_EVENT,
    VIDEO_FRAME_METADATA_EVENT,
    FILE_EVENT,
    TEXT_EVENT,
    TEXT_STREAM_HANDLER,
    BYTE_STREAM_HANDLER,
    CHAT_EVENT,
    METADATA_EVENT,
    PARTICIPANT_EVENT,
    REASON_EVENT,
    ROOM_EVENT,
    STATE_EVENT,
    TRACK_EVENT,
    TRACK_FEEDBACK_EVENT,
    ENCRYPTION_EVENT,
    DataPublishOptions,
    TextSendOptions,
    ByteSendOptions,
    StreamTextOptions,
    StreamBytesOptions,
    TrackRecorderOptionsRaw,
    FileSendOptions,
    Attribute,
    DataTrackSnapshotInfoRaw,
    MediaStreamOptionsRaw,
    ParticipantSnapshotInfoRaw,
    ParticipantPermissionsRaw,
    AudioPlaybackStatsRaw,
    LiveKitError,
    RoomCallbacks,
    RemoteTrackPublicationInfo,
    RemoteTrackSettings,
    RpcPerformOptions,
    decode,
    native,
    utf8,
)
from ._models import (
    AudioPlaybackStats,
    ByteMessage,
    ByteStreamEvent,
    ChatMessage,
    ConnectionState,
    DataPacket,
    Participant,
    ParticipantPermissions,
    ParticipantTrackPermission,
    RoomOptions,
    RpcError,
    RpcInvocationData,
    StreamEventType,
    SubscriptionError,
    TextMessage,
    TextStreamEvent,
    TrackPublication,
    TrackStreamState,
    TrackSubscriptionStatus,
    TokenSourceFetchOptions,
    copy_attributes as _attributes,
    copy_strings as _strings,
)
from ._events import EventDispatcher
from ._connection import connection_options, subscription_permissions
from ._token_source import TokenSourceBridge, fetch_options_raw
from ._rpc import RpcHandlerBridge
from .media import (
    AudioFrame, AudioSource, LocalTrack, RemoteMediaStream, TrackPublishOptions,
    VideoFrame, VideoSource,
)
from .data_track import (
    DataTrackFrame, DataTrackPublication, DataTrackPublishOptions, DataTrackReader,
    DataTrackSchema, DataTrackSchemaEncoding, DataTrackSchemaId,
    DataTrackSubscriptionOptions, LocalDataTrack, RemoteDataTrack,
    _check as _check_data_track,
)
from .e2ee import E2EEManager, EncryptionState
from .stream import StreamCallbackBridge, StreamWriter
from .recording import TrackRecorder
from .stats import RtcTrackStats, read_rtc_stats

__version__ = "0.6.0"


class LocalParticipant:
    def __init__(self, room: Room):
        self._room = room

    def _string(self, suffix: str) -> str:
        self._room._assert_open()
        return self._room._native.string("lk_local_participant_" + suffix, self._room._handle)

    @property
    def snapshot(self) -> Participant:
        return self._room.local_participant_snapshot

    @property
    def sid(self) -> str:
        return self._string("sid")

    @property
    def identity(self) -> str:
        return self._string("identity")

    @property
    def name(self) -> str:
        return self._string("name")

    @property
    def metadata(self) -> str:
        return self._string("metadata")

    async def set_name(self, name: str) -> None:
        await self._room._run(
            self._room._native.lib.lk_local_participant_set_name,
            self._room._handle, utf8(name),
        )

    async def set_metadata(self, metadata: str) -> None:
        await self._room._run(
            self._room._native.lib.lk_local_participant_set_metadata,
            self._room._handle, utf8(metadata),
        )

    async def set_attributes(self, attributes: dict[str, str]) -> None:
        raw = (Attribute * len(attributes))(
            *(Attribute(utf8(key), utf8(value)) for key, value in attributes.items())
        )
        await self._room._run(
            self._room._native.lib.lk_local_participant_set_attributes,
            self._room._handle, raw, len(attributes),
        )

    async def set_track_subscription_permissions(
        self, *, allow_all_participants: bool,
        participant_permissions: list[ParticipantTrackPermission] | None = None,
    ) -> None:
        await self._room.set_track_subscription_permissions(
            allow_all_participants, participant_permissions
        )

    async def publish_data(
        self, data: bytes, *, reliable: bool = True, topic: str = "",
        destination_identities: list[str] | None = None,
    ) -> None:
        await self._room.publish_data(
            data, reliable=reliable, topic=topic,
            destination_identities=destination_identities,
        )

    async def publish_data_track(self, options: DataTrackPublishOptions) -> LocalDataTrack:
        return await self._room.publish_data_track(options)

    def register_rpc_method(self, method: str, handler: Callable[[RpcInvocationData], str]) -> None:
        self._room.register_rpc_method(method, handler)

    def unregister_rpc_method(self, method: str) -> None:
        self._room.unregister_rpc_method(method)

    async def create_audio_track(self, label: str, source: AudioSource) -> LocalTrack:
        return await self._create_track("audio", label, source)

    async def create_video_track(self, label: str, source: VideoSource) -> LocalTrack:
        return await self._create_track("video", label, source)

    async def publish_track(
        self, track: LocalTrack, options: TrackPublishOptions | None = None,
    ) -> None:
        if track._room is not self._room:
            raise ValueError("track belongs to another room")
        await track.publish(options)

    async def unpublish_track(
        self, track: LocalTrack, *, stop_on_unpublish: bool = False,
    ) -> None:
        if track._room is not self._room:
            raise ValueError("track belongs to another room")
        await track.unpublish(stop_on_unpublish=stop_on_unpublish)

    async def _create_track(self, kind: str, label: str, source) -> LocalTrack:
        expected = AudioSource if kind == "audio" else VideoSource
        if not isinstance(source, expected):
            raise TypeError(f"{kind} track requires {expected.__name__}")

        def create():
            with source._lock:
                source._assert_open()
                handle = C.c_void_p()
                fn = getattr(self._room._native.lib, f"lk_room_create_{kind}_track")
                self._room._native.check(fn(
                    self._room._handle, utf8(label), source._handle, C.byref(handle)
                ))
                track = LocalTrack(self._room, source, handle)
                self._room._local_tracks.add(track)
                return track

        return await self._room._run_callable(create)


class Room:
    """One LiveKit room. Await operations and call ``aclose`` before leaving the loop.

    Native callbacks only copy their arguments and schedule Python listeners on the
    asyncio loop. A single worker serializes blocking C API operations and teardown.
    """

    def __init__(self, loop: asyncio.AbstractEventLoop | None = None):
        self._native = native()
        self._handle = C.c_void_p()
        self._native.check(self._native.lib.lk_room_create(C.byref(self._handle)))
        self._executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="livekit-room")
        self._loop: asyncio.AbstractEventLoop | None = loop
        self._closed = False
        self._close_future: asyncio.Future | None = None
        self._dispatcher = EventDispatcher(lambda: self._loop)
        self._text_decoders: dict[str, codecs.IncrementalDecoder] = {}
        self._text_stream_topics: dict[str, str] = {}
        self._text_decoder_lock = threading.Lock()
        self._callbacks = RoomCallbacks()
        self._callback_refs: list[object] = []
        self._rpc_handlers: dict[str, RpcHandlerBridge] = {}
        self._local_tracks: set[LocalTrack] = set()
        self._local_data_tracks: set[LocalDataTrack] = set()
        self._data_track_readers: set[DataTrackReader] = set()
        self._media_streams: set[RemoteMediaStream] = set()
        self._stream_writers: set[StreamWriter] = set()
        self._recorders: set[TrackRecorder] = set()
        self._native.lib.lk_room_callbacks_init(C.byref(self._callbacks))
        self._install_callbacks()
        try:
            self._native.check(self._native.lib.lk_room_set_callbacks(
                self._handle, C.byref(self._callbacks)
            ))
        except BaseException:
            self._native.lib.lk_room_destroy(self._handle)
            self._executor.shutdown(wait=True)
            raise
        self.local_participant = LocalParticipant(self)
        self.e2ee = E2EEManager(self)
        self.e2ee_manager = self.e2ee

    def on(self, event: str, callback: Callable | None = None):
        """Register a listener; can also be used as ``@room.on('connected')``."""
        return self._dispatcher.on(event, callback)

    def off(self, event: str, callback: Callable) -> None:
        self._dispatcher.off(event, callback)

    def _register_stream_handler(self, kind: str, topic: str, handler: Callable) -> None:
        self._assert_open()
        loop = asyncio.get_running_loop()
        if self._loop is None:
            self._loop = loop
        elif self._loop is not loop:
            raise LiveKitError("room is bound to another event loop")
        if not callable(handler):
            raise TypeError("handler must be callable")
        key = (kind, topic)
        if key in self._dispatcher.stream_handlers:
            raise LiveKitError(f"{kind} stream topic is already registered: {topic}")
        factory = TEXT_STREAM_HANDLER if kind == "text" else BYTE_STREAM_HANDLER
        callback = factory(self._on_text_stream if kind == "text" else self._on_byte_stream)
        fn = getattr(self._native.lib, f"lk_room_register_{kind}_stream_handler")
        self._native.check(fn(self._handle, utf8(topic), callback, None))
        self._callback_refs.append(callback)
        self._dispatcher.stream_handlers[key] = handler

    def register_text_stream_handler(self, topic: str, handler: Callable) -> None:
        self._register_stream_handler("text", topic, handler)

    def register_byte_stream_handler(self, topic: str, handler: Callable) -> None:
        self._register_stream_handler("byte", topic, handler)

    def _unregister_stream_handler(self, kind: str, topic: str) -> None:
        self._assert_open()
        fn = getattr(self._native.lib, f"lk_room_unregister_{kind}_stream_handler")
        self._native.check(fn(self._handle, utf8(topic)))
        self._dispatcher.stream_handlers.pop((kind, topic), None)
        if kind == "text":
            with self._text_decoder_lock:
                for stream_id, stream_topic in tuple(self._text_stream_topics.items()):
                    if stream_topic == topic:
                        self._text_decoders.pop(stream_id, None)
                        self._text_stream_topics.pop(stream_id, None)

    def unregister_text_stream_handler(self, topic: str) -> None:
        self._unregister_stream_handler("text", topic)

    def unregister_byte_stream_handler(self, topic: str) -> None:
        self._unregister_stream_handler("byte", topic)

    def register_rpc_method(
        self, method: str, handler: Callable[[RpcInvocationData], str],
    ) -> None:
        self._assert_open()
        loop = asyncio.get_running_loop()
        if self._loop is None:
            self._loop = loop
        elif self._loop is not loop:
            raise LiveKitError("room is bound to another event loop")
        bridge = RpcHandlerBridge(handler, loop)
        self._native.check(self._native.lib.lk_room_register_rpc_method_out(
            self._handle, utf8(method), bridge.callback, None
        ))
        self._rpc_handlers[method] = bridge
        self._callback_refs.append(bridge)

    def unregister_rpc_method(self, method: str) -> None:
        self._assert_open()
        self._native.check(self._native.lib.lk_room_unregister_rpc_method(
            self._handle, utf8(method)
        ))
        self._rpc_handlers.pop(method, None)

    def _post(self, event: str, *args) -> None:
        self._dispatcher.post(event, *args)

    @property
    def dropped_events(self) -> int:
        return self._dispatcher.dropped_events

    def _assert_open(self) -> None:
        if self._closed:
            raise LiveKitError("room is closed")

    def _callback(self, field: str, factory, fn) -> None:
        callback = factory(fn)
        self._callback_refs.append(callback)
        setattr(self._callbacks, field, C.cast(callback, C.c_void_p).value)

    def _install_callbacks(self) -> None:
        for event in ("connected", "disconnected", "reconnecting", "reconnected",
                      "token_refreshed", "room_eos"):
            self._callback("on_" + event, ROOM_EVENT,
                           lambda _user, _room, name=event: self._post(name))
        for event in ("participant_connected", "participant_disconnected"):
            self._callback("on_" + event, PARTICIPANT_EVENT,
                           lambda _user, _room, info, name=event:
                           self._post(name, Participant.from_native(info.contents))
                           if info else None)
        self._callback("on_data_received", DATA_EVENT, self._on_data)
        self._callback("on_connection_state_changed", STATE_EVENT,
                       lambda _user, _room, state:
                       self._post("connection_state_changed", ConnectionState(state)))
        self._callback("on_disconnected_with_reason", REASON_EVENT,
                       lambda _user, _room, reason: self._post("disconnect_reason", reason))
        self._callback("on_room_metadata_changed", METADATA_EVENT,
                       lambda _user, _room, value:
                       self._post("room_metadata_changed", decode(value)))
        self._callback("on_recording_status_changed", STATE_EVENT,
                       lambda _user, _room, recording:
                       self._post("recording_status_changed", bool(recording)))
        self._callback("on_chat_message_received", CHAT_EVENT, self._on_chat)
        for event in ("data_track_published", "data_track_unpublished",
                      "local_data_track_published", "local_data_track_unpublished"):
            self._callback("on_" + event, DATA_TRACK_EVENT,
                           lambda _user, _room, track, participant, name=event:
                           self._post(name,
                                      DataTrackPublication.from_native(track.contents),
                                      Participant.from_native(participant.contents))
                           if track and participant else None)
        self._callback("on_data_track_frame", DATA_TRACK_FRAME_EVENT,
                       self._on_data_track_frame)
        self._callback("on_audio_frame", AUDIO_FRAME_EVENT, self._on_audio_frame)
        self._callback("on_video_frame", VIDEO_FRAME_EVENT, self._on_video_frame)
        self._callback("on_video_frame_with_metadata", VIDEO_FRAME_METADATA_EVENT,
                       self._on_video_frame_with_metadata)
        self._callback("on_encryption_state_changed", ENCRYPTION_EVENT,
                       lambda _user, _room, value:
                       self._post("encryption_state_changed",
                                  EncryptionState.from_native(value.contents))
                       if value else None)
        self._callback("on_text_received", TEXT_EVENT, self._on_text)
        self._callback("on_file_received", FILE_EVENT,
                       lambda user, room, value: self._on_bytes("file_received", value))
        self._callback("on_byte_received", FILE_EVENT,
                       lambda user, room, value: self._on_bytes("byte_received", value))
        for event in ("track_published", "track_unpublished", "track_muted",
                      "track_unmuted", "track_subscribed", "track_unsubscribed",
                      "local_track_published", "local_track_unpublished",
                      "local_track_subscribed"):
            self._callback("on_" + event, TRACK_EVENT,
                           lambda _user, _room, track, participant, name=event:
                           self._post(name,
                                      TrackPublication.from_native(track.contents),
                                      Participant.from_native(participant.contents))
                           if track and participant else None)
        for event, convert in (
            ("track_subscription_permission_changed", bool),
            ("track_subscription_failed", lambda value: SubscriptionError(
                value) if value in SubscriptionError._value2member_map_
                else SubscriptionError.UNKNOWN),
            ("track_stream_state_changed", lambda value: TrackStreamState(
                value) if value in TrackStreamState._value2member_map_
                else TrackStreamState.UNKNOWN),
            ("track_subscription_status_changed", lambda value: TrackSubscriptionStatus(
                value) if value in TrackSubscriptionStatus._value2member_map_ else value),
        ):
            self._callback("on_" + event, TRACK_FEEDBACK_EVENT,
                           lambda _user, _room, track, participant, value,
                           name=event, convert=convert:
                           self._post(name,
                                      TrackPublication.from_native(track.contents),
                                      Participant.from_native(participant.contents),
                                      convert(value))
                           if track and participant else None)

    def _on_data(self, _user, _room, received) -> None:
        if not received:
            return
        value = received.contents
        packet = DataPacket(
            data=C.string_at(value.data, value.data_size) if value.data_size else b"",
            topic=decode(value.topic), participant_identity=decode(value.participant_identity),
            reliable=bool(value.reliable),
        )
        self._post("data_received", packet)

    def _on_data_track_frame(self, _user, _room, track, participant, frame) -> None:
        if not track or not participant or not frame:
            return
        value = frame.contents
        self._post("data_track_frame",
                   DataTrackPublication.from_native(track.contents),
                   Participant.from_native(participant.contents),
                   DataTrackFrame(
                       C.string_at(value.data, value.data_size) if value.data_size else b"",
                       value.user_timestamp if value.has_user_timestamp else None,
                   ))

    def _on_audio_frame(self, _user, _room, track, participant, frame) -> None:
        if track and participant and frame:
            self._post("audio_frame", TrackPublication.from_native(track.contents),
                       Participant.from_native(participant.contents),
                       AudioFrame.from_native(frame.contents))

    def _on_video_frame(self, _user, _room, track, participant, frame) -> None:
        if track and participant and frame:
            self._post("video_frame", TrackPublication.from_native(track.contents),
                       Participant.from_native(participant.contents),
                       VideoFrame.from_native(frame.contents))

    def _on_video_frame_with_metadata(
        self, _user, _room, track, participant, frame, metadata,
    ) -> None:
        if track and participant and frame:
            self._post("video_frame_with_metadata",
                       TrackPublication.from_native(track.contents),
                       Participant.from_native(participant.contents),
                       VideoFrame.from_native(frame.contents,
                                              metadata.contents if metadata else None))

    def _on_chat(self, _user, _room, received) -> None:
        if not received:
            return
        value = received.contents
        self._post("chat_message_received", ChatMessage(
            id=decode(value.id), timestamp=value.timestamp,
            message=decode(value.message),
            participant_identity=decode(value.participant_identity),
            edit_timestamp=value.edit_timestamp if value.has_edit_timestamp else None,
            deleted=bool(value.deleted), generated=bool(value.generated),
        ))

    def _on_text(self, _user, _room, received) -> None:
        if not received:
            return
        value = received.contents
        self._post("text_received", TextMessage(
            stream_id=decode(value.stream_id), text=decode(value.text),
            topic=decode(value.topic),
            participant_identity=decode(value.participant_identity),
            reply_to_stream_id=decode(value.reply_to_stream_id),
            timestamp=value.timestamp,
            attached_stream_ids=_strings(
                value.attached_stream_ids, value.attached_stream_id_count
            ),
            attributes=_attributes(value.attributes, value.attribute_count),
        ))

    def _on_bytes(self, event: str, received) -> None:
        if not received:
            return
        value = received.contents
        self._post(event, ByteMessage(
            data=C.string_at(value.data, value.data_size) if value.data_size else b"",
            stream_id=decode(value.stream_id), name=decode(value.name),
            mime_type=decode(value.mime_type), topic=decode(value.topic),
            participant_identity=decode(value.participant_identity),
            timestamp=value.timestamp,
            attributes=_attributes(value.attributes, value.attribute_count),
        ))

    def _on_text_stream(self, _user, _room, received) -> None:
        if not received:
            return
        value = received.contents
        content = C.string_at(value.content, value.content_size) if value.content_size else b""
        stream_id = decode(value.stream_id)
        topic = decode(value.topic)
        with self._text_decoder_lock:
            self._text_stream_topics[stream_id] = topic
            if value.type == StreamEventType.OPEN:
                self._text_decoders[stream_id] = codecs.getincrementaldecoder("utf-8")(
                    errors="replace"
                )
            decoder = self._text_decoders.setdefault(
                stream_id, codecs.getincrementaldecoder("utf-8")(errors="replace")
            )
            decoded_content = decoder.decode(
                content, final=value.type in (StreamEventType.CLOSED, StreamEventType.FAILED)
            )
            if value.type in (StreamEventType.CLOSED, StreamEventType.FAILED):
                self._text_decoders.pop(stream_id, None)
                self._text_stream_topics.pop(stream_id, None)
        self._post("text_stream_event", TextStreamEvent(
            type=StreamEventType(value.type), stream_id=stream_id,
            mime_type=decode(value.mime_type), topic=topic,
            participant_identity=decode(value.participant_identity),
            content=decoded_content,
            chunk_index=value.chunk_index,
            total_size=value.total_size if value.has_total_size else None,
            reason=decode(value.reason),
            attributes=_attributes(value.attributes, value.attribute_count),
            timestamp=value.timestamp,
            reply_to_stream_id=decode(value.reply_to_stream_id),
            attached_stream_ids=_strings(
                value.attached_stream_ids, value.attached_stream_id_count
            ),
        ))

    def _on_byte_stream(self, _user, _room, received) -> None:
        if not received:
            return
        value = received.contents
        self._post("byte_stream_event", ByteStreamEvent(
            type=StreamEventType(value.type), stream_id=decode(value.stream_id),
            name=decode(value.name), mime_type=decode(value.mime_type),
            topic=decode(value.topic),
            participant_identity=decode(value.participant_identity),
            content=C.string_at(value.content, value.content_size)
            if value.content_size else b"",
            chunk_index=value.chunk_index,
            total_size=value.total_size if value.has_total_size else None,
            reason=decode(value.reason),
            attributes=_attributes(value.attributes, value.attribute_count),
            timestamp=value.timestamp,
        ))

    async def _run_callable(self, call):
        if self._closed:
            raise LiveKitError("room is closed")
        loop = asyncio.get_running_loop()
        if self._loop is None:
            self._loop = loop
        elif self._loop is not loop:
            raise LiveKitError("room is bound to another event loop")

        return await loop.run_in_executor(self._executor, call)

    async def _run(self, fn, *args) -> None:
        def call():
            self._native.check(fn(*args))

        await self._run_callable(call)

    async def connect(self, url: str, token: str, options: RoomOptions | None = None) -> None:
        options = options or RoomOptions()
        raw, callback = connection_options(self._native.lib, options)
        if callback is not None:
            self._callback_refs.append(callback)
        await self._run(self._native.lib.lk_room_connect_with_options,
                        self._handle, utf8(url), utf8(token), C.byref(raw))

    async def connect_with_token_source(
        self, provider: Callable,
        fetch_options: TokenSourceFetchOptions | None = None,
        options: RoomOptions | None = None,
    ) -> None:
        """Connect using a sync or async provider, including later token refreshes."""
        self._assert_open()
        loop = asyncio.get_running_loop()
        if self._loop is not None and self._loop is not loop:
            raise LiveKitError("room is bound to another event loop")
        bridge = TokenSourceBridge(provider, loop)
        source = fetch_options_raw(
            self._native.lib, fetch_options or TokenSourceFetchOptions()
        )
        connect, policy = connection_options(self._native.lib, options or RoomOptions())
        self._callback_refs.append(bridge)
        if policy is not None:
            self._callback_refs.append(policy)
        try:
            await self._run(
                self._native.lib.lk_room_connect_with_token_source_and_options,
                self._handle, bridge.callback, None, C.byref(source), C.byref(connect),
            )
        except LiveKitError as error:
            if bridge.last_error is not None:
                raise LiveKitError(f"token source failed: {bridge.last_error}", error.code) from error
            raise

    async def disconnect(self) -> None:
        await self._run(self._native.lib.lk_room_disconnect, self._handle)

    async def republish_all_tracks(self) -> None:
        await self._run(self._native.lib.lk_room_republish_all_tracks, self._handle)

    async def publish_data(
        self, data: bytes, *, reliable: bool = True, topic: str = "",
        destination_identities: list[str] | None = None,
    ) -> None:
        if not isinstance(data, bytes):
            raise TypeError("data must be bytes")
        raw = DataPublishOptions()
        self._native.lib.lk_data_publish_options_init(C.byref(raw))
        raw.reliable = int(reliable)
        raw.topic = utf8(topic)
        destinations = [utf8(identity) for identity in (destination_identities or [])]
        array = (C.c_char_p * len(destinations))(*destinations)
        raw.destination_identities = array
        raw.destination_identity_count = len(destinations)
        payload = C.create_string_buffer(data)
        await self._run(self._native.lib.lk_room_publish_data, self._handle,
                        C.cast(payload, C.c_void_p), len(data), C.byref(raw))

    async def store_data_track_schema(self, schema: DataTrackSchema) -> None:
        if not isinstance(schema.definition, bytes):
            raise TypeError("schema definition must be bytes")
        raw = schema.id._raw(self._native.lib)
        payload = C.create_string_buffer(schema.definition)

        def call():
            _check_data_track(self._native, self._native.lib.lk_room_store_data_track_schema(
                self._handle, C.byref(raw), C.cast(payload, C.c_void_p),
                len(schema.definition),
            ))
        await self._run_callable(call)

    async def get_data_track_schema(
        self, participant_identity: str, schema_id: DataTrackSchemaId,
    ) -> DataTrackSchema:
        raw = schema_id._raw(self._native.lib)

        def call():
            lib = self._native.lib
            handle = C.c_void_p()
            _check_data_track(self._native, lib.lk_room_get_data_track_schema(
                self._handle, utf8(participant_identity), C.byref(raw), C.byref(handle)
            ))
            try:
                name = self._native.string("lk_data_track_schema_name", handle)
                encoding = DataTrackSchemaEncoding(lib.lk_data_track_schema_encoding(handle))
                custom = self._native.string("lk_data_track_schema_custom_encoding", handle)
                size = lib.lk_data_track_schema_definition(handle, None, 0)
                buffer = C.create_string_buffer(size)
                lib.lk_data_track_schema_definition(handle, C.cast(buffer, C.c_void_p), size)
                return DataTrackSchema(DataTrackSchemaId(name, encoding, custom),
                                       bytes(buffer.raw[:size]))
            finally:
                lib.lk_data_track_schema_destroy(handle)
        return await self._run_callable(call)

    async def publish_data_track(self, options: DataTrackPublishOptions) -> LocalDataTrack:
        raw = options._raw(self._native.lib)

        def call():
            handle = C.c_void_p()
            _check_data_track(self._native, self._native.lib.lk_room_publish_data_track(
                self._handle, C.byref(raw), C.byref(handle)
            ))
            track = LocalDataTrack(self, handle)
            self._local_data_tracks.add(track)
            return track
        return await self._run_callable(call)

    async def subscribe_data_track(
        self, participant_identity: str, track_sid: str,
        options: DataTrackSubscriptionOptions | None = None,
    ) -> DataTrackReader:
        raw = (options or DataTrackSubscriptionOptions())._raw(self._native.lib)

        def call():
            handle = C.c_void_p()
            _check_data_track(self._native, self._native.lib.lk_room_subscribe_data_track(
                self._handle, utf8(participant_identity), utf8(track_sid),
                C.byref(raw), C.byref(handle),
            ))
            reader = DataTrackReader(self, handle)
            self._data_track_readers.add(reader)
            return reader
        return await self._run_callable(call)

    async def update_data_track_subscription_options(
        self, participant_identity: str, track_sid: str,
        options: DataTrackSubscriptionOptions,
    ) -> None:
        raw = options._raw(self._native.lib)

        def call():
            _check_data_track(self._native,
                self._native.lib.lk_room_update_data_track_subscription_options(
                    self._handle, utf8(participant_identity), utf8(track_sid), C.byref(raw)
                ))
        await self._run_callable(call)

    async def start_track_recording(
        self, participant_identity: str, track_sid: str, output_path: str,
        *, queue_capacity: int = 256,
    ) -> TrackRecorder:
        if not participant_identity or not track_sid or not output_path:
            raise ValueError("participant identity, track SID, and output path are required")
        if queue_capacity <= 0:
            raise ValueError("queue_capacity must be positive")
        raw = TrackRecorderOptionsRaw()
        self._native.lib.lk_track_recorder_options_init(C.byref(raw))
        raw.output_path = utf8(output_path)
        raw.queue_capacity = queue_capacity

        def call():
            handle = C.c_void_p()
            self._native.check(self._native.lib.lk_room_start_track_recording(
                self._handle, utf8(participant_identity), utf8(track_sid),
                C.byref(raw), C.byref(handle)
            ))
            recorder = TrackRecorder(self, handle)
            self._recorders.add(recorder)
            return recorder

        return await self._run_callable(call)

    async def create_audio_stream(
        self, participant_identity: str, track_sid: str, *, capacity: int = 16,
    ) -> RemoteMediaStream:
        return await self._create_media_stream(
            "audio", participant_identity, track_sid, capacity
        )

    async def create_video_stream(
        self, participant_identity: str, track_sid: str, *, capacity: int = 16,
    ) -> RemoteMediaStream:
        return await self._create_media_stream(
            "video", participant_identity, track_sid, capacity
        )

    async def _create_media_stream(
        self, kind: str, participant_identity: str, track_sid: str, capacity: int,
    ) -> RemoteMediaStream:
        if capacity <= 0:
            raise ValueError("media stream capacity must be positive")
        raw = MediaStreamOptionsRaw()
        self._native.lib.lk_media_stream_options_init(C.byref(raw))
        raw.capacity = capacity

        def call():
            handle = C.c_void_p()
            fn = getattr(self._native.lib, f"lk_room_create_{kind}_stream")
            self._native.check(fn(
                self._handle, utf8(participant_identity), utf8(track_sid),
                C.byref(raw), C.byref(handle),
            ))
            stream = RemoteMediaStream(self, kind, handle)
            self._media_streams.add(stream)
            return stream
        return await self._run_callable(call)

    async def publish_dtmf(self, code: int, digit: str) -> None:
        await self._run(self._native.lib.lk_room_publish_dtmf,
                        self._handle, code, utf8(digit))

    async def send_chat_message(self, message: str) -> tuple[str, int]:
        message_id = C.create_string_buffer(37)
        timestamp = C.c_int64()
        await self._run(self._native.lib.lk_room_send_chat_message,
                        self._handle, utf8(message), message_id, len(message_id),
                        C.byref(timestamp))
        return decode(message_id.value), timestamp.value

    async def edit_chat_message(
        self, message_id: str, original_timestamp: int, message: str,
    ) -> None:
        await self._run(self._native.lib.lk_room_edit_chat_message,
                        self._handle, utf8(message_id), original_timestamp, utf8(message))

    async def send_text(
        self, text: str, *, topic: str = "", destination_identities: list[str] | None = None,
        attributes: dict[str, str] | None = None, compress: bool = False,
    ) -> None:
        raw = TextSendOptions()
        self._native.lib.lk_text_send_options_init(C.byref(raw))
        raw.topic = utf8(topic)
        destinations = [utf8(value) for value in (destination_identities or [])]
        raw.destination_identities = (C.c_char_p * len(destinations))(*destinations)
        raw.destination_identity_count = len(destinations)
        attrs = attributes or {}
        raw.attributes = (Attribute * len(attrs))(
            *(Attribute(utf8(key), utf8(value)) for key, value in attrs.items())
        )
        raw.attribute_count = len(attrs)
        raw.compress = int(compress)
        await self._run(self._native.lib.lk_room_send_text,
                        self._handle, utf8(text), C.byref(raw))

    async def stream_text(
        self, *, topic: str = "", destination_identities: list[str] | None = None,
        attributes: dict[str, str] | None = None, reply_to_stream_id: str = "",
        attached_stream_ids: list[str] | None = None, stream_id: str = "",
        total_size: int | None = None, chunk_size: int = 15000,
        update: bool = False, version: int = 0, compress: bool = False,
    ) -> StreamWriter:
        return await self._stream_writer(
            "text", topic=topic, destination_identities=destination_identities,
            attributes=attributes, stream_id=stream_id, total_size=total_size,
            chunk_size=chunk_size, compress=compress,
            reply_to_stream_id=reply_to_stream_id,
            attached_stream_ids=attached_stream_ids, update=update, version=version,
        )

    async def stream_bytes(
        self, *, topic: str = "", mime_type: str = "application/octet-stream",
        name: str = "unknown", destination_identities: list[str] | None = None,
        attributes: dict[str, str] | None = None, stream_id: str = "",
        total_size: int | None = None, chunk_size: int = 15000,
        compress: bool = False,
    ) -> StreamWriter:
        return await self._stream_writer(
            "bytes", topic=topic, destination_identities=destination_identities,
            attributes=attributes, stream_id=stream_id, total_size=total_size,
            chunk_size=chunk_size, compress=compress, mime_type=mime_type, name=name,
        )

    async def _stream_writer(
        self, kind: str, *, topic: str, destination_identities: list[str] | None,
        attributes: dict[str, str] | None, stream_id: str, total_size: int | None,
        chunk_size: int, compress: bool, **extra,
    ) -> StreamWriter:
        if total_size is not None and not 0 <= total_size <= 0xFFFFFFFFFFFFFFFF:
            raise ValueError("total_size is out of range")
        if chunk_size <= 0:
            raise ValueError("chunk_size must be positive")
        if kind == "text" and not -0x80000000 <= extra["version"] <= 0x7FFFFFFF:
            raise ValueError("version is out of range")
        raw = StreamTextOptions() if kind == "text" else StreamBytesOptions()
        lib = self._native.lib
        getattr(lib, f"lk_stream_{kind}_options_init")(C.byref(raw))
        raw.topic = utf8(topic)
        destinations = [utf8(value) for value in (destination_identities or [])]
        raw.destination_identities = (C.c_char_p * len(destinations))(*destinations)
        raw.destination_identity_count = len(destinations)
        attrs = attributes or {}
        raw.attributes = (Attribute * len(attrs))(
            *(Attribute(utf8(key), utf8(value)) for key, value in attrs.items())
        )
        raw.attribute_count = len(attrs)
        raw.stream_id = utf8(stream_id) if stream_id else None
        raw.has_total_size = int(total_size is not None)
        raw.total_size = total_size or 0
        raw.chunk_size = chunk_size
        raw.compress = int(compress)
        callbacks = StreamCallbackBridge(self)
        raw.on_progress = C.cast(callbacks.progress, C.c_void_p)
        raw.on_complete = C.cast(callbacks.completion, C.c_void_p)
        if kind == "text":
            raw.reply_to_stream_id = utf8(extra["reply_to_stream_id"])
            attached = [utf8(value) for value in (extra["attached_stream_ids"] or [])]
            raw.attached_stream_ids = (C.c_char_p * len(attached))(*attached)
            raw.attached_stream_id_count = len(attached)
            raw.update = int(extra["update"])
            raw.version = extra["version"]
        else:
            raw.mime_type = utf8(extra["mime_type"])
            raw.name = utf8(extra["name"])

        def call():
            handle = C.c_void_p()
            self._native.check(getattr(lib, f"lk_room_stream_{kind}")(
                self._handle, C.byref(raw), C.byref(handle)
            ))
            writer = StreamWriter(self, kind, handle, callbacks)
            self._stream_writers.add(writer)
            return writer

        return await self._run_callable(call)

    async def send_bytes(
        self, data: bytes, *, topic: str = "", mime_type: str = "application/octet-stream",
        name: str = "", destination_identities: list[str] | None = None,
        attributes: dict[str, str] | None = None, compress: bool = False,
    ) -> None:
        if not isinstance(data, bytes):
            raise TypeError("data must be bytes")
        raw = ByteSendOptions()
        self._native.lib.lk_byte_send_options_init(C.byref(raw))
        raw.topic, raw.mime_type, raw.name = utf8(topic), utf8(mime_type), utf8(name)
        destinations = [utf8(value) for value in (destination_identities or [])]
        raw.destination_identities = (C.c_char_p * len(destinations))(*destinations)
        raw.destination_identity_count = len(destinations)
        attrs = attributes or {}
        raw.attributes = (Attribute * len(attrs))(
            *(Attribute(utf8(key), utf8(value)) for key, value in attrs.items())
        )
        raw.attribute_count = len(attrs)
        raw.compress = int(compress)
        payload = C.create_string_buffer(data)
        await self._run(self._native.lib.lk_room_send_bytes,
                        self._handle, C.cast(payload, C.c_void_p), len(data), C.byref(raw))

    async def send_file(
        self, path: str, *, topic: str = "", mime_type: str = "application/octet-stream",
        destination_identities: list[str] | None = None,
        attributes: dict[str, str] | None = None, compress: bool = False,
    ) -> None:
        raw = FileSendOptions()
        self._native.lib.lk_file_send_options_init(C.byref(raw))
        raw.topic, raw.mime_type = utf8(topic), utf8(mime_type)
        destinations = [utf8(value) for value in (destination_identities or [])]
        raw.destination_identities = (C.c_char_p * len(destinations))(*destinations)
        raw.destination_identity_count = len(destinations)
        attrs = attributes or {}
        raw.attributes = (Attribute * len(attrs))(
            *(Attribute(utf8(key), utf8(value)) for key, value in attrs.items())
        )
        raw.attribute_count = len(attrs)
        raw.compress = int(compress)
        await self._run(self._native.lib.lk_room_send_file,
                        self._handle, utf8(path), C.byref(raw))

    async def perform_rpc(
        self, destination_identity: str, method: str, payload: str,
        response_timeout_ms: int = 10000,
    ) -> str:
        raw = RpcPerformOptions()
        self._native.lib.lk_rpc_perform_options_init(C.byref(raw))
        raw.destination_identity = utf8(destination_identity)
        raw.method = utf8(method)
        raw.payload = utf8(payload)
        raw.response_timeout_ms = response_timeout_ms
        def call() -> str:
            lib = self._native.lib
            result = C.c_void_p()
            status = lib.lk_room_perform_rpc(
                self._handle, C.byref(raw), C.byref(result)
            )
            try:
                self._native.check(status)
                if not lib.lk_rpc_result_ok(result):
                    raise RpcError(
                        self._native.string("lk_rpc_result_error_message", result),
                        lib.lk_rpc_result_error_code(result),
                        self._native.string("lk_rpc_result_error_data", result),
                    )
                return self._native.string("lk_rpc_result_payload", result)
            finally:
                if result:
                    lib.lk_rpc_result_destroy(result)

        return await self._run_callable(call)

    async def set_remote_track_subscribed(
        self, participant_sid: str, track_sid: str, subscribed: bool,
    ) -> None:
        await self._run(self._native.lib.lk_room_set_remote_track_subscribed,
                        self._handle, utf8(participant_sid), utf8(track_sid), int(subscribed))

    async def set_track_subscription_permissions(
        self, all_participants_allowed: bool,
        participant_permissions: list[ParticipantTrackPermission] | None = None,
    ) -> None:
        permissions = subscription_permissions(
            self._native.lib, participant_permissions or []
        )
        await self._run(self._native.lib.lk_room_set_track_subscription_permissions,
                        self._handle, int(all_participants_allowed),
                        permissions, len(permissions))

    async def update_remote_track_settings(
        self, participant_sid: str, track_sid: str, *, enabled: bool = True,
        video_quality: int | None = None, video_width: int = 0,
        video_height: int = 0, video_fps: int = 0, priority: int = 0,
    ) -> None:
        raw = RemoteTrackSettings()
        self._native.lib.lk_remote_track_settings_init(C.byref(raw))
        raw.enabled = int(enabled)
        if video_quality is not None:
            raw.has_video_quality = 1
            raw.video_quality = video_quality
        raw.video_width = video_width
        raw.video_height = video_height
        raw.video_fps = video_fps
        raw.priority = priority
        await self._run(self._native.lib.lk_room_update_remote_track_settings,
                        self._handle, utf8(participant_sid), utf8(track_sid), C.byref(raw))

    @property
    def connection_state(self) -> ConnectionState:
        self._assert_open()
        return ConnectionState(self._native.lib.lk_room_state(self._handle))

    @property
    def is_connected(self) -> bool:
        self._assert_open()
        return bool(self._native.lib.lk_room_is_connected(self._handle))

    @property
    def is_recording(self) -> bool:
        self._assert_open()
        return bool(self._native.lib.lk_room_is_recording(self._handle))

    @property
    def audio_output_device(self) -> str:
        self._assert_open()
        return self._native.string("lk_room_audio_output_device", self._handle)

    async def set_audio_output_device(self, device_id: str) -> None:
        await self._run(self._native.lib.lk_room_set_audio_output_device,
                        self._handle, utf8(device_id))

    @property
    def speaker_volume(self) -> float:
        self._assert_open()
        return float(self._native.lib.lk_room_speaker_volume(self._handle))

    async def set_speaker_volume(self, volume: float) -> None:
        await self._run(self._native.lib.lk_room_set_speaker_volume,
                        self._handle, volume)

    @property
    def speaker_muted(self) -> bool:
        self._assert_open()
        return bool(self._native.lib.lk_room_speaker_is_muted(self._handle))

    async def set_speaker_muted(self, muted: bool) -> None:
        await self._run(self._native.lib.lk_room_set_speaker_muted,
                        self._handle, int(muted))

    @property
    def audio_playback_stats(self) -> AudioPlaybackStats:
        self._assert_open()
        raw = AudioPlaybackStatsRaw()
        self._native.lib.lk_audio_playback_stats_init(C.byref(raw))
        self._native.check(self._native.lib.lk_room_audio_playback_stats(
            self._handle, C.byref(raw)
        ))
        return AudioPlaybackStats(
            queued_frames=raw.queued_frames, played_frames=raw.played_frames,
            dropped_frames=raw.dropped_frames, underrun_frames=raw.underrun_frames,
            buffered_duration_ms=raw.buffered_duration_ms,
            device_latency_ms=raw.device_latency_ms,
            estimated_delay_ms=raw.estimated_delay_ms,
        )

    @property
    def sid(self) -> str:
        self._assert_open()
        return self._native.string("lk_room_sid", self._handle)

    @property
    def name(self) -> str:
        self._assert_open()
        return self._native.string("lk_room_name", self._handle)

    @property
    def metadata(self) -> str:
        self._assert_open()
        return self._native.string("lk_room_metadata", self._handle)

    def _participant_snapshot_data(self, handle, kind: str) -> dict:
        lib = self._native.lib
        prefix = f"lk_{kind}_participant_snapshot_"
        info = ParticipantSnapshotInfoRaw()
        getattr(lib, prefix + "info_init")(C.byref(info))
        self._native.check(getattr(lib, prefix + "info")(handle, C.byref(info)))
        permissions = ParticipantPermissionsRaw()
        self._native.check(getattr(lib, prefix + "permissions")(
            handle, C.byref(permissions)
        ))
        permission_value = ParticipantPermissions(
            can_subscribe=bool(permissions.can_subscribe),
            can_publish=bool(permissions.can_publish),
            can_publish_data=bool(permissions.can_publish_data),
            can_publish_sources=tuple(permissions.can_publish_sources[index]
                for index in range(permissions.can_publish_source_count)),
            hidden=bool(permissions.hidden), recorder=bool(permissions.recorder),
            can_update_metadata=bool(permissions.can_update_metadata),
            agent=bool(permissions.agent),
            can_subscribe_metrics=bool(permissions.can_subscribe_metrics),
            can_manage_agent_session=bool(permissions.can_manage_agent_session),
        )
        attributes = {}
        count = getattr(lib, prefix + "attribute_count")(handle)
        for index in range(count):
            def attribute(part: str) -> str:
                fn = getattr(lib, prefix + "attribute_" + part)
                size = fn(handle, index, None, 0)
                value = C.create_string_buffer(size)
                fn(handle, index, value, size)
                return decode(value.value)
            attributes[attribute("key")] = attribute("value")
        return dict(
            sid=self._native.string(prefix + "sid", handle),
            identity=self._native.string(prefix + "identity", handle),
            name=self._native.string(prefix + "name", handle),
            metadata=self._native.string(prefix + "metadata", handle),
            audio_level=info.audio_level,
            connection_quality=info.connection_quality,
            is_speaking=bool(info.is_speaking),
            is_local=(kind == "local"),
            attributes=attributes,
            permissions=permission_value,
        )

    @property
    def local_participant_snapshot(self) -> Participant:
        self._assert_open()
        lib = self._native.lib
        snapshot = C.c_void_p()
        self._native.check(lib.lk_room_create_local_participant_snapshot(
            self._handle, C.byref(snapshot)
        ))
        try:
            return Participant(**self._participant_snapshot_data(snapshot, "local"))
        finally:
            lib.lk_local_participant_snapshot_destroy(snapshot)

    def get_remote_track_rtc_stats(
        self, participant_identity: str, track_sid: str,
    ) -> tuple[RtcTrackStats, ...]:
        """Return detached normalized stats for a subscribed remote track."""
        self._assert_open()
        lib = self._native.lib
        participants = C.c_void_p()
        self._native.check(lib.lk_room_create_remote_participant_snapshot(
            self._handle, C.byref(participants)
        ))
        try:
            for index in range(lib.lk_remote_participant_list_count(participants)):
                participant = C.c_void_p()
                self._native.check(lib.lk_remote_participant_list_at(
                    participants, index, C.byref(participant)
                ))
                identity = self._native.string(
                    "lk_remote_participant_snapshot_identity", participant
                )
                if identity != participant_identity:
                    continue
                for track_index in range(
                    lib.lk_remote_participant_snapshot_publication_count(participant)
                ):
                    publication = C.c_void_p()
                    self._native.check(lib.lk_remote_participant_snapshot_publication_at(
                        participant, track_index, C.byref(publication)
                    ))
                    sid = self._native.string(
                        "lk_remote_track_publication_snapshot_sid", publication
                    )
                    if sid != track_sid:
                        continue
                    track = C.c_void_p()
                    self._native.check(lib.lk_remote_track_publication_snapshot_track(
                        publication, C.byref(track)
                    ))
                    if not track:
                        return ()
                    snapshot = C.c_void_p()
                    self._native.check(
                        lib.lk_remote_track_snapshot_create_rtc_stats_snapshot(
                            track, C.byref(snapshot)
                        )
                    )
                    return read_rtc_stats(self._native, snapshot)
            raise LiveKitError("remote track was not found")
        finally:
            lib.lk_remote_participant_list_destroy(participants)

    @property
    def remote_participants(self) -> dict[str, Participant]:
        """Detached snapshots keyed by participant identity."""
        self._assert_open()
        lib = self._native.lib
        snapshot = C.c_void_p()
        self._native.check(lib.lk_room_create_remote_participant_snapshot(
            self._handle, C.byref(snapshot)
        ))
        try:
            participants = {}
            for index in range(lib.lk_remote_participant_list_count(snapshot)):
                item = C.c_void_p()
                self._native.check(lib.lk_remote_participant_list_at(
                    snapshot, index, C.byref(item)
                ))
                publications = {}
                for publication_index in range(
                    lib.lk_remote_participant_snapshot_publication_count(item)
                ):
                    publication = C.c_void_p()
                    self._native.check(lib.lk_remote_participant_snapshot_publication_at(
                        item, publication_index, C.byref(publication)
                    ))
                    info = RemoteTrackPublicationInfo()
                    lib.lk_remote_track_publication_snapshot_info_init(C.byref(info))
                    self._native.check(lib.lk_remote_track_publication_snapshot_info(
                        publication, C.byref(info)
                    ))
                    string = lambda part: self._native.string(
                        "lk_remote_track_publication_snapshot_" + part, publication
                    )
                    track = TrackPublication(
                        sid=string("sid"), name=string("name"),
                        mime_type=string("mime_type"), kind=info.kind,
                        source=info.source, width=info.width, height=info.height,
                        is_muted=bool(info.is_muted),
                        is_simulcasted=bool(info.is_simulcasted),
                        subscription_allowed=bool(info.subscription_allowed),
                        subscription_status=info.subscription_status,
                        has_subscribed_track=bool(info.has_subscribed_track),
                    )
                    publications[track.sid] = track
                participant = Participant(
                    **self._participant_snapshot_data(item, "remote"),
                    track_publications=publications,
                )
                participants[participant.identity] = participant
            return participants
        finally:
            lib.lk_remote_participant_list_destroy(snapshot)

    @property
    def remote_data_tracks(self) -> dict[str, RemoteDataTrack]:
        self._assert_open()
        lib = self._native.lib
        snapshot = C.c_void_p()
        self._native.check(lib.lk_room_create_remote_data_track_snapshot(
            self._handle, C.byref(snapshot)
        ))
        try:
            tracks = {}
            for index in range(lib.lk_remote_data_track_list_count(snapshot)):
                handle = C.c_void_p()
                self._native.check(lib.lk_remote_data_track_list_at(
                    snapshot, index, C.byref(handle)
                ))
                info = DataTrackSnapshotInfoRaw()
                lib.lk_data_track_snapshot_info_init(C.byref(info))
                self._native.check(lib.lk_remote_data_track_snapshot_info(
                    handle, C.byref(info)
                ))
                get = lambda field: self._native.string(
                    "lk_remote_data_track_snapshot_" + field, handle
                )
                item = RemoteDataTrack(
                    publisher_identity=get("publisher_identity"), sid=get("sid"),
                    name=get("name"), publisher_handle=info.publisher_handle,
                    uses_e2ee=bool(info.uses_e2ee),
                    frame_encoding=info.frame_encoding if info.has_frame_encoding else None,
                    custom_frame_encoding=get("custom_frame_encoding"),
                    schema_name=get("schema_name"),
                    schema_encoding=info.schema_encoding if info.has_schema else None,
                    custom_schema_encoding=get("custom_schema_encoding"),
                    is_published=bool(info.is_published),
                )
                tracks[item.sid] = item
            return tracks
        finally:
            lib.lk_remote_data_track_list_destroy(snapshot)

    async def aclose(self) -> None:
        loop = asyncio.get_running_loop()
        if self._loop is not None and self._loop is not loop:
            raise LiveKitError("room is bound to another event loop")
        if self._loop is None:
            self._loop = loop
        if self._close_future is None:
            self._closed = True
            self._close_future = loop.run_in_executor(
                self._executor, self._destroy_native
            )
            self._close_future.add_done_callback(self._finish_close)
        try:
            await asyncio.shield(self._close_future)
        except asyncio.CancelledError:
            await asyncio.shield(self._close_future)
            raise

    def _destroy_native(self) -> None:
        for recorder in tuple(self._recorders):
            recorder._destroy()
        for writer in tuple(self._stream_writers):
            writer._destroy()
        for reader in tuple(self._data_track_readers):
            reader.close()
        for stream in tuple(self._media_streams):
            stream.close()
        self._native.lib.lk_room_destroy(self._handle)
        for track in tuple(self._local_tracks):
            if track._handle:
                self._native.check(self._native.lib.lk_local_track_destroy(track._handle))
                track._handle = C.c_void_p()
        self._local_tracks.clear()
        for track in tuple(self._local_data_tracks):
            if track._handle:
                _check_data_track(self._native,
                    self._native.lib.lk_local_data_track_destroy(track._handle))
                track._handle = C.c_void_p()
        self._local_data_tracks.clear()

    def _finish_close(self, _future: asyncio.Future) -> None:
        self._executor.shutdown(wait=False)
        self._handle = C.c_void_p()
        self._callback_refs.clear()
        self._rpc_handlers.clear()
        self._dispatcher.close()
        with self._text_decoder_lock:
            self._text_decoders.clear()
            self._text_stream_topics.clear()

    async def __aenter__(self) -> Room:
        return self

    async def __aexit__(self, *_exc) -> None:
        await self.aclose()
