"""Detached public values shared by room events and snapshots."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import IntEnum

from ._native import LiveKitError, ParticipantInfo, TrackPublicationInfo, decode
from .e2ee import E2EEOptions


class ConnectionState(IntEnum):
    CONNECTING = 0
    CONNECTED = 1
    DISCONNECTING = 2
    DISCONNECTED = 3
    FAILED = 4
    RECONNECTING = 5


@dataclass(frozen=True)
class RoomOptions:
    auto_subscribe: bool = True
    adaptive_stream: bool = False
    dynacast: bool = False
    join_retries: int | None = None
    reconnect_timeout_ms: int | None = None
    e2ee: E2EEOptions | None = None
    encryption: E2EEOptions | None = None


@dataclass(frozen=True)
class Participant:
    sid: str
    identity: str
    name: str
    metadata: str
    audio_level: float = 0.0
    connection_quality: int = 0
    is_speaking: bool = False
    is_local: bool = False
    attributes: dict[str, str] = field(default_factory=dict)
    track_publications: dict[str, TrackPublication] = field(default_factory=dict)

    @classmethod
    def from_native(cls, info: ParticipantInfo) -> Participant:
        return cls(
            sid=decode(info.sid), identity=decode(info.identity),
            name=decode(info.name), metadata=decode(info.metadata),
            audio_level=info.audio_level, connection_quality=info.connection_quality,
            is_speaking=bool(info.is_speaking), is_local=bool(info.is_local),
        )


@dataclass(frozen=True)
class TrackPublication:
    sid: str
    name: str
    mime_type: str
    kind: int
    source: int
    width: int
    height: int
    is_muted: bool
    is_simulcasted: bool
    subscription_allowed: bool
    subscription_status: int = 0
    has_subscribed_track: bool = False

    @classmethod
    def from_native(cls, info: TrackPublicationInfo) -> TrackPublication:
        return cls(
            sid=decode(info.sid), name=decode(info.name),
            mime_type=decode(info.mime_type), kind=info.kind, source=info.source,
            width=info.width, height=info.height, is_muted=bool(info.is_muted),
            is_simulcasted=bool(info.is_simulcasted),
            subscription_allowed=bool(info.subscription_allowed),
        )


@dataclass(frozen=True)
class DataPacket:
    data: bytes
    topic: str
    participant_identity: str
    reliable: bool


@dataclass(frozen=True)
class ChatMessage:
    id: str
    timestamp: int
    message: str
    participant_identity: str
    edit_timestamp: int | None = None
    deleted: bool = False
    generated: bool = False


class StreamEventType(IntEnum):
    OPEN = 0
    CHUNK = 1
    CLOSED = 2
    FAILED = 3


@dataclass(frozen=True)
class TextMessage:
    stream_id: str
    text: str
    topic: str
    participant_identity: str
    reply_to_stream_id: str
    timestamp: int
    attached_stream_ids: tuple[str, ...]
    attributes: dict[str, str]


@dataclass(frozen=True)
class ByteMessage:
    data: bytes
    stream_id: str
    name: str
    mime_type: str
    topic: str
    participant_identity: str
    timestamp: int
    attributes: dict[str, str]


@dataclass(frozen=True)
class TextStreamEvent:
    type: StreamEventType
    stream_id: str
    mime_type: str
    topic: str
    participant_identity: str
    content: str
    chunk_index: int
    total_size: int | None
    reason: str
    attributes: dict[str, str]
    timestamp: int
    reply_to_stream_id: str
    attached_stream_ids: tuple[str, ...]


@dataclass(frozen=True)
class ByteStreamEvent:
    type: StreamEventType
    stream_id: str
    name: str
    mime_type: str
    topic: str
    participant_identity: str
    content: bytes
    chunk_index: int
    total_size: int | None
    reason: str
    attributes: dict[str, str]
    timestamp: int


class RpcError(LiveKitError):
    def __init__(self, message: str, code: int, data: str = ""):
        super().__init__(message, code)
        self.data = data


def copy_attributes(values, count: int) -> dict[str, str]:
    return {decode(values[index].key): decode(values[index].value)
            for index in range(count)} if values else {}


def copy_strings(values, count: int) -> tuple[str, ...]:
    return tuple(decode(values[index]) for index in range(count)) if values else ()
